/**
 * @file main.cpp
 * @brief Firmware entry point and LVGL scheduling loop.
 *
 * Initializes LVGL, delegates board-specific display/touch registration to the
 * board adapter, and drives the LVGL tick and timer handler from one task.
 * IMPORTANT: The current board adapter intentionally returns false; this is
 * not yet flash-tested, working display firmware.
 */
#include "board_adapter.h"
#include "ui.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "lvgl.h"

extern "C" void app_main(void) {
    // LVGL must be initialized before the board adapter registers display/input.
    lv_init();
    const bool display_ready = footholed_board_init();
    if (display_ready) {
        footholed_ui_init();
    } else {
        ESP_LOGW("FootHoled", "UI unavailable until the 3.5B display/touch adapter is implemented.");
    }
    // Convert microsecond wall time into LVGL millisecond ticks.
    // A future display driver may provide its own tick source; avoid double-ticking.
    int64_t last = esp_timer_get_time();
    while (true) {
        int64_t now = esp_timer_get_time();
        uint32_t elapsed = (uint32_t)((now - last) / 1000);
        if (elapsed > 0) { lv_tick_inc(elapsed); last += (int64_t)elapsed * 1000; }
        if (display_ready) lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
