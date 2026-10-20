#include "wifi_scan.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "recon_log.h"
#include <string.h>

namespace {
constexpr size_t MAX_AP = 48;
FootHoledAP aps[MAX_AP] = {};
size_t ap_count = 0;
uint32_t generation = 0;
bool requested = false;
bool ready = false;
bool started = false;        // radio currently started (esp_wifi_start done)
bool error_state = false;
SemaphoreHandle_t guard = nullptr;
TaskHandle_t worker = nullptr;
const char *TAG = "FootHoled/wifi";

void scan_task(void *) {
    while (true) {
        bool active;
        xSemaphoreTake(guard, portMAX_DELAY);
        active = requested && started;   // only scan while the radio is up
        xSemaphoreGive(guard);
        if (!active) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        wifi_scan_config_t config = {};
        config.show_hidden = true;
        config.scan_type = WIFI_SCAN_TYPE_ACTIVE; // standard discovery probe requests, no association
        config.scan_time.active.min = 60;
        config.scan_time.active.max = 120;
        esp_err_t err = esp_wifi_scan_start(&config, true); // block only this worker task
        // Wi-Fi AP records are too large for this task's stack. Allocate on heap.
        wifi_ap_record_t *records = static_cast<wifi_ap_record_t *>(
            heap_caps_calloc(MAX_AP, sizeof(wifi_ap_record_t), MALLOC_CAP_8BIT));
        uint16_t count = MAX_AP;
        if (!records) {
            ESP_LOGE(TAG, "Unable to allocate Wi-Fi scan records");
            err = ESP_ERR_NO_MEM;
        } else if (err == ESP_OK) {
            err = esp_wifi_scan_get_ap_records(&count, records);
        }
        // Release the driver's INTERNAL scan-result list every cycle. In a
        // beacon-spam environment (thousands of APs) the driver buffers the
        // full result set internally; reading only MAX_AP of them can leave the
        // rest allocated, and that internal RAM accumulates cycle over cycle
        // until an allocation fails and the firmware panic-reboots. Clearing it
        // unconditionally after each scan bounds the driver's own footprint.
        esp_wifi_clear_ap_list();
        // Defensive: the API clamps `count` to the buffer (MAX_AP), but never
        // let it exceed MAX_AP — the loop below indexes aps[MAX_AP].
        if (count > MAX_AP) count = MAX_AP;
        xSemaphoreTake(guard, portMAX_DELAY);
        size_t log_n = 0;
        static FootHoledAP wifi_log_copy[MAX_AP];
        if (requested) {
            error_state = err != ESP_OK;
            if (err == ESP_OK) {
                ap_count = count;
                for (size_t i=0; i<count; ++i) {
                    memcpy(aps[i].ssid, records[i].ssid, 32);
                    aps[i].ssid[32] = 0;
                    memcpy(aps[i].bssid, records[i].bssid, 6);
                    aps[i].rssi = records[i].rssi;
                    aps[i].channel = records[i].primary;
                    aps[i].authmode = (uint8_t)records[i].authmode;
                }
                ++generation;
                // Private copy for the UNLOCKED SD log below, so it never reads
                // the shared aps[] while the next scan cycle rewrites it (same
                // torn-read/heap-corruption hazard as the BLE path).
                memcpy(wifi_log_copy, aps, count * sizeof(FootHoledAP));
                log_n = count;
            }
        }
        xSemaphoreGive(guard);
        // Persist from the PRIVATE copy, not the shared snapshot table.
        // No-ops safely if no card is mounted. Done after releasing the guard
        // so SD I/O never blocks a UI snapshot read.
        if (log_n > 0) {
            footholed_recon_log_wifi(wifi_log_copy, log_n);
        }
        heap_caps_free(records);
        vTaskDelay(pdMS_TO_TICKS(2500));
    }
}
}

bool footholed_wifi_init() {
    if (ready) return true;
    guard = xSemaphoreCreateMutex();
    if (!guard) return false;
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;
    if (!esp_netif_create_default_wifi_sta()) return false;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) { ESP_LOGE(TAG,"WiFi init: %s",esp_err_to_name(err)); return false; }
    esp_wifi_set_mode(WIFI_MODE_STA);
    // NOTE: the radio is NOT started here. footholed_wifi_start() calls
    // esp_wifi_start() on screen-entry and footholed_wifi_stop() calls
    // esp_wifi_stop() on screen-leave, so the Wi-Fi station control block and
    // its RX/TX radio buffers are only resident while the Wi-Fi screen is open.
    // This frees that internal RAM for NimBLE while the user is on the BLE
    // screen — the two radio stacks co-residing was exhausting internal RAM in
    // dense environments and causing a spontaneous reboot during BLE scanning.
    started = false;
    ready = true;
    if (xTaskCreate(scan_task, "fh_wifi_scan", 6144, nullptr, 4, &worker) != pdPASS) {
        ready = false;
        return false;
    }
    return true;
}
void footholed_wifi_start() {
    if (!ready) return;
    // Bring the radio up (idempotent) before marking scanning requested.
    if (!started) {
        esp_err_t err = esp_wifi_start();
        if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STOPPED) {
            ESP_LOGE(TAG, "WiFi start: %s", esp_err_to_name(err));
        } else {
            xSemaphoreTake(guard, portMAX_DELAY);
            started = true;
            xSemaphoreGive(guard);
        }
    }
    xSemaphoreTake(guard, portMAX_DELAY);
    ap_count=0; ++generation; error_state=false; requested=true;
    xSemaphoreGive(guard);
}
void footholed_wifi_stop() {
    if (!ready) return;
    xSemaphoreTake(guard, portMAX_DELAY);
    requested=false;
    bool was_started = started;
    started = false;              // task stops scanning before the radio drops
    xSemaphoreGive(guard);
    // Power the radio down so its station control block and RX/TX buffers are
    // freed for BLE. esp_wifi_stop() aborts any in-flight scan. The worker task
    // stays alive but idles (requested=false) until footholed_wifi_start() is
    // called again on the next screen-entry, which re-runs esp_wifi_start().
    if (was_started) {
        esp_err_t err = esp_wifi_stop();
        if (err != ESP_OK) ESP_LOGW(TAG, "WiFi stop: %s", esp_err_to_name(err));
    }
}
size_t footholed_wifi_snapshot(FootHoledAP *out, size_t capacity, uint32_t *gen, bool *running, bool *error) {
    if (!ready) { if(gen)*gen=0; if(running)*running=false; if(error)*error=true; return 0; }
    xSemaphoreTake(guard, portMAX_DELAY);
    size_t n = ap_count < capacity ? ap_count : capacity;
    if (out && n) memcpy(out, aps, n*sizeof(FootHoledAP));
    if (gen) *gen=generation;
    if (running) *running=requested;
    if (error) *error=error_state;
    xSemaphoreGive(guard);
    return n;
}
