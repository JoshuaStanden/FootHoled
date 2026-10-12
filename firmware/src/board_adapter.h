/**
 * @file board_adapter.h
 * @brief Interface between generic LVGL application code and board drivers.
 */
#pragma once
// Integrate Waveshare ESP32-S3-Touch-LCD-3.5B display and touch drivers here.
// Return true ONLY after LVGL display and input devices are registered.
bool footholed_board_init();
