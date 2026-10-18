/**
 * @file board_adapter.h
 * @brief Interface between generic LVGL application code and board drivers.
 */
#pragma once
#include "driver/i2c_master.h"
// Integrate Waveshare ESP32-S3-Touch-LCD-3.5B display and touch drivers here.
// Return true ONLY after LVGL display and input devices are registered.
bool footholed_board_init();

// ---------------------------------------------------------------------------
// Accessors for services that need board-owned resources (diagnostics, etc.)
// ---------------------------------------------------------------------------
// The shared I2C0 master bus handle (touch, IMU, RTC, PMIC, power monitor,
// audio codec all live here). Returns nullptr before footholed_board_init().
i2c_master_bus_handle_t footholed_board_i2c_bus(void);

// True once the display + input devices are registered with LVGL (i.e. after
// footholed_board_init() has succeeded). Used by the diagnostics self-test.
bool footholed_board_display_ready(void);

// ---------------------------------------------------------------------------
// Display auto-rotation (QMI8658 IMU)
// ---------------------------------------------------------------------------
// Call footholed_orient_init() AFTER footholed_board_init() succeeds (the IMU
// shares the I2C bus created there, and rotation needs the LVGL display). It
// returns false if the IMU is absent or disabled — auto-rotation is then
// skipped and the UI runs normally.
//
// Call footholed_orient_poll() periodically from the main loop (a few times a
// second is plenty). It reads the accelerometer and, when the device's
// orientation changes, rotates the LVGL display to match. Safe to call even
// when the IMU is unavailable (it no-ops).
bool footholed_orient_init(void);
void footholed_orient_poll(void);
