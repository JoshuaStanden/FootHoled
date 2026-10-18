/**
 * @file lv_conf.h
 * @brief Minimal LVGL 9.2 configuration for the FootHoled H1
 *        (Waveshare ESP32-S3-Touch-LCD-3.5 NON-B, ST7796 SPI, 320x480 RGB565).
 *
 * Why this file exists:
 *   platformio.ini sets -D LV_CONF_INCLUDE_SIMPLE, which makes LVGL look for an
 *   lv_conf.h on the include path.  Without one, LVGL 9 either fails to build
 *   or falls back to LV_COLOR_DEPTH 32 (lv_color_t = 4 bytes).  The ST7796
 *   panel is driven as 16-bit RGB565 (esp_lcd_panel_dev_config_t.bits_per_pixel
 *   = 16 in board_adapter.cpp), so the colour depth MUST be pinned to 16 for
 *   lv_color_t, the draw-buffer sizing, and the flush to agree.  With depth 16,
 *   sizeof(lv_color_t) == 2 and the board_adapter buffer maths is correct.
 *
 * This is a deliberately small config: only what a bring-up needs is enabled.
 * TODO(HW): revisit font/widget/feature set once the panel is verified.
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/

/* Colour depth: 16-bit RGB565 to match the ST7796 panel. */
#define LV_COLOR_DEPTH 16

/*=========================
   STDLIB WRAPPER SETTINGS
 *=========================*/

/* Use the ESP-IDF / newlib C library for malloc, memcpy, etc. */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB

/*====================
   HAL SETTINGS
 *====================*/

/* Default display refresh / input read period (ms). */
#define LV_DEF_REFR_PERIOD  30
#define LV_DPI_DEF          130     /* 3.5" 320x480 ~= 164 dpi; 130 is a safe default */

/*=======================
   OPERATING SYSTEM
 *=======================*/

/* LVGL is driven from a single task here (FreeRTOS); no built-in OS locking. */
#define LV_USE_OS   LV_OS_NONE

/*========================
   RENDERING / DRAW
 *========================*/

/* Software renderer; no custom draw units for bring-up. */
#define LV_DRAW_BUF_STRIDE_ALIGN    1
#define LV_DRAW_BUF_ALIGN           4

/* Required for lv_draw_rotate(): the firmware software-rotates each partial
 * tile in board_adapter.cpp:flush_cb() for IMU-driven display auto-rotation,
 * because the ST7796 is not driven with hardware rotation. Rotation is a
 * "complex" software-draw feature, so this MUST stay enabled while
 * auto-rotation is in use. */
#define LV_DRAW_SW_COMPLEX          1

/*====================
   LOGGING
 *====================*/

#define LV_USE_LOG 0

/*====================
   ASSERTS
 *====================*/

#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

/*====================
   FONTS
 *====================*/

/* 14px Montserrat is the UI default (ui.cpp); 28px is used only by the boot
 * splash wordmark (splash.cpp). Keep this set minimal to limit font flash use. */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/*====================
   LAYOUTS
 *====================*/

/* ui.cpp lays every screen out with lv_obj_set_flex_flow(). */
#define LV_USE_FLEX 1
#define LV_USE_GRID 0

/*====================
   WIDGETS
   The set the app instantiates: label, button, switch, slider (built on bar),
   plus line + bar used by the boot splash (splash.cpp). The generic container
   lv_obj_create() needs no feature flag.
 *====================*/

#define LV_USE_LABEL    1
#define LV_USE_BUTTON   1
#define LV_USE_SWITCH   1
#define LV_USE_BAR      1   /* required by slider; also the boot splash bar */
#define LV_USE_SLIDER   1
#define LV_USE_LINE     1   /* boot splash shield outline (splash.cpp) */

/*====================
   OTHERS / EXAMPLES
 *====================*/

#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR  0

#endif /* LV_CONF_H */
