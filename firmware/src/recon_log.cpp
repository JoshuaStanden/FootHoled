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
#include <stdlib.h>
#include <atomic>

namespace {
const char *TAG = "FootHoled/log";

// Mount point and log path on the FAT32 card.
constexpr const char *MOUNT_POINT = "/sdcard";
constexpr const char *LOG_PATH    = "/sdcard/recon_log.csv";
constexpr const char *CSV_HEADER  =
    "uptime_s,type,identifier,mac,addr_type,channel,rssi,auth_or_advtype,"
    "ble_flags,ble_appearance,ble_tx_power,ble_svc_uuids,ble_mfg_id\n";

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

// Card presence independent of filesystem mount (for Option-A field reclaim).
std::atomic<int>      s_card_status{0};  // fh_sd_card_status_t
std::atomic<uint64_t> s_card_bytes{0};   // capacity from CSD, valid if present

// Build the SDMMC host + slot config used for both mount and standalone probe,
// so the two paths can never drift. Defined once here.
static void sd_make_host_slot(sdmmc_host_t *host, sdmmc_slot_config_t *slot) {
    *host = SDMMC_HOST_DEFAULT();
    // Run at the normal 20 MHz default. An earlier revision pinned this to
    // SDMMC_FREQ_PROBING (400 kHz) as a belt-and-suspenders for first-contact
    // on this board's marginal SD lines — but that 400 kHz cap applied to ALL
    // I/O, making an 8 GB format take MANY MINUTES. Reliability on this board
    // comes from the internal pull-ups (below); the init handshake always
    // starts slow internally and negotiates up, so DEFAULT is safe here and
    // makes format + logging run at full speed.
    host->max_freq_khz = SDMMC_FREQ_DEFAULT;   // 20 MHz (was 400 kHz probe)
    *slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot->width = 1;                            // 1-bit: CLK/CMD/D0 only
    slot->clk = (gpio_num_t)FH_PIN_SD_CLK;
    slot->cmd = (gpio_num_t)FH_PIN_SD_CMD;
    slot->d0  = (gpio_num_t)FH_PIN_SD_D0;
    slot->flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;  // board has no strong ext.
}

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

    sdmmc_host_t host;
    sdmmc_slot_config_t slot_cfg;
    sd_make_host_slot(&host, &slot_cfg);

    esp_err_t err = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_cfg,
                                            &mount_cfg, &s_card);
    if (err != ESP_OK) {
        // Mount failed. Distinguish "no card" from "card present but its
        // filesystem is unreadable" (exFAT/SDXC/raw) — the latter can be
        // reclaimed by Format without a PC. When the CARD initialised but only
        // the FAT mount failed (e.g. ESP_FAIL on an exFAT card), the mount
        // helper still populated s_card with the card info, so we read capacity
        // from it. A null s_card means the card never initialised = no card.
        if (s_card) {
            uint64_t bytes = ((uint64_t)s_card->csd.capacity) * s_card->csd.sector_size;
            s_card_bytes.store(bytes, std::memory_order_relaxed);
            s_card_status.store(FH_SD_CARD_UNREADABLE, std::memory_order_release);
            ESP_LOGW(TAG, "SD card present (%.1f GB) but filesystem unreadable "
                          "(%s). Use Format to reclaim it.",
                     bytes / 1e9, esp_err_to_name(err));
        } else {
            s_card_bytes.store(0, std::memory_order_relaxed);
            s_card_status.store(FH_SD_CARD_NONE, std::memory_order_release);
            ESP_LOGW(TAG, "No SD card detected (%s).", esp_err_to_name(err));
        }
        s_available = false;
        return false;
    }

    // Mounted OK — record capacity + ready status for the UI.
    if (s_card) {
        uint64_t bytes = ((uint64_t)s_card->csd.capacity) * s_card->csd.sector_size;
        s_card_bytes.store(bytes, std::memory_order_relaxed);
    }
    s_card_status.store(FH_SD_CARD_READY, std::memory_order_release);

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
            // uptime,type,identifier(SSID),mac,addr_type(blank),channel,rssi,authmode,
            // then 5 blank BLE-capability columns so wifi/ble rows stay aligned.
            fprintf(f, "%u,wifi,", (unsigned)t);
            write_csv_field(f, aps[i].ssid);
            fprintf(f, ",%s,,%u,%d,%u,,,,,\n",
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
            // uptime,type,identifier(name),mac,addr_type,channel(blank),rssi,adv_type,
            // then capability columns: flags,appearance,tx_power,svc_uuids,mfg_id
            fprintf(f, "%u,ble,", (unsigned)t);
            write_csv_field(f, devs[i].name);
            fprintf(f, ",%s,%u,,%d,%u,",
                    mac, (unsigned)devs[i].addr_type, (int)devs[i].rssi,
                    (unsigned)devs[i].adv_type);
            // flags (hex), appearance (hex), tx_power (dBm or blank if absent)
            fprintf(f, "0x%02X,0x%04X,", devs[i].adv_flags, devs[i].appearance);
            if (devs[i].tx_power == FH_BLE_TX_POWER_NONE) fprintf(f, ",");
            else fprintf(f, "%d,", (int)devs[i].tx_power);
            // svc_uuids: ';'-joined 16-bit UUIDs in one quoted field (so the
            // internal separator never breaks the CSV column layout).
            {
                char uuids[8 * 7 + 1];  // up to 8 * "0xXXXX;" + NUL
                size_t p = 0;
                for (uint8_t u = 0; u < devs[i].svc_uuid_count && p + 7 < sizeof(uuids); ++u) {
                    p += snprintf(uuids + p, sizeof(uuids) - p, "%s0x%04X",
                                  u ? ";" : "", devs[i].svc_uuids[u]);
                }
                uuids[p] = 0;
                write_csv_field(f, uuids);
            }
            // mfg_id (hex) or blank if none
            if (devs[i].mfg_id == FH_BLE_MFG_NONE) fprintf(f, ",\n");
            else fprintf(f, ",0x%04X\n", devs[i].mfg_id);
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
    int status = s_card_status.load(std::memory_order_acquire);
    if (status == FH_SD_CARD_NONE || !s_card) {
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
    // while the format unmounts and rebuilds it. Without this the UI froze.
    s_available = false;

    xSemaphoreTake(s_guard, portMAX_DELAY);

    esp_err_t err;
    // Cluster size matters a LOT for format SPEED: with allocation_unit_size=0
    // f_mkfs auto-picked 512-byte clusters on this card, producing a huge FAT
    // and a multi-MINUTE format (confirmed by the ESP-IDF maintainer: a long
    // f_mkfs means too-small a cluster). Use 32 KB — a normal FAT32 cluster for
    // 8–32 GB cards — so the FAT is small and the format finishes in seconds.
    // (32 KB is also fine for this logger's CSV write pattern.)
    esp_vfs_fat_mount_config_t fmt_cfg = {};
    fmt_cfg.format_if_mount_failed = true;   // reclaim an unreadable/exFAT card
    fmt_cfg.max_files              = 2;
    fmt_cfg.allocation_unit_size   = 32 * 1024;  // 32 KB clusters (fast f_mkfs)

    if (status == FH_SD_CARD_READY) {
        // Already mounted via the VFS helper — format in place.
        err = esp_vfs_fat_sdcard_format_cfg(MOUNT_POINT, s_card, &fmt_cfg);
    } else {
        // UNREADABLE (exFAT/SDXC/raw): s_card is the VFS mount helper's handle
        // from the failed mount at init (card initialised, FS unmountable). The
        // in-place format helper needs a cleanly (re)mounted volume, so release
        // this handle via the VFS unmount, then mount WITH
        // format_if_mount_failed=true — that re-inits the card and lays down a
        // fresh FAT32 when the existing FS won't mount, reclaiming it to full
        // capacity, no PC needed.
        esp_vfs_fat_sdcard_unmount(MOUNT_POINT, s_card);
        s_card = nullptr;
        sdmmc_host_t host;
        sdmmc_slot_config_t slot;
        sd_make_host_slot(&host, &slot);
        err = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot, &fmt_cfg, &s_card);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "async format failed: %s", esp_err_to_name(err));
        s_available = false;
        s_card_status.store(s_card ? FH_SD_CARD_UNREADABLE : FH_SD_CARD_NONE,
                            std::memory_order_release);
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
    // Refresh capacity + mark ready (card is now mounted FAT32 at full size).
    if (s_card) {
        uint64_t bytes = ((uint64_t)s_card->csd.capacity) * s_card->csd.sector_size;
        s_card_bytes.store(bytes, std::memory_order_relaxed);
    }
    s_card_status.store(FH_SD_CARD_READY, std::memory_order_release);
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
    // Need a card PRESENT (ready or unreadable). Only true "no card" refuses.
    int status = s_card_status.load(std::memory_order_acquire);
    if (status == FH_SD_CARD_NONE || !s_card) {
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

fh_sd_card_status_t footholed_recon_log_card_status(void)
{
    return (fh_sd_card_status_t)s_card_status.load(std::memory_order_acquire);
}

uint64_t footholed_recon_log_card_bytes(void)
{
    return s_card_bytes.load(std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Generic locked append helper.
//
// recon_log.cpp owns the SD card (the mount handle and the s_guard mutex), so
// any OTHER module that needs to write a file on the card must go through here
// rather than opening the card independently — two unsynchronised writers to
// the same FAT volume can corrupt it. This appends `line` to `path`, creating
// the file with `header` first if it does not exist yet, all under s_guard.
// Used by boot_log.cpp for /sdcard/boot_log.csv. No-ops (returns false) when
// the card is unavailable, so callers degrade gracefully.
bool footholed_recon_log_append_line(const char *path, const char *header,
                                     const char *line)
{
    if (!s_available || !s_guard || !path || !line) return false;
    xSemaphoreTake(s_guard, portMAX_DELAY);
    FILE *probe = fopen(path, "r");
    bool exists = (probe != nullptr);
    if (probe) fclose(probe);

    bool ok = false;
    FILE *f = fopen(path, "a");
    if (f) {
        if (!exists && header) fputs(header, f);
        fputs(line, f);
        fclose(f);
        ok = true;
    } else {
        ESP_LOGW(TAG, "append to %s failed", path);
    }
    xSemaphoreGive(s_guard);
    return ok;
}
