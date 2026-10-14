/**
 * @file board_adapter.cpp
 * @brief Waveshare ESP32-S3-Touch-LCD-3.5 (NON-B) display and touch init.
 *
 * TARGET BOARD: Waveshare ESP32-S3-Touch-LCD-3.5 (the plain 3.5, NOT the 3.5B).
 *   - Display controller: ST7796 over STANDARD SPI (NOT QSPI)
 *   - Touch controller:   FT6336 over I2C
 *
 * This adapter mirrors the VENDOR's own ESP-IDF bring-up
 * (ESP32-S3-Touch-LCD-3.5-Demo / ESP-IDF/01_factory/components/esp_port/
 *  esp_3inch5_lcd_port.cpp) so it matches a known-working configuration, then
 * wraps it in the FootHoled LVGL 9 integration (async flush via trans-done
 * callback, software rotation, QMI8658 auto-rotation).
 *
 * All pin numbers below are VERIFIED against the vendor port (see values and
 * their source in docs/H0.1_Pin_Map.md). The vendor path uses NO TCA9554 I/O
 * expander for display or touch — CS/RST/TP_RST/TP_INT are all GPIO_NUM_NC — so
 * this adapter does not use one either. (An earlier draft added a speculative
 * TCA9554 reset path; the vendor source shows it is unnecessary, so it was
 * removed to match the proven configuration.)
 *
 * IMPORTANT — per AGENTS.md rule #12: this has NOT been compiled, flashed, or
 * tested on hardware. It is a faithful port of the vendor configuration; treat
 * all hardware observations as TODO(HW).
 */

#include "board_adapter.h"
#include "settings.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7796.h"       // espressif/esp_lcd_st7796 component
#include "qmi8658.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// The ST7796 panel is driven as 16-bit RGB565 (bits_per_pixel = 16 below), and
// the draw-buffer sizing uses sizeof(lv_color_t). Those agree ONLY when LVGL is
// configured for 16-bit colour. Fail the build loudly otherwise.
#if LV_COLOR_DEPTH != 16
#error "FootHoled board_adapter.cpp requires LV_COLOR_DEPTH==16 (RGB565) to match the ST7796 panel. Check firmware/src/lv_conf.h."
#endif

static const char *TAG = "FootHoled/board";

// ---------------------------------------------------------------------------
// Pin definitions — VERIFIED, identical to the vendor ESP-IDF port
// (esp_3inch5_lcd_port.cpp) and the vendor Arduino example (08_gfx_helloworld).
// Cross-checked against the official 2.54 mm expansion-header pinout for I2C.
// ---------------------------------------------------------------------------
#define FH_PIN_LCD_MISO  GPIO_NUM_NC   // [verified] MISO not used (vendor = NC)
#define FH_PIN_LCD_MOSI  GPIO_NUM_1    // [verified] SPI MOSI (data to panel)
#define FH_PIN_LCD_SCLK  GPIO_NUM_5    // [verified] SPI clock
#define FH_PIN_LCD_CS    GPIO_NUM_NC   // [verified] CS not a dedicated GPIO
#define FH_PIN_LCD_DC    GPIO_NUM_3    // [verified] data/command select
#define FH_PIN_LCD_RST   GPIO_NUM_NC   // [verified] no dedicated reset GPIO
#define FH_PIN_LCD_BL    GPIO_NUM_6    // [verified] backlight (LEDC PWM)

#define FH_PIN_I2C_SDA   GPIO_NUM_8    // [verified] shared I2C0 — header pin 28
#define FH_PIN_I2C_SCL   GPIO_NUM_7    // [verified] shared I2C0 — header pin 26

#define FH_PIN_TP_INT    GPIO_NUM_NC   // [verified] touch INT not wired (polled)
#define FH_PIN_TP_RST    GPIO_NUM_NC   // [verified] touch RST not wired

// ---------------------------------------------------------------------------
// Display parameters
// ---------------------------------------------------------------------------
#define FH_LCD_H_RES          320
#define FH_LCD_V_RES          480
// [verified] vendor uses an 80 MHz pixel clock for the ST7796 on this board.
#define FH_LCD_PCLK_HZ        (80 * 1000 * 1000)
#define FH_SPI_HOST           SPI2_HOST
#define FH_LCD_CMD_BITS       8
#define FH_LCD_PARAM_BITS     8

// LVGL draw-buffer size: 1/10 frame, SPIRAM-backed.
#define FH_BUF_LINES          (FH_LCD_V_RES / 10)
#define FH_BUF_SIZE           (FH_LCD_H_RES * FH_BUF_LINES)

// ---------------------------------------------------------------------------
// Backlight PWM — matches the vendor LEDC config (10-bit, 5 kHz).
// ---------------------------------------------------------------------------
#define FH_BL_LEDC_TIMER      LEDC_TIMER_1          // vendor uses TIMER_1 here
#define FH_BL_LEDC_MODE       LEDC_LOW_SPEED_MODE
#define FH_BL_LEDC_CHANNEL    LEDC_CHANNEL_0
#define FH_BL_DUTY_BITS       LEDC_TIMER_10_BIT     // 0–1023
#define FH_BL_FREQ_HZ         5000
#define FH_BL_DUTY_MAX        1023

// FT6336 touch controller — standard FT6x36 I2C register layout.
#define FH_TOUCH_I2C_ADDR     0x38   // FT6336 fixed 7-bit address (vendor confirms)
#define FT6336_REG_TD_STATUS  0x02   // number of touch points (low nibble)
#define FT6336_REG_P1_XH      0x03   // point 1 X high byte (+flags), then XL, YH, YL

// ---------------------------------------------------------------------------
// Module-level handles — owned by this translation unit for the device lifetime
// ---------------------------------------------------------------------------
static esp_lcd_panel_io_handle_t  s_io_handle    = nullptr;
static esp_lcd_panel_handle_t     s_panel_handle = nullptr;
static i2c_master_bus_handle_t    s_i2c_bus      = nullptr;
static i2c_master_dev_handle_t    s_touch_dev    = nullptr;

static lv_display_t *s_lvgl_disp = nullptr;
static qmi8658_handle_t s_imu = nullptr;
static qmi8658_orientation_t s_applied_orient = QMI8658_ORIENT_PORTRAIT;
static bool s_imu_init_failed = false;

static lv_color_t *s_lvgl_buf1 = nullptr;
static lv_color_t *s_lvgl_buf2 = nullptr;
static lv_color_t *s_rotate_buf = nullptr;

// ---------------------------------------------------------------------------
// LVGL 9 flush callback.
//
// esp_lcd_panel_draw_bitmap() on the ST7796 SPI panel is asynchronous (queues a
// DMA transfer). We do NOT call lv_display_flush_ready() here; it is signalled
// from colour_trans_done_cb() when the transfer finishes. Software rotation
// into s_rotate_buf is used for non-zero rotations (no hardware rotation).
// ---------------------------------------------------------------------------
static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    lv_area_t out_area = *area;
    uint8_t  *out_px   = px_map;

    lv_display_rotation_t rot = lv_display_get_rotation(disp);
    if (rot != LV_DISPLAY_ROTATION_0 && s_rotate_buf) {
        lv_color_format_t cf = lv_display_get_color_format(disp);
        lv_display_rotate_area(disp, &out_area);

        int32_t src_w = lv_area_get_width(area);
        int32_t src_h = lv_area_get_height(area);
        uint32_t src_stride  = lv_draw_buf_width_to_stride(src_w, cf);
        uint32_t dest_stride = lv_draw_buf_width_to_stride(lv_area_get_width(&out_area), cf);

        lv_draw_sw_rotate(px_map, s_rotate_buf, src_w, src_h,
                       src_stride, dest_stride, rot, cf);
        out_px = (uint8_t *)s_rotate_buf;
    }

    // Convert LVGL RGB565 byte order for the ST7796 SPI transfer.
    const uint32_t pixel_count =
        (out_area.x2 - out_area.x1 + 1) *
        (out_area.y2 - out_area.y1 + 1);

    lv_draw_sw_rgb565_swap(out_px, pixel_count);

    esp_err_t err = esp_lcd_panel_draw_bitmap(
        s_panel_handle,
        out_area.x1, out_area.y1,
        out_area.x2 + 1, out_area.y2 + 1,
        out_px);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "draw_bitmap failed: %s — releasing buffer to avoid stall",
                 esp_err_to_name(err));
        lv_display_flush_ready(disp);
    }
    // On success colour_trans_done_cb() releases the buffer.
}

static bool colour_trans_done_cb(esp_lcd_panel_io_handle_t io,
                                 esp_lcd_panel_io_event_data_t *edata,
                                 void *user_ctx)
{
    lv_display_t *disp = (lv_display_t *)user_ctx;
    if (disp) lv_display_flush_ready(disp);
    return false;
}

// ---------------------------------------------------------------------------
// FT6336 touch read — STANDARD FT6x36 register protocol.
//
// Write the start register (TD_STATUS 0x02), then read 5 bytes:
//   [0] TD_STATUS: low nibble = active touch points (0..2)
//   [1] P1_XH: [3:0] X high nibble ; [2] P1_XL: X low byte
//   [3] P1_YH: [3:0] Y high nibble ; [4] P1_YL: Y low byte
//
// NOTE: the vendor ESP-IDF demo uses the dedicated esp_lcd_touch_ft6336
// component (an I2C panel-IO + esp_lcd_touch handle). This inline register read
// is a lighter equivalent that avoids pulling that component in. If touch is
// flaky on hardware, switching to esp_lcd_touch_ft6336 is the robust fallback
// (it also handles swap/mirror per rotation). TODO(HW): confirm on hardware.
// ---------------------------------------------------------------------------
static bool read_touch_raw(uint16_t *x_out, uint16_t *y_out)
{
    if (!s_touch_dev) return false;

    const uint8_t start_reg = FT6336_REG_TD_STATUS;
    uint8_t buf[5] = {};

    esp_err_t ret = i2c_master_transmit_receive(
        s_touch_dev, &start_reg, 1, buf, sizeof(buf), 50);
    if (ret != ESP_OK) return false;

    uint8_t points = buf[0] & 0x0F;
    if (points < 1 || points > 2) return false;   // no touch / invalid

    *x_out = (uint16_t)(((buf[1] & 0x0F) << 8) | buf[2]);
    *y_out = (uint16_t)(((buf[3] & 0x0F) << 8) | buf[4]);
    return true;
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    uint16_t x = 0, y = 0;
    if (read_touch_raw(&x, &y)) {
        data->point.x = (int32_t)x;
        data->point.y = (int32_t)y;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ---------------------------------------------------------------------------
// Backlight — LEDC PWM at 50 % duty (matches vendor LEDC setup).
// ---------------------------------------------------------------------------
static void backlight_init(void)
{
    ledc_timer_config_t timer = {};
    timer.speed_mode      = FH_BL_LEDC_MODE;
    timer.timer_num       = FH_BL_LEDC_TIMER;
    timer.duty_resolution = FH_BL_DUTY_BITS;
    timer.freq_hz         = FH_BL_FREQ_HZ;
    timer.clk_cfg         = LEDC_AUTO_CLK;
    ledc_timer_config(&timer);

    ledc_channel_config_t ch = {};
    ch.speed_mode = FH_BL_LEDC_MODE;
    ch.channel    = FH_BL_LEDC_CHANNEL;
    ch.timer_sel  = FH_BL_LEDC_TIMER;
    ch.intr_type  = LEDC_INTR_DISABLE;
    ch.gpio_num   = FH_PIN_LCD_BL;
    ch.duty       = FH_BL_DUTY_MAX / 2;   // 50 % — corrected to stored value in main.cpp
    ch.hpoint     = 0;
    ledc_channel_config(&ch);
    ESP_LOGI(TAG, "Backlight on at 50%% (GPIO%d PWM)", FH_PIN_LCD_BL);
}

// ---------------------------------------------------------------------------
// footholed_board_init() — the one symbol main.cpp calls.
// Order mirrors the vendor port: SPI bus -> panel IO -> ST7796 panel ->
// reset/init/invert/on, then I2C bus + FT6336, then LVGL wiring + backlight.
// ---------------------------------------------------------------------------
bool footholed_board_init(void)
{
    ESP_LOGI(TAG, "Initialising Waveshare ESP32-S3-Touch-LCD-3.5 (NON-B: ST7796 SPI + FT6336)");

    // ------------------------------------------------------------------
    // 1. SPI bus (standard 1-data-line SPI, NOT QSPI)
    // ------------------------------------------------------------------
    spi_bus_config_t bus = {};
    bus.sclk_io_num     = FH_PIN_LCD_SCLK;
    bus.mosi_io_num     = FH_PIN_LCD_MOSI;
    bus.miso_io_num     = FH_PIN_LCD_MISO;
    bus.quadwp_io_num   = -1;
    bus.quadhd_io_num   = -1;
    bus.max_transfer_sz = FH_LCD_H_RES * FH_BUF_LINES * (int)sizeof(uint16_t);
    if (spi_bus_initialize(FH_SPI_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed");
        return false;
    }

    // ------------------------------------------------------------------
    // 2. Panel IO (SPI). The transfer-done callback is registered here via
    //    user_ctx = lvgl display, set once the display exists (step 5); the
    //    vendor sets it NULL and signals synchronously. We attach it in step 5
    //    with esp_lcd_panel_io_register_event_callbacks instead.
    // ------------------------------------------------------------------
    esp_lcd_panel_io_spi_config_t io_cfg = {};
    io_cfg.cs_gpio_num       = FH_PIN_LCD_CS;
    io_cfg.dc_gpio_num       = FH_PIN_LCD_DC;
    io_cfg.spi_mode          = 0;
    io_cfg.pclk_hz           = FH_LCD_PCLK_HZ;
    io_cfg.trans_queue_depth = 10;
    io_cfg.lcd_cmd_bits      = FH_LCD_CMD_BITS;
    io_cfg.lcd_param_bits    = FH_LCD_PARAM_BITS;
    if (esp_lcd_new_panel_io_spi(
            (esp_lcd_spi_bus_handle_t)FH_SPI_HOST, &io_cfg, &s_io_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Panel IO create failed");
        return false;
    }

    // ------------------------------------------------------------------
    // 3. ST7796 panel driver (stock component default init table).
    //    BGR order + colour inversion ON, exactly as the vendor port does.
    // ------------------------------------------------------------------
    esp_lcd_panel_dev_config_t panel_cfg = {};
    panel_cfg.reset_gpio_num  = FH_PIN_LCD_RST;              // NC — no HW reset line
    panel_cfg.rgb_ele_order   = LCD_RGB_ELEMENT_ORDER_BGR;  // [verified] vendor uses BGR
    panel_cfg.bits_per_pixel  = 16;
    if (esp_lcd_new_panel_st7796(s_io_handle, &panel_cfg, &s_panel_handle) != ESP_OK) {
        ESP_LOGE(TAG, "ST7796 panel create failed");
        return false;
    }
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel_handle));
    // Set the verified horizontal mirror once, not on every DMA flush.
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel_handle, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel_handle, true));  // [verified] vendor inverts
    // Display stays off until LVGL is wired; turned on after disp registration.
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel_handle, false));

    // ------------------------------------------------------------------
    // 4. I2C bus for touch (also used by IMU, RTC, PMIC, codec)
    // ------------------------------------------------------------------
    i2c_master_bus_config_t i2c_cfg = {};
    i2c_cfg.i2c_port          = I2C_NUM_0;
    i2c_cfg.sda_io_num        = FH_PIN_I2C_SDA;
    i2c_cfg.scl_io_num        = FH_PIN_I2C_SCL;
    i2c_cfg.clk_source        = I2C_CLK_SRC_DEFAULT;
    i2c_cfg.glitch_ignore_cnt = 7;
    i2c_cfg.flags.enable_internal_pullup = true;
    if (i2c_new_master_bus(&i2c_cfg, &s_i2c_bus) != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed");
        return false;
    }

    i2c_device_config_t touch_cfg = {};
    touch_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    touch_cfg.device_address  = FH_TOUCH_I2C_ADDR;
    touch_cfg.scl_speed_hz    = 400000;
    if (i2c_master_bus_add_device(s_i2c_bus, &touch_cfg, &s_touch_dev) != ESP_OK) {
        ESP_LOGW(TAG, "FT6336 touch add failed — touch disabled, UI still runs");
        s_touch_dev = nullptr;
    }

    // ------------------------------------------------------------------
    // 5. Register LVGL 9 display + async flush
    // ------------------------------------------------------------------
    lv_display_t *lvgl_disp = lv_display_create(FH_LCD_H_RES, FH_LCD_V_RES);
    if (!lvgl_disp) {
        ESP_LOGE(TAG, "lv_display_create failed");
        return false;
    }
    s_lvgl_disp = lvgl_disp;

    const esp_lcd_panel_io_callbacks_t io_cbs = {
        .on_color_trans_done = colour_trans_done_cb,
    };
    if (esp_lcd_panel_io_register_event_callbacks(
            s_io_handle, &io_cbs, lvgl_disp) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register panel-IO transfer-done callback");
        return false;
    }

    s_lvgl_buf1 = (lv_color_t *)heap_caps_malloc(FH_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    s_lvgl_buf2 = (lv_color_t *)heap_caps_malloc(FH_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    s_rotate_buf = (lv_color_t *)heap_caps_malloc(FH_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    if (!s_lvgl_buf1 || !s_lvgl_buf2 || !s_rotate_buf) {
        ESP_LOGE(TAG, "LVGL draw buffer allocation failed (SPIRAM)");
        if (s_lvgl_buf1) { heap_caps_free(s_lvgl_buf1); s_lvgl_buf1 = nullptr; }
        if (s_lvgl_buf2) { heap_caps_free(s_lvgl_buf2); s_lvgl_buf2 = nullptr; }
        if (s_rotate_buf){ heap_caps_free(s_rotate_buf);s_rotate_buf = nullptr; }
        return false;
    }

    lv_display_set_buffers(lvgl_disp, s_lvgl_buf1, s_lvgl_buf2,
                           FH_BUF_SIZE * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(lvgl_disp, flush_cb);

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel_handle, true));

    // ------------------------------------------------------------------
    // 6. Register LVGL 9 touch input device (FT6336)
    // ------------------------------------------------------------------
    lv_indev_t *indev = lv_indev_create();
    if (indev) {
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, touch_read_cb);
    } else {
        ESP_LOGW(TAG, "Could not create LVGL touch indev");
    }

    // ------------------------------------------------------------------
    // 7. Backlight on
    // ------------------------------------------------------------------
    backlight_init();

    ESP_LOGI(TAG, "Board init complete — display %dx%d (ST7796 SPI @ %d MHz), touch %s (FT6336)",
             FH_LCD_H_RES, FH_LCD_V_RES, FH_LCD_PCLK_HZ / 1000000,
             indev ? "registered" : "UNAVAILABLE");

    // TODO(HW): verify on physical hardware (AGENTS.md #12). Pins match the
    // vendor port, but this has not been compiled/flashed/tested.
    return true;
}


// ===========================================================================
// Display auto-rotation service (QMI8658 IMU) — unchanged, board-independent
// ===========================================================================
// static lv_display_rotation_t orient_to_lv(qmi8658_orientation_t o)
// {
//     switch (o) {
//         case QMI8658_ORIENT_PORTRAIT:         return LV_DISPLAY_ROTATION_0;
//         case QMI8658_ORIENT_LANDSCAPE_LEFT:   return LV_DISPLAY_ROTATION_90;
//         case QMI8658_ORIENT_PORTRAIT_FLIPPED: return LV_DISPLAY_ROTATION_180;
//         case QMI8658_ORIENT_LANDSCAPE_RIGHT:  return LV_DISPLAY_ROTATION_270;
//         default:                              return LV_DISPLAY_ROTATION_0;
//     }
// }
// above mapping is all fuckered up, testing remap below. 
static lv_display_rotation_t orient_to_lv(qmi8658_orientation_t o)
{
    switch ((int)o) {
        case 3: return LV_DISPLAY_ROTATION_0;
        case 2: return LV_DISPLAY_ROTATION_90;
        case 1: return LV_DISPLAY_ROTATION_180;
        case 0: return LV_DISPLAY_ROTATION_270;
        default: return LV_DISPLAY_ROTATION_0;
    }
}



bool footholed_orient_init(void)
{
    if (!s_i2c_bus) {
        ESP_LOGW(TAG, "Auto-rotate: I2C bus not ready, IMU not started");
        return false;
    }
    esp_err_t err = qmi8658_init(s_i2c_bus, QMI8658_I2C_ADDR_LOW, &s_imu);
    if (err != ESP_OK) {
        err = qmi8658_init(s_i2c_bus, QMI8658_I2C_ADDR_HIGH, &s_imu);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Auto-rotate: QMI8658 not found — auto-rotation disabled");
        s_imu = nullptr;
        s_imu_init_failed = true;
        return false;
    }
    s_imu_init_failed = false;
    ESP_LOGI(TAG, "Auto-rotate: IMU ready");
    return true;
}

void footholed_orient_poll(void)
{
    if (!s_lvgl_disp) return;

    if (!footholed_settings_get_auto_rotate()) {
        if (s_applied_orient != QMI8658_ORIENT_PORTRAIT) {
            lv_display_set_rotation(s_lvgl_disp, LV_DISPLAY_ROTATION_0);
            s_applied_orient = QMI8658_ORIENT_PORTRAIT;
        }
        return;
    }

    if (!s_imu) {
        if (s_imu_init_failed) return;
        if (!footholed_orient_init()) return;
    }

    qmi8658_orientation_t o;

    if (qmi8658_orientation(s_imu, &o) != ESP_OK) return;
    if (o == QMI8658_ORIENT_FLAT) return;

    if (o != s_applied_orient) {
        lv_display_rotation_t rotation = orient_to_lv(o);

        lv_display_set_rotation(s_lvgl_disp, rotation);
        s_applied_orient = o;

        ESP_LOGI(TAG,
                "Auto-rotate: IMU=%d, LVGL=%d",
                (int)o, (int)rotation);
    }
}
