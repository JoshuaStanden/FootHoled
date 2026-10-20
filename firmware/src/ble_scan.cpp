/**
 * @file ble_scan.cpp
 * @brief Passive Bluetooth Low Energy device discovery for FootHoled recon.
 *
 * Mirrors wifi_scan.cpp: a background worker drives repeated scan cycles and
 * publishes a mutex-guarded snapshot the UI polls. The two services are
 * deliberately shaped the same so ui.cpp can treat Wi-Fi and BLE identically.
 *
 * Radio: uses the NimBLE host (CONFIG_BT_NIMBLE_ENABLED). Scanning is PASSIVE
 * (listen only, no scan requests, no connections) ΓÇö appropriate for recon and
 * low-impact on the shared 2.4 GHz radio it co-exists with Wi-Fi on.
 *
 * Refresh timing (matches the Wi-Fi scanner's two-tier model):
 *   - the UI re-reads the cached snapshot every ~500 ms (see ui.cpp),
 *   - the radio runs a fresh ~3 s discovery window each worker cycle.
 * A 1ΓÇô2 s radio window was considered and rejected: BLE advertisers broadcast
 * on an interval (often 1ΓÇô2 s apart), so too short a window misses devices and
 * the list flickers. ~3 s reliably catches active advertisers while keeping the
 * on-screen list feeling live.
 *
 * TODO(HW): not compiled or flashed on this host (no ESP-IDF toolchain). The
 * NimBLE field names and the sdkconfig BLE knobs need one real build to confirm.
 */
#include "ble_scan.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "recon_log.h"
#include <string.h>

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/util/util.h"
#include "nimble/ble.h"

namespace {
constexpr size_t MAX_DEV = 48;          // same ceiling as the Wi-Fi scanner
constexpr int32_t SCAN_WINDOW_MS = 3000; // radio discovery window per cycle
FootHoledBLEDevice devices[MAX_DEV] = {};
size_t dev_count = 0;
uint32_t generation = 0;
bool requested = false;      // UI asked us to be scanning
bool ready = false;          // host stack up and sync'd
bool error_state = false;
bool scanning = false;       // a GAP discovery is currently in flight
SemaphoreHandle_t guard = nullptr;
TaskHandle_t worker = nullptr;
uint8_t own_addr_type = 0;
const char *TAG = "FootHoled/ble";

// Working table filled by the GAP callback during one scan window, then
// published to `devices` atomically when the window ends. De-duplicated by
// address so a device advertising repeatedly appears once with its latest RSSI.
FootHoledBLEDevice working[MAX_DEV] = {};
size_t working_count = 0;

int find_working(const uint8_t *addr) {
    for (size_t i = 0; i < working_count; ++i) {
        if (memcmp(working[i].addr, addr, 6) == 0) return (int)i;
    }
    return -1;
}

// Pull the advertised local name (complete or shortened) out of the adv data.
void extract_name(const uint8_t *data, uint8_t len, char *out, size_t out_sz) {
    out[0] = 0;
    size_t i = 0;
    while (i + 1 < len) {
        uint8_t field_len = data[i];
        if (field_len == 0 || i + 1 + field_len > len) break;
        uint8_t type = data[i + 1];
        if (type == BLE_HS_ADV_TYPE_COMP_NAME || type == BLE_HS_ADV_TYPE_INCOMP_NAME) {
            uint8_t name_len = field_len - 1;
            if (name_len >= out_sz) name_len = (uint8_t)(out_sz - 1);
            memcpy(out, &data[i + 2], name_len);
            out[name_len] = 0;
            return;
        }
        i += 1 + field_len;
    }
}

// Parse capability fields out of the advertisement with the same TLV walk as
// extract_name. Fills flags/appearance/tx_power/service-UUIDs/mfg-id on `dev`;
// absent fields are left at their "none" sentinels. AD type constants per the
// Bluetooth Core Supplement (CSS) / NimBLE BLE_HS_ADV_TYPE_* values.
void extract_caps(const uint8_t *data, uint8_t len, FootHoledBLEDevice *dev) {
    dev->adv_flags      = 0;
    dev->appearance     = 0;
    dev->tx_power       = FH_BLE_TX_POWER_NONE;
    dev->svc_uuid_count = 0;
    dev->mfg_id         = FH_BLE_MFG_NONE;

    size_t i = 0;
    while (i + 1 < len) {
        uint8_t field_len = data[i];
        if (field_len == 0 || i + 1 + field_len > len) break;
        uint8_t type = data[i + 1];
        const uint8_t *val = &data[i + 2];
        uint8_t vlen = field_len - 1;

        switch (type) {
        case BLE_HS_ADV_TYPE_FLAGS:
            if (vlen >= 1) dev->adv_flags = val[0];
            break;
        case BLE_HS_ADV_TYPE_TX_PWR_LVL:
            if (vlen >= 1) dev->tx_power = (int8_t)val[0];
            break;
        case BLE_HS_ADV_TYPE_APPEARANCE:
            if (vlen >= 2) dev->appearance = (uint16_t)(val[0] | (val[1] << 8));
            break;
        case BLE_HS_ADV_TYPE_INCOMP_UUIDS16:
        case BLE_HS_ADV_TYPE_COMP_UUIDS16:
            // List of 16-bit service UUIDs, little-endian, 2 bytes each.
            for (uint8_t o = 0; o + 1 < vlen && dev->svc_uuid_count < 8; o += 2) {
                dev->svc_uuids[dev->svc_uuid_count++] =
                    (uint16_t)(val[o] | (val[o + 1] << 8));
            }
            break;
        case BLE_HS_ADV_TYPE_MFG_DATA:
            // First 2 bytes of manufacturer-specific data are the company ID.
            if (vlen >= 2) dev->mfg_id = (uint16_t)(val[0] | (val[1] << 8));
            break;
        default:
            break;
        }
        i += 1 + field_len;
    }
}

int gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        const struct ble_gap_disc_desc *d = &event->disc;
        // NimBLE addresses are little-endian (LSB first); store MSB-first so the
        // printed MAC matches the Wi-Fi scanner's BSSID formatting.
        uint8_t addr[6];
        for (int b = 0; b < 6; ++b) addr[b] = d->addr.val[5 - b];

        xSemaphoreTake(guard, portMAX_DELAY);
        int idx = find_working(addr);
        if (idx < 0 && working_count < MAX_DEV) idx = (int)working_count++;
        if (idx >= 0) {
            memcpy(working[idx].addr, addr, 6);
            working[idx].addr_type = d->addr.type;
            working[idx].rssi = d->rssi;
            working[idx].adv_type = d->event_type;
            char name[33];
            extract_name(d->data, d->length_data, name, sizeof(name));
            // Keep an earlier name if this particular advert carried none.
            if (name[0] || working[idx].name[0] == 0) {
                strncpy(working[idx].name, name, sizeof(working[idx].name));
                working[idx].name[sizeof(working[idx].name) - 1] = 0;
            }
            // Parse capability fields (flags/appearance/tx power/service UUIDs/
            // mfg id) from this advertisement for the recon log.
            extract_caps(d->data, d->length_data, &working[idx]);
        }
        xSemaphoreGive(guard);
        return 0;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE:
        xSemaphoreTake(guard, portMAX_DELAY);
        scanning = false;
        xSemaphoreGive(guard);
        return 0;
    default:
        return 0;
    }
}

void start_discovery() {
    struct ble_gap_disc_params p = {};
    p.passive = 1;               // listen only ΓÇö no scan requests, recon-friendly
    p.filter_duplicates = 0;     // we de-dup ourselves and want fresh RSSI
    p.itvl = 0;                  // controller default scan interval
    p.window = 0;                // controller default scan window
    p.limited = 0;
    int rc = ble_gap_disc(own_addr_type, SCAN_WINDOW_MS, &p, gap_event, nullptr);
    xSemaphoreTake(guard, portMAX_DELAY);
    if (rc == 0) { scanning = true; error_state = false; }
    else         { scanning = false; error_state = true; }
    xSemaphoreGive(guard);
    if (rc != 0) ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
}

void scan_task(void *) {
    while (true) {
        bool active, in_flight;
        xSemaphoreTake(guard, portMAX_DELAY);
        active = requested && ready;
        in_flight = scanning;
        xSemaphoreGive(guard);

        if (!active) {
            if (in_flight) ble_gap_disc_cancel();
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (in_flight) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }

        // Start a fresh working table for this window.
        xSemaphoreTake(guard, portMAX_DELAY);
        working_count = 0;
        xSemaphoreGive(guard);

        start_discovery();

        // Wait out the discovery window (callback fills `working`).
        vTaskDelay(pdMS_TO_TICKS(SCAN_WINDOW_MS + 150));

        // Publish the window's result to the snapshot table, and take a PRIVATE
        // copy for the SD log in the SAME locked section. The log write below
        // runs UNLOCKED (SD I/O must not block a UI snapshot read), so it must
        // NOT read the shared `devices[]` — the next loop's memcpy into
        // `devices`, or a concurrent gap_event, would be writing it at the same
        // time. Reading a struct mid-memcpy yields torn fields (e.g. a
        // svc_uuid_count/name out of sync with its data) and walking those off
        // the end corrupts the heap — the TLSF "block must be free" panic.
        // `log_copy` is touched only by this task, so it needs no lock itself.
        static FootHoledBLEDevice log_copy[MAX_DEV];
        xSemaphoreTake(guard, portMAX_DELAY);
        size_t logged = 0;
        if (requested) {
            dev_count = working_count;
            memcpy(devices, working, working_count * sizeof(FootHoledBLEDevice));
            memcpy(log_copy, working, working_count * sizeof(FootHoledBLEDevice));
            ++generation;
            logged = dev_count;
        }
        xSemaphoreGive(guard);
        // Persist from the PRIVATE copy, not the shared snapshot table.
        if (logged > 0) {
            footholed_recon_log_ble(log_copy, logged);
        }

        vTaskDelay(pdMS_TO_TICKS(250)); // brief gap, matches Wi-Fi cadence feel
    }
}

// NimBLE host sync: the controller is ready; infer our address type and mark
// the service usable. Called from the NimBLE host task context.
void on_sync() {
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) { ESP_LOGE(TAG, "ensure_addr failed: %d", rc); return; }
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) { ESP_LOGE(TAG, "infer addr type failed: %d", rc); return; }
    xSemaphoreTake(guard, portMAX_DELAY);
    ready = true;
    xSemaphoreGive(guard);
    ESP_LOGI(TAG, "BLE host synced; scanner ready");
}

void on_reset(int reason) {
    ESP_LOGW(TAG, "BLE host reset; reason=%d", reason);
    xSemaphoreTake(guard, portMAX_DELAY);
    ready = false;
    scanning = false;
    error_state = true;
    xSemaphoreGive(guard);
}

void host_task(void *) {
    nimble_port_run();            // returns only at nimble_port_stop()
    nimble_port_freertos_deinit();
}
} // namespace

bool footholed_ble_init() {
    if (worker) return true;      // already initialised
    guard = xSemaphoreCreateMutex();
    if (!guard) return false;

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", (int)err);
        return false;
    }
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    nimble_port_freertos_init(host_task);

    // Snapshot worker: owns the scan-cycle timing, like the Wi-Fi worker.
    if (xTaskCreate(scan_task, "fh_ble_scan", 4096, nullptr, 4, &worker) != pdPASS) {
        ESP_LOGE(TAG, "failed to create BLE scan worker");
        return false;
    }
    return true;
}

void footholed_ble_start() {
    if (!worker) return;
    xSemaphoreTake(guard, portMAX_DELAY);
    dev_count = 0; working_count = 0; ++generation; error_state = false; requested = true;
    xSemaphoreGive(guard);
}

void footholed_ble_stop() {
    if (!worker) return;
    xSemaphoreTake(guard, portMAX_DELAY);
    requested = false;
    xSemaphoreGive(guard);
    // The worker cancels any in-flight discovery on its next cycle.
}

size_t footholed_ble_snapshot(FootHoledBLEDevice *out, size_t capacity,
                              uint32_t *gen, bool *running, bool *error) {
    if (!worker) { if (gen) *gen = 0; if (running) *running = false; if (error) *error = true; return 0; }
    xSemaphoreTake(guard, portMAX_DELAY);
    size_t n = dev_count < capacity ? dev_count : capacity;
    if (out && n) memcpy(out, devices, n * sizeof(FootHoledBLEDevice));
    if (gen) *gen = generation;
    if (running) *running = requested;
    if (error) *error = error_state;
    xSemaphoreGive(guard);
    return n;
}
