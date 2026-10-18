#pragma once
#include <stdbool.h>

/**
 * @file sd_format.h
 * @brief Destructive "Format SD Card" service for the onboard microSD slot.
 *
 * This performs a FULL, IRREVERSIBLE format of the onboard microSD card:
 * every file on the card is permanently erased and a fresh FAT filesystem is
 * written. After a successful format a new, empty recon log
 * (/sdcard/recon_log.csv, header row only) is recreated so recon logging keeps
 * working. THIS CANNOT BE UNDONE.
 *
 * The UI MUST gate this behind an explicit, two-stage confirmation (see the
 * Storage screen in ui.cpp). This service itself does NO confirmation — it
 * assumes the caller already got the user's informed consent.
 *
 * Format filesystem: FAT (FAT32 / exFAT per card size) — the only filesystems
 * ESP-IDF's FatFs supports. See recon_log.h for the format/size rules.
 *
 * TODO(HW): not compiled or flashed on this host (no ESP-IDF toolchain). The
 * SDMMC mount it relies on (recon_log.cpp) uses board-rev-unverified GPIOs —
 * a real format must be tested on a THROWAWAY card first.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FH_SD_FORMAT_OK = 0,       // card formatted and fresh log recreated
    FH_SD_FORMAT_NO_CARD,      // no SD card detected / not mounted
    FH_SD_FORMAT_FAILED,       // the format operation itself failed
} fh_sd_format_result_t;

/**
 * @brief Destructively format the onboard microSD card, then recreate an empty
 *        recon log. Caller MUST have obtained explicit user confirmation first.
 *
 * @return FH_SD_FORMAT_OK       success (card wiped, empty log recreated)
 *         FH_SD_FORMAT_NO_CARD  no card present / not mounted — nothing done
 *         FH_SD_FORMAT_FAILED   the format failed — card state is undefined
 */
fh_sd_format_result_t footholed_sd_format(void);

/** @brief Human-readable text for a format result (for on-screen reporting). */
const char *footholed_sd_format_result_text(fh_sd_format_result_t r);

#ifdef __cplusplus
}
#endif
