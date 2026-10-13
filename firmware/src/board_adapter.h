/**
 * @file board_adapter.h
 * @brief Interface between generic LVGL application code and board drivers.
 */
#pragma once
// Integrate Waveshare ESP32-S3-Touch-LCD-3.5B display and touch drivers here.
// Return true ONLY after LVGL display and input devices are registered.
bool footholed_board_init();

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
