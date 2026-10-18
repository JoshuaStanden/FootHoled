/**
 * @file sd_format.cpp
 * @brief Implements the destructive SD-card format service. See sd_format.h.
 *
 * The onboard microSD card is mounted and owned by recon_log.cpp (it holds the
 * sdmmc_card_t handle and the log path). To avoid duplicating SD ownership or
 * exposing that handle, the actual format is delegated to
 * footholed_recon_log_format(), which formats the mounted card and recreates a
 * fresh empty /sdcard/recon_log.csv. This file is the thin "service" the UI
 * calls, mapping the outcome to a result code + display text.
 */
#include "sd_format.h"
#include "recon_log.h"
#include "esp_log.h"

namespace {
const char *TAG = "FootHoled/sdfmt";
}

fh_sd_format_result_t footholed_sd_format(void) {
    // No card mounted -> nothing to format. Report so the UI can warn.
    if (!footholed_recon_log_available()) {
        ESP_LOGW(TAG, "format requested but no SD card is mounted");
        return FH_SD_FORMAT_NO_CARD;
    }

    ESP_LOGW(TAG, "DESTRUCTIVE: formatting SD card (all data will be erased)");
    // Delegates to the SD owner: formats the mounted card, then recreates an
    // empty recon_log.csv (header row only). Returns false on failure.
    if (!footholed_recon_log_format()) {
        ESP_LOGE(TAG, "SD format failed");
        return FH_SD_FORMAT_FAILED;
    }

    ESP_LOGI(TAG, "SD card formatted; fresh empty recon log recreated");
    return FH_SD_FORMAT_OK;
}

const char *footholed_sd_format_result_text(fh_sd_format_result_t r) {
    switch (r) {
        case FH_SD_FORMAT_OK:      return "SD card formatted. Fresh empty log created.";
        case FH_SD_FORMAT_NO_CARD: return "No SD card detected. Nothing was changed.";
        case FH_SD_FORMAT_FAILED:  return "Format FAILED. Card may need manual reformat.";
        default:                   return "Unknown result.";
    }
}
