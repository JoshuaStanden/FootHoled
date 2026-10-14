/**
 * @file main.cpp
 * @brief Firmware entry point and LVGL scheduling loop.
 *
 * Startup order:
 *   1. footholed_settings_init() — load NVS settings (or apply defaults)
 *   2. lv_init()                 — LVGL must be ready before board adapter
 *   3. footholed_board_init()    — register ST7796 display + FT6336 touch with LVGL
 *   4. Apply persisted brightness to the backlight
 *   5. footholed_ui_init()       — build the LVGL screen tree
 *   6. footholed_orient_init()   — bring up IMU display auto-rotation (optional)
 *   7. LVGL tick/handler loop (also polls orientation ~4 Hz)
 *
 * IMPORTANT: footholed_board_init() returns false until the Waveshare 3.5B
 * display/touch BSP is compiled, flashed, and verified on real hardware.
 */
#include "board_adapter.h"
#include "settings.h"
#include "ui.h"
#include "wifi_scan.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "lvgl.h"

static const char *TAG = "FootHoled/main";

/**
 * @brief Apply the persisted brightness setting to the LEDC backlight.
 *
 * board_adapter.cpp starts the backlight at 50 % during init.  Once settings
 * are loaded we correct it to the user's stored value.  Only called when the
 * display is confirmed ready so the LEDC channel is guaranteed initialised.
 *
 * @param brightness  Brightness percentage 0–100.
 */
static void apply_brightness(uint8_t brightness) {
    // 10-bit duty: 0–1023.  Match constants from board_adapter.cpp.
    const uint32_t max_duty = (1u << 10) - 1;  // 1023
    uint32_t duty = ((uint32_t)brightness * max_duty) / 100u;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    ESP_LOGI(TAG, "Backlight set to %u%% (duty %lu)", brightness, duty);
}

extern "C" void app_main(void) {
    // 1. Load settings from NVS (or apply compile-time defaults on first boot).
    //    Must happen before UI init so the UI reads correct initial values.
    const bool settings_ok = footholed_settings_init();
    if (!settings_ok) {
        ESP_LOGW(TAG, "Settings service degraded — using defaults");
    }

    // Wi-Fi scanning runs on a separate task; no scanning during boot.
    if (!footholed_wifi_init()) ESP_LOGE(TAG, "Wi-Fi scanner initialization failed");

    // 2. LVGL must be initialized before the board adapter registers display/input.
    lv_init();

    // 3. Initialise board hardware (display + touch).
    const bool display_ready = footholed_board_init();

    if (display_ready) {
        // 4. Correct backlight to the user's stored brightness (board init set 50 %).
        apply_brightness(footholed_settings_get_brightness());

        // 5. Build the LVGL screen tree.
        footholed_ui_init();

        // 6. Bring up IMU-driven display auto-rotation (non-fatal if absent).
        //    Gated on the persisted setting so the user can turn it off.
        if (footholed_settings_get_auto_rotate()) {
            if (!footholed_orient_init()) {
                ESP_LOGW(TAG, "Auto-rotation unavailable (IMU not detected)");
            }
        } else {
            ESP_LOGI(TAG, "Auto-rotation disabled in settings");
        }
    } else {
        ESP_LOGW(TAG, "UI unavailable — display/touch adapter not yet verified on hardware.");
    }

    // 7. LVGL tick and timer handler loop.
    //    Convert microsecond wall time into LVGL millisecond ticks.
    //    A future display driver may provide its own tick source; avoid double-ticking.
    int64_t last = esp_timer_get_time();
    uint32_t orient_div = 0;   // throttle: poll the IMU every ~250 ms, not every 10 ms
    while (true) {
        int64_t now     = esp_timer_get_time();
        uint32_t elapsed = (uint32_t)((now - last) / 1000);
        if (elapsed > 0) {
            lv_tick_inc(elapsed);
            last += (int64_t)elapsed * 1000;
        }
        if (display_ready) {
            lv_timer_handler();
            // Orientation changes slowly; polling at ~4 Hz is plenty and keeps
            // the shared I2C bus free for touch. 25 * 10 ms = 250 ms.
            if (++orient_div >= 25) {
                orient_div = 0;
                footholed_orient_poll();   // no-ops if the IMU is unavailable
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
