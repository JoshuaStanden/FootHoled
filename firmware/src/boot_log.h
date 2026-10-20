#pragma once
#include <stdint.h>

/**
 * @file boot_log.h
 * @brief Boot / reset-reason diagnostics log (/sdcard/boot_log.csv).
 *
 * Records, once per boot, WHY the device last reset plus startup heap figures,
 * so random reboots in the field can be diagnosed from the SD card afterward.
 *
 * Why the record for a reboot is written by the NEXT boot: a crash cannot write
 * anything itself (RAM is gone and the chip resets immediately). The hardware
 * instead stamps the reset reason into an always-on register that SURVIVES the
 * reset; this module reads it back on the following boot and logs it. Over a
 * session the file builds a complete reboot history.
 *
 * SD ownership stays with recon_log.cpp — this module does NOT touch the card
 * directly. It formats one CSV row and hands it to
 * footholed_recon_log_append_line(), which performs the write under the SD
 * mutex (so there is never a second unsynchronised writer to the FAT volume).
 *
 * CSV schema (/sdcard/boot_log.csv):
 *   boot_count,reset_reason,reason_detail,free_heap,min_free_heap_ever,largest_block
 *
 * The boot counter is a small dedicated NVS counter (namespace "fh_boot"),
 * independent of the settings service, so it survives power cycles and gives a
 * monotonic "this is boot N".
 *
 * TODO(HW): not compiled/flashed from this environment (no ESP-IDF toolchain),
 * per AGENTS.md rule #12.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Record the current boot's reset reason + heap figures.
 *
 * Call once in app_main(), AFTER footholed_recon_log_init() (the card must be
 * mounted to persist). Capture the inputs at the very TOP of app_main(), before
 * anything large allocates, so the heap figures reflect a near-pristine start.
 * Always logs to serial; writes to /sdcard/boot_log.csv only if the card is
 * available, and no-ops gracefully otherwise.
 *
 * @param reset_reason        (int)esp_reset_reason()
 * @param free_heap           esp_get_free_heap_size()
 * @param min_free_heap_ever  esp_get_minimum_free_heap_size()
 * @param largest_block       heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT)
 */
void footholed_boot_log_record(int reset_reason,
                               uint32_t free_heap,
                               uint32_t min_free_heap_ever,
                               uint32_t largest_block);

/**
 * @brief Append one heap-usage sample to /sdcard/heap_log.csv.
 *
 * Diagnostic trend logging for the spontaneous-reboot investigation. Unlike the
 * boot-time figures (captured once, pristine), this is called REPEATEDLY while
 * recon is running, so a slow leak shows up as a declining min_free trend you
 * can read off the card after a crash.
 *
 * Crucially this logs the INTERNAL-RAM heap (MALLOC_CAP_INTERNAL) separately
 * from the total. The total free heap (~7 MB) is dominated by PSRAM and hides
 * the real pressure; the Wi-Fi/BLE radio stacks fight over the scarce internal
 * RAM (~512 KB), which is what actually runs out before a panic.
 *
 * CSV schema (/sdcard/heap_log.csv):
 *   uptime_s,context,free_total,free_internal,min_free_internal,largest_internal
 *
 * @param context short tag for WHERE this sample was taken, e.g. "wifi",
 *                "ble", or "wifi->ble" (the transition that precedes the crash).
 *                Must be a short literal/identifier with no comma or newline.
 */
void footholed_boot_log_heap_sample(const char *context);

#ifdef __cplusplus
}
#endif
