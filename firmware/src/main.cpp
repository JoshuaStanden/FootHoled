/**
 * @file main.cpp
 * @brief Firmware entry point and LVGL scheduling loop.
 *
 * Startup order:
 *   1.  footholed_settings_init()   -- load NVS settings (or apply defaults)
 *   1b. footholed_recon_log_init()  -- mount SD card + open the recon CSV log
 *   1c. footholed_wifi_init()       -- start the Wi-Fi scanner task (idle)
 *   1d. footholed_ble_init()        -- start the BLE scanner task (idle)
 *   2.  lv_init()                   -- LVGL must be ready before board adapter
 *   3.  footholed_board_init()      -- register ST7796 display + FT6336 touch with LVGL
 *   4.  Apply persisted brightness to the backlight
 *   5.  footholed_splash_show()     -- boot splash; on fade-out it calls build_app_ui()
 *   6.  build_app_ui()              -- footholed_ui_init() + IMU auto-rotation
 *   7.  LVGL tick/handler loop (also polls orientation ~4 Hz)
 *
 * IMPORTANT: footholed_board_init() returns false until the Waveshare 3.5
 * (non-B) display/touch BSP is compiled, flashed, and verified on real hardware.
 */
#include "board_adapter.h"
#include "settings.h"
#include "ui.h"
#include "splash.h"
#include "wifi_scan.h"
#include "ble_scan.h"
#include "recon_log.h"
#include "boot_log.h"
#include "esp_log.h"
#include "esp_system.h"       // esp_reset_reason(), esp_get_*_free_heap_size()
#include "esp_heap_caps.h"    // heap_caps_get_largest_free_block()
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
 * @param brightness  Brightness percentage 0-100.
 */
static void apply_brightness(uint8_t brightness) {
    // Delegate to the board's public backlight setter (single source of the
    // LEDC duty math; see board_adapter.cpp).
    footholed_board_set_backlight(brightness);
    ESP_LOGI(TAG, "Backlight set to %u%%", brightness);
}

/**
 * @brief Build the application UI and bring up auto-rotation.
 *
 * Invoked once by the boot splash after it finishes fading out (see
 * footholed_splash_show). Runs on the LVGL/main thread. Kept separate from
 * app_main so the splash controls exactly when the real UI appears.
 */
static void build_app_ui(void) {
    // Build the LVGL screen tree on the (now splash-free) active screen.
    footholed_ui_init();

    // Bring up IMU-driven display auto-rotation (non-fatal if absent).
    // Gated on the persisted setting so the user can turn it off.
    if (footholed_settings_get_auto_rotate()) {
        if (!footholed_orient_init()) {
            ESP_LOGW(TAG, "Auto-rotation unavailable (IMU not detected)");
        }
    } else {
        ESP_LOGI(TAG, "Auto-rotation disabled in settings");
    }
}

extern "C" void app_main(void) {
    // 0. Capture the reset reason and startup heap FIRST, before anything
    //    allocates. The reset reason explains why the previous run ended (a
    //    crash stamps it into an always-on register that survives the reset);
    //    we persist it to /sdcard/boot_log.csv once the card is mounted below.
    //    Heap is read here so the figures reflect a near-pristine startup.
    const int      boot_reset_reason = (int)esp_reset_reason();
    const uint32_t boot_free_heap    = (uint32_t)esp_get_free_heap_size();
    const uint32_t boot_min_free     = (uint32_t)esp_get_minimum_free_heap_size();
    const uint32_t boot_largest_blk  =
        (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);

    // 1. Load settings from NVS (or apply compile-time defaults on first boot).
    //    Must happen before UI init so the UI reads correct initial values.
    const bool settings_ok = footholed_settings_init();
    if (!settings_ok) {
        ESP_LOGW(TAG, "Settings service degraded -- using defaults");
    }

    // 2. LVGL + display hardware FIRST, before the radios.
    //    The LVGL draw buffers need contiguous DMA-capable INTERNAL RAM. Wi-Fi
    //    and NimBLE each grab large internal blocks when they start, which
    //    fragments the region and makes the ~3x15 KB buffer allocation fail
    //    (observed on hardware: "internal DMA draw-buffer alloc failed;
    //    falling back to PSRAM", which then re-triggers the SPI NO_MEM storm).
    //    Claiming the buffers here, while internal RAM is still whole, avoids it.
    //    The scanner tasks below are idle until a screen starts them, so moving
    //    them a few lines later is behaviourally identical.
    lv_init();
    const bool display_ready = footholed_board_init();

    // Mount the SD card and open the persistent recon log before the scanners
    // start producing sightings. Degrades gracefully (feature off) if no card.
    if (!footholed_recon_log_init()) {
        ESP_LOGW(TAG, "Recon SD log unavailable -- sightings will not be persisted");
    }

    // Persist the boot/reset-reason row now that the card (if any) is mounted.
    // Uses the values captured at the very top of app_main(). No-ops to
    // serial-only when there is no card. (boot_log.cpp formats the row and
    // writes it through recon_log's locked SD append.)
    footholed_boot_log_record(boot_reset_reason, boot_free_heap,
                              boot_min_free, boot_largest_blk);

    // Wi-Fi scanning runs on a separate task; no scanning during boot.
    if (!footholed_wifi_init()) ESP_LOGE(TAG, "Wi-Fi scanner initialization failed");

    // BLE scanning also runs on its own task (NimBLE host + worker); idle until
    // the Bluetooth recon screen calls start(). Shares the 2.4 GHz radio with
    // Wi-Fi via the controller's coexistence support.
    if (!footholed_ble_init()) ESP_LOGE(TAG, "BLE scanner initialization failed");

    if (display_ready) {
        // 4. Correct backlight to the user's stored brightness (board init set 50 %).
        apply_brightness(footholed_settings_get_brightness());

        // 5. Show the boot splash now, and build the real UI only after it
        //    finishes (build_app_ui runs from the splash's completion callback).
        //    The splash is non-blocking; the LVGL loop below drives its timing.
        footholed_splash_show(1800 /* hold ms */, build_app_ui);
    } else {
        ESP_LOGW(TAG, "UI unavailable -- display/touch adapter not yet verified on hardware.");
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
