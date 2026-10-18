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
#include <stdio.h>
#include <string.h>

namespace {
const char *TAG = "FootHoled/log";

// Mount point and log path on the FAT32 card.
constexpr const char *MOUNT_POINT = "/sdcard";
constexpr const char *LOG_PATH    = "/sdcard/recon_log.csv";
constexpr const char *CSV_HEADER  =
    "uptime_s,type,identifier,mac,addr_type,channel,rssi,auth_or_advtype\n";

// ---------------------------------------------------------------------------
// TODO(HW): SDMMC pins for the onboard microSD slot (1-bit mode).
// These are the documented ESP32-S3-Touch-LCD-3.5*B* values (CLK 11 / CMD 10 /
// D0 9). docs/H0.1_Pin_Map.md warns these must NOT be assumed identical on the
// non-B board — confirm against the schematic and edit HERE only.
// ---------------------------------------------------------------------------
constexpr int FH_PIN_SD_CLK = 11;
constexpr int FH_PIN_SD_CMD = 10;
constexpr int FH_PIN_SD_D0  = 9;

sdmmc_card_t *s_card = nullptr;
bool s_available = false;
SemaphoreHandle_t s_guard = nullptr;

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
    sdmmc_slot_config_t slot_cfg = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_cfg.width = 1;                 // 1-bit mode: only CLK/CMD/D0 needed
    slot_cfg.clk = (gpio_num_t)FH_PIN_SD_CLK;
    slot_cfg.cmd = (gpio_num_t)FH_PIN_SD_CMD;
    slot_cfg.d0  = (gpio_num_t)FH_PIN_SD_D0;

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
            esp_vfs_fat_sdmmc_unmount();
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
