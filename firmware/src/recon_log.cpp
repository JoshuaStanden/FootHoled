/**
 * @file recon_log.cpp
 * @brief SD-card CSV logger for Wi-Fi / BLE recon sightings. See recon_log.h.
 *
 * Design mirrors the recon services' house style: an opaque module with a
 * mutex, graceful degradation when hardware is absent, and TODO(HW) markers
 * where real hardware is required to confirm behaviour.
 *
 * Mount: ESP-IDF SDMMC host in 1-bit mode (onboard hard-wired slot). FatFs
 * VFS mounts the FAT32 card at /sdcard. Writes are flushed per completed scan
 * so an unclean power-off loses at most the last in-progress scan's rows.
 *
 * TODO(HW): not compiled or flashed on this host (no ESP-IDF toolchain). The
 * SDMMC GPIOs below are the documented 3.5B values and are NOT verified for
 * this board revision; confirm CLK/CMD/D0 from the schematic.
 */
#include "recon_log.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <atomic>

namespace {
const char *TAG = "FootHoled/log";

// Mount point and log path on the FAT32 card.
constexpr const char *MOUNT_POINT = "/sdcard";
constexpr const char *LOG_PATH    = "/sdcard/recon_log.csv";
constexpr const char *CSV_HEADER  =
    "uptime_s,type,identifier,mac,addr_type,channel,rssi,auth_or_advtype\n";

// ---------------------------------------------------------------------------
// SDMMC pins for the onboard microSD slot (1-bit mode), VERIFIED against the
// ESP32-S3-Touch-LCD-3.5 (non-B) Rev2.0 schematic GPIO table:
//   IO11 = SD_CLK, IO10 = SD_CMD, IO9 = SD_D0. (SD_CD/card-detect is on the
//   TCA/CH422 I/O expander as EXIO3 — not wired to a GPIO, so not used here.)
// These lines have no strong external pull-ups on this board, so internal
// pull-ups MUST be enabled in the slot config below or the card never ACKs
// (send_op_cond times out with 0x107 / ESP_ERR_TIMEOUT).
// ---------------------------------------------------------------------------
constexpr int FH_PIN_SD_CLK = 11;
constexpr int FH_PIN_SD_CMD = 10;
constexpr int FH_PIN_SD_D0  = 9;

sdmmc_card_t *s_card = nullptr;
bool s_available = false;
SemaphoreHandle_t s_guard = nullptr;

// ---------------------------------------------------------------------------
// Async format state. The format erases the whole card and can take many
// seconds; running it on the LVGL/UI thread freezes the display. Instead a
// dedicated task does the blocking work and publishes a phase + percent that
// the UI polls (lock-free, single writer = the format task, readers = UI).
// ---------------------------------------------------------------------------
std::atomic<int> s_fmt_phase{0};   // fh_recon_fmt_phase_t
std::atomic<int> s_fmt_pct{0};     // 0..100 (coarse, phase-driven)
TaskHandle_t     s_fmt_task = nullptr;

// Format a 6-byte address MSB-first as AA:BB:CC:DD:EE:FF.
void format_mac(const uint8_t *mac, char *out, size_t out_sz) {
    snprintf(out, out_sz, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// CSV-escape an identifier (SSID / BLE name). RFC-4180 style: wrap in quotes
// and double any embedded quote, so commas/quotes in an SSID cannot break the
// column layout. A control char (newline/CR) is replaced with a space.
void write_csv_field(FILE *f, const char *s) {
    fputc('"', f);
    for (const char *p = s; *p; ++p) {
        char c = *p;
        if (c == '"') { fputc('"', f); fputc('"', f); }
        else if (c == '\n' || c == '\r') { fputc(' ', f); }
        else { fputc(c, f); }
    }
    fputc('"', f);
}

uint32_t uptime_seconds() {
    return (uint32_t)(esp_log_timestamp() / 1000); // ms since boot -> seconds
}
} // namespace

bool footholed_recon_log_init() {
    if (s_available) return true;
    if (!s_guard) {
        s_guard = xSemaphoreCreateMutex();
        if (!s_guard) return false;
    }

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {};
    mount_cfg.format_if_mount_failed = false; // never auto-format the user's card
    mount_cfg.max_files = 2;
    mount_cfg.allocation_unit_size = 16 * 1024;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    // Start the handshake slow; this board's SD lines are marginal without
    // strong external pull-ups, and a conservative probe clock improves
    // first-contact reliability. FatFs bumps to the card's real speed after.
    host.max_freq_khz = SDMMC_FREQ_PROBING;   // 400 kHz for init

    sdmmc_slot_config_t slot_cfg = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_cfg.width = 1;                 // 1-bit mode: only CLK/CMD/D0 needed
    slot_cfg.clk = (gpio_num_t)FH_PIN_SD_CLK;
    slot_cfg.cmd = (gpio_num_t)FH_PIN_SD_CMD;
    slot_cfg.d0  = (gpio_num_t)FH_PIN_SD_D0;
    // CMD and D0 need pull-ups; this board has none strong enough externally,
    // so enable the ESP32's internal pull-ups. Without this send_op_cond times
    // out (0x107) and the card is never detected.
    slot_cfg.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_err_t err = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_cfg,
                                            &mount_cfg, &s_card);
    if (err != ESP_OK) {
        // No card / unformatted / wrong pins: degrade gracefully, do not crash.
        ESP_LOGW(TAG, "SD mount failed (%s). Recon log disabled; card must be "
                      "FAT32 and SDMMC pins verified.", esp_err_to_name(err));
        s_available = false;
        return false;
    }

    // Create the file with a header row if it does not exist yet; otherwise
    // append (running log across power cycles).
    FILE *f = fopen(LOG_PATH, "r");
    bool exists = (f != nullptr);
    if (f) fclose(f);
    if (!exists) {
        f = fopen(LOG_PATH, "w");
        if (!f) {
            ESP_LOGE(TAG, "cannot create %s", LOG_PATH);
            // ESP-IDF 6.1 removed the arg-less esp_vfs_fat_sdmmc_unmount();
            // use the handle-based unmount with the mount point and card.
            esp_vfs_fat_sdcard_unmount(MOUNT_POINT, s_card);
            s_card = nullptr;
            s_available = false;
            return false;
        }
        fputs(CSV_HEADER, f);
        fclose(f);
    }

    s_available = true;
    ESP_LOGI(TAG, "recon log ready at %s (card %s)", LOG_PATH,
             s_card && s_card->cid.name[0] ? s_card->cid.name : "?");
    return true;
}

bool footholed_recon_log_available() { return s_available; }

void footholed_recon_log_wifi(const FootHoledAP *aps, size_t count) {
    if (!s_available || !aps || count == 0) return;
    xSemaphoreTake(s_guard, portMAX_DELAY);
    FILE *f = fopen(LOG_PATH, "a");
    if (f) {
        uint32_t t = uptime_seconds();
        char mac[18];
        for (size_t i = 0; i < count; ++i) {
            format_mac(aps[i].bssid, mac, sizeof(mac));
            // uptime,type,identifier(SSID),mac,addr_type(blank),channel,rssi,authmode
            fprintf(f, "%u,wifi,", (unsigned)t);
            write_csv_field(f, aps[i].ssid);
            fprintf(f, ",%s,,%u,%d,%u\n",
                    mac, (unsigned)aps[i].channel, (int)aps[i].rssi,
                    (unsigned)aps[i].authmode);
        }
        fclose(f); // flush per completed scan
    } else {
        ESP_LOGW(TAG, "wifi log append failed");
    }
    xSemaphoreGive(s_guard);
}

void footholed_recon_log_ble(const FootHoledBLEDevice *devs, size_t count) {
    if (!s_available || !devs || count == 0) return;
    xSemaphoreTake(s_guard, portMAX_DELAY);
    FILE *f = fopen(LOG_PATH, "a");
    if (f) {
        uint32_t t = uptime_seconds();
        char mac[18];
        for (size_t i = 0; i < count; ++i) {
            format_mac(devs[i].addr, mac, sizeof(mac));
            // uptime,type,identifier(name),mac,addr_type,channel(blank),rssi,adv_type
            fprintf(f, "%u,ble,", (unsigned)t);
            write_csv_field(f, devs[i].name);
            fprintf(f, ",%s,%u,,%d,%u\n",
                    mac, (unsigned)devs[i].addr_type, (int)devs[i].rssi,
                    (unsigned)devs[i].adv_type);
        }
        fclose(f);
    } else {
        ESP_LOGW(TAG, "ble log append failed");
    }
    xSemaphoreGive(s_guard);
}

bool footholed_recon_log_format(void) {
    // Caller (sd_format service / UI) is responsible for user confirmation.
    // Here we just do the destructive work on the already-mounted card.
    if (!s_available || !s_card) {
        ESP_LOGW(TAG, "format requested but no card mounted");
        return false;
    }

    xSemaphoreTake(s_guard, portMAX_DELAY);

    // esp_vfs_fat_sdcard_format() reuses the existing mount: it erases the card
    // and lays down a fresh FAT filesystem, leaving it mounted at MOUNT_POINT.
    // Every file (including the old recon_log.csv) is permanently destroyed.
    esp_err_t err = esp_vfs_fat_sdcard_format(MOUNT_POINT, s_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_vfs_fat_sdcard_format failed: %s", esp_err_to_name(err));
        // Card state is undefined after a failed format; mark unavailable so
        // callers do not assume a usable log, and let a reboot re-mount cleanly.
        s_available = false;
        xSemaphoreGive(s_guard);
        return false;
    }

    // Recreate a fresh, empty log file with just the header row so recon
    // logging continues to work immediately after the wipe.
    FILE *f = fopen(LOG_PATH, "w");
    if (!f) {
        ESP_LOGE(TAG, "format OK but cannot recreate %s", LOG_PATH);
        s_available = false;
        xSemaphoreGive(s_guard);
        return false;
    }
    fputs(CSV_HEADER, f);
    fclose(f);

    s_available = true;
    ESP_LOGI(TAG, "SD card formatted; fresh empty log at %s", LOG_PATH);
    xSemaphoreGive(s_guard);
    return true;
}

// ---------------------------------------------------------------------------
// Async format implementation.
//
// esp_vfs_fat_sdcard_format() has no progress callback, so the percent is
// phase-driven: we advance it at the start of each stage. It reflects REAL
// progress through the stages (mount check -> erase/format -> recreate log ->
// done), just not byte-accurate within the long erase stage. The UI shows an
// indeterminate-style bar while the erase runs so it never looks stalled.
// ---------------------------------------------------------------------------
static void format_task(void *)
{
    s_fmt_pct.store(5, std::memory_order_relaxed);
    s_fmt_phase.store(FH_RECON_FMT_RUNNING, std::memory_order_release);

    // Guard against no card (shouldn't happen — start() checks — but be safe).
    if (!s_available || !s_card) {
        s_fmt_phase.store(FH_RECON_FMT_DONE_NOCARD, std::memory_order_release);
        s_fmt_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    s_fmt_pct.store(10, std::memory_order_relaxed);
    ESP_LOGW(TAG, "DESTRUCTIVE: async formatting SD card (all data erased)");

    // Stop the Wi-Fi/BLE scanner log writers from touching the card DURING the
    // format. They check s_available before taking s_guard, so clearing it here
    // makes them no-op instead of contending for the lock / the FAT volume
    // while esp_vfs_fat_sdcard_format() unmounts and rebuilds it. Without this
    // the UI appeared to freeze under scanner contention.
    s_available = false;

    xSemaphoreTake(s_guard, portMAX_DELAY);
    // Format at the card's FULL capacity: allocation_unit_size = 0 lets f_mkfs()
    // pick the cluster size from the real card size. A fixed 16 KB unit (the
    // mount default) produced a FAT that under-reported capacity (e.g. "16 GB"
    // on a 32 GB card). use_one_fat=false keeps the redundant second FAT.
    esp_vfs_fat_mount_config_t fmt_cfg = {};
    fmt_cfg.format_if_mount_failed = true;
    fmt_cfg.max_files              = 2;
    fmt_cfg.allocation_unit_size   = 0;   // auto-size for full capacity
    // The long stage: full erase + fresh FAT. Blocks this TASK, not the UI.
    esp_err_t err = esp_vfs_fat_sdcard_format_cfg(MOUNT_POINT, s_card, &fmt_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "async format failed: %s", esp_err_to_name(err));
        s_available = false;
        xSemaphoreGive(s_guard);
        s_fmt_pct.store(100, std::memory_order_relaxed);
        s_fmt_phase.store(FH_RECON_FMT_DONE_FAIL, std::memory_order_release);
        s_fmt_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    s_fmt_pct.store(85, std::memory_order_relaxed);
    // Recreate the fresh empty log (header row only).
    FILE *f = fopen(LOG_PATH, "w");
    if (!f) {
        ESP_LOGE(TAG, "async format OK but cannot recreate %s", LOG_PATH);
        s_available = false;
        xSemaphoreGive(s_guard);
        s_fmt_pct.store(100, std::memory_order_relaxed);
        s_fmt_phase.store(FH_RECON_FMT_DONE_FAIL, std::memory_order_release);
        s_fmt_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }
    fputs(CSV_HEADER, f);
    fclose(f);

    s_available = true;
    xSemaphoreGive(s_guard);
    ESP_LOGI(TAG, "async SD format complete; fresh empty log at %s", LOG_PATH);
    s_fmt_pct.store(100, std::memory_order_relaxed);
    s_fmt_phase.store(FH_RECON_FMT_DONE_OK, std::memory_order_release);
    s_fmt_task = nullptr;
    vTaskDelete(nullptr);
}

bool footholed_recon_log_format_start(void)
{
    // Already running? Refuse to start a second.
    if (s_fmt_phase.load(std::memory_order_acquire) == FH_RECON_FMT_RUNNING) {
        return false;
    }
    if (!s_available || !s_card) {
        s_fmt_pct.store(0, std::memory_order_relaxed);
        s_fmt_phase.store(FH_RECON_FMT_DONE_NOCARD, std::memory_order_release);
        return false;
    }
    s_fmt_pct.store(0, std::memory_order_relaxed);
    s_fmt_phase.store(FH_RECON_FMT_RUNNING, std::memory_order_release);
    // 4 KB stack is plenty for the format call; priority below the UI so the
    // display task keeps repainting the progress bar smoothly.
    BaseType_t ok = xTaskCreate(format_task, "sd_format", 4096, nullptr,
                                3, &s_fmt_task);
    if (ok != pdPASS) {
        s_fmt_task = nullptr;
        s_fmt_phase.store(FH_RECON_FMT_DONE_FAIL, std::memory_order_release);
        return false;
    }
    return true;
}

fh_recon_fmt_phase_t footholed_recon_log_format_poll(int *out_percent)
{
    if (out_percent) *out_percent = s_fmt_pct.load(std::memory_order_relaxed);
    return (fh_recon_fmt_phase_t)s_fmt_phase.load(std::memory_order_acquire);
}
