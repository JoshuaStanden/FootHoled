/**
 * @file settings.h
 * @brief FootHoled persistent settings service — NVS-backed key/value store.
 *
 * Provides typed getters and setters for every user-configurable value.
 * All writes go to NVS flash immediately (no explicit flush needed).
 * Reads return the cached in-RAM value after init; NVS is not queried again
 * until the next reboot.
 *
 * Call footholed_settings_init() once during startup (before footholed_ui_init).
 * The service is safe to call from any task after init.
 *
 * NVS namespace: "fh_settings"
 *
 * Key names (max 15 chars per ESP-IDF NVS limit):
 *   "brightness"   uint8   0–100 (backlight %)
 *   "privacy_on"   uint8   0 = off, 1 = on
 *   "mask_len"     uint8   1–15 (privacy mask character count)
 *   "auto_rotate"  uint8   0 = off, 1 = on (IMU display auto-rotation)
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the settings service and load all values from NVS.
 *
 * Must be called after nvs_flash_init() and before any getter/setter.
 * Safe to call multiple times; subsequent calls are no-ops.
 *
 * @return true  NVS opened and values loaded (or defaults applied on first boot).
 * @return false NVS initialisation failed; getters return defaults, setters are no-ops.
 */
bool footholed_settings_init(void);

// ---------------------------------------------------------------------------
// Backlight brightness  (0–100 %)
// ---------------------------------------------------------------------------

/** @brief Get display brightness. @return 0–100 */
uint8_t footholed_settings_get_brightness(void);

/**
 * @brief Set and persist display brightness.
 * @param value  Clamped to 0–100. Values above 100 are stored as 100.
 */
void footholed_settings_set_brightness(uint8_t value);

// ---------------------------------------------------------------------------
// Privacy masking
// ---------------------------------------------------------------------------

/** @brief Get whether privacy masking is enabled. */
bool footholed_settings_get_privacy_on(void);

/** @brief Set and persist privacy masking enabled flag. */
void footholed_settings_set_privacy_on(bool on);

// ---------------------------------------------------------------------------
// Display auto-rotation
// ---------------------------------------------------------------------------

/** @brief Get whether IMU-driven display auto-rotation is enabled (default on). */
bool footholed_settings_get_auto_rotate(void);

/** @brief Set and persist the display auto-rotation enabled flag. */
void footholed_settings_set_auto_rotate(bool on);

/** @brief Get privacy mask length (1–15 leading characters masked). */
uint8_t footholed_settings_get_mask_length(void);

/**
 * @brief Set and persist privacy mask length.
 * @param len  Clamped to 1–15.
 */
void footholed_settings_set_mask_length(uint8_t len);

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------

/**
 * @brief Reset all settings to factory defaults and persist to NVS.
 *
 * Defaults: brightness=75, privacy_on=true, mask_length=4, auto_rotate=true.
 * Used by the UI "Reset to defaults" action (future).
 */
void footholed_settings_reset_defaults(void);

#ifdef __cplusplus
}
#endif
