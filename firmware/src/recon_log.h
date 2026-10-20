#pragma once
#include <stddef.h>
#include <stdint.h>
#include "wifi_scan.h"
#include "ble_scan.h"

/**
 * @file recon_log.h
 * @brief Persistent recon sighting log written to the onboard microSD card.
 *
 * Every Wi-Fi AP and BLE device the recon scanners publish is appended as one
 * CSV row to /sdcard/recon_log.csv. The log is a running trail: it accumulates
 * across power cycles and logs EVERY sighting (no de-duplication) so the file
 * is a time-ordered record of everything the device encountered.
 *
 * CSV schema (one combined file, `type` column distinguishes wifi vs ble):
 *
 *   uptime_s,type,identifier,mac,addr_type,channel,rssi,auth_or_advtype
 *
 *   uptime_s          seconds since boot (see TIMESTAMP TODO below)
 *   type              "wifi" or "ble"
 *   identifier        Wi-Fi SSID, or BLE advertised name ("" if none)
 *   mac               BSSID / BLE address, MSB-first, colon-separated
 *   addr_type         BLE only: 0=public 1=random (blank for wifi)
 *   channel           Wi-Fi only: primary channel (blank for ble)
 *   rssi              signal strength in dBm
 *   auth_or_advtype   Wi-Fi authmode, or BLE advertising PDU type
 *
 * ============================ TIMESTAMP TODO ============================
 * There is currently NO real wall-clock timestamp on each row, only
 * seconds-since-boot (uptime_s). This is a KNOWN LIMITATION we must resolve.
 *
 * The board HAS the hardware to fix this: an onboard **PCF85063 real-time
 * clock** sits on the shared I2C0 bus (SDA GPIO 8 / SCL GPIO 7). To get real
 * dated timestamps a future change must:
 *   1. add a PCF85063 driver (I2C read/write of its time registers),
 *   2. set the RTC once from a known source (build time, GPS, or a UI),
 *   3. read it here per row and emit an ISO-8601 datetime column.
 * Until that lands, uptime_s is the only time reference and resets to 0 every
 * boot. See docs/ROADMAP.md ("Recon log real-time timestamps").
 * =======================================================================
 *
 * SD CARD FORMAT: the microSD must be formatted **FAT32** (ESP-IDF FatFs
 * supports FAT32/exFAT only; ext2/3/4 are NOT supported). Cards up to 32 GB
 * format FAT32 directly; 64 GB+ cards use exFAT (requires CONFIG_FATFS_LFN +
 * exFAT enabled in sdkconfig). See docs/Build_Log.md.
 *
 * TODO(HW): the SDMMC GPIOs (CLK/CMD/D0) are NOT verified for this board
 * revision — see FH_PIN_SD_* in recon_log.cpp. Confirm from the schematic.
 */

// Mount the SD card and open/create the log file. Safe to call once at boot
// after NVS init. Returns false (and the logger no-ops) if no card is present
// or the mount fails — recon still works, it just isn't persisted.
bool footholed_recon_log_init();

// True if the SD card mounted and the log file is writable.
bool footholed_recon_log_available();

// ---------------------------------------------------------------------------
// Card presence / status, independent of whether a FAT filesystem mounted.
// A field device must tell "no card" apart from "card present but its
// filesystem is unreadable (e.g. exFAT/SDXC or raw)" so the UI can offer to
// reclaim the latter with Format — without a PC.
// ---------------------------------------------------------------------------
typedef enum {
    FH_SD_CARD_NONE = 0,       // no card detected (card init failed)
    FH_SD_CARD_READY,          // card present AND FAT mounted (logging works)
    FH_SD_CARD_UNREADABLE,     // card present but filesystem won't mount
} fh_sd_card_status_t;

// Current card status. Safe to call any time after footholed_recon_log_init().
fh_sd_card_status_t footholed_recon_log_card_status(void);

// Card capacity in bytes, 0 if no card. Read from the card CSD at init, so it
// is valid even when the filesystem is UNREADABLE (for "32.0 GB card" display).
uint64_t footholed_recon_log_card_bytes(void);

// Append one CSV row per entry for a completed Wi-Fi scan (every sighting).
// No-ops safely if the SD card is unavailable.
void footholed_recon_log_wifi(const FootHoledAP *aps, size_t count);

// Append one CSV row per entry for a completed BLE scan window (every sighting).
// No-ops safely if the SD card is unavailable.
void footholed_recon_log_ble(const FootHoledBLEDevice *devs, size_t count);

// ---------------------------------------------------------------------------
// DESTRUCTIVE: fully format the mounted microSD card, erasing EVERYTHING on it,
// then recreate a fresh empty /sdcard/recon_log.csv (header row only).
//
// This is the backing operation for the "Format SD Card" UI action. It lives
// here, not in sd_format.cpp, because this module owns the mounted card handle
// and the log path. The UI MUST gate this behind explicit user confirmation;
// this function performs NO confirmation of its own.
//
// Returns false if no card is mounted or the format fails. On success the
// logger remains available and ready to append to the new empty log.
// THIS CANNOT BE UNDONE.
bool footholed_recon_log_format(void);

// ---------------------------------------------------------------------------
// ASYNC format: the synchronous footholed_recon_log_format() above blocks for
// many seconds (full-card erase) and would freeze the LVGL UI thread if called
// from a click handler. These run the SAME destructive format on a dedicated
// FreeRTOS task and expose a phase + percent the UI can POLL without blocking.
// ---------------------------------------------------------------------------
typedef enum {
    FH_RECON_FMT_IDLE = 0,   // no format has been started
    FH_RECON_FMT_RUNNING,    // format in progress (see percent)
    FH_RECON_FMT_DONE_OK,    // finished successfully; empty log recreated
    FH_RECON_FMT_DONE_NOCARD,// no card mounted; nothing done
    FH_RECON_FMT_DONE_FAIL,  // format failed; card state undefined
} fh_recon_fmt_phase_t;

// Start the async format. Formats the card to FAT32 at FULL capacity and
// mounts it. Works whether the card is currently mounted (FH_SD_CARD_READY) OR
// present-but-unreadable (FH_SD_CARD_UNREADABLE, e.g. exFAT/SDXC/raw) — so a
// field user can reclaim any card WITHOUT a PC. Returns false only if a format
// is already running or NO card is present (phase set to DONE_NOCARD then).
// The caller MUST have obtained explicit user confirmation first. Non-blocking.
bool footholed_recon_log_format_start(void);

// Poll async-format status. Writes the current phase and a coarse 0..100
// percent (phase-driven, not byte-accurate). Safe to call from the UI thread
// every frame. Returns the phase for convenience.
fh_recon_fmt_phase_t footholed_recon_log_format_poll(int *out_percent);

// ---------------------------------------------------------------------------
// Generic locked append helper (SD ownership stays here).
//
// recon_log.cpp owns the SD card (mount handle + mutex). Other modules that
// need to write a file on the card (e.g. boot_log.cpp -> /sdcard/boot_log.csv)
// MUST go through this instead of opening the card themselves — two
// unsynchronised writers to one FAT volume can corrupt it. Appends `line` to
// `path`, writing `header` first if the file does not exist yet, all under the
// SD mutex. Returns false (no-op) when the card is unavailable.
bool footholed_recon_log_append_line(const char *path, const char *header,
                                     const char *line);
