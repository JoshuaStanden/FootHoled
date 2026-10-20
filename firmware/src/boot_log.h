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

#ifdef __cplusplus
}
#endif
