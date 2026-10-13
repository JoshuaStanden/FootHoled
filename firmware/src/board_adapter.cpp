/**
 * @file board_adapter.cpp
 * @brief Waveshare ESP32-S3-Touch-LCD-3.5B display and touch initialisation.
 *
 * Registers the AXS15231B QSPI display and its integrated capacitive touch
 * controller with LVGL 9, then turns on the backlight.  All GPIO numbers and
 * the AXS15231B init-command table are sourced verbatim from Waveshare's own
 * 05_lvgl_example BSP (github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5B).
 *
 * IMPORTANT — do not claim this is tested until it has been compiled, flashed
 * and verified on physical hardware.  Until then this file is a faithful port
 * of the vendor BSP into the FootHoled LVGL 9 scaffold; treat all hardware
 * observations as TODO(HW).
 *
 * Verified pin map (from bsp_display.h / bsp_i2c.h in vendor repo):
 *   QSPI LCD
 *     CS    GPIO 12     SCLK  GPIO 5
 *     D0    GPIO 1      D1    GPIO 2
 *     D2    GPIO 3      D3    GPIO 4
 *     RST   not connected
 *     BL    GPIO 6      (LEDC PWM, 5 kHz, 10-bit duty)
 *   I2C touch (AXS15231B integrated, address 0x3B)
 *     SDA   GPIO 8      SCL   GPIO 7
 *     INT   not connected
 *     RST   not connected
 *   Display resolution: 320 x 480 (portrait)
 *   Pixel clock: 40 MHz
 *   Colour depth: 16-bit RGB565
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
#include "esp_lcd_axs15231b.h"
#include "qmi8658.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// The AXS15231B panel is driven as 16-bit RGB565 (bits_per_pixel = 16 below),
// and the draw-buffer sizing uses sizeof(lv_color_t).  Those agree ONLY when
// LVGL is configured for 16-bit colour.  Fail the build loudly if lv_conf.h is
// missing or set to another depth, rather than shipping 32-bit pixels to a
// 16-bit panel (corrupt display) or mis-sizing the DMA buffer.
#if LV_COLOR_DEPTH != 16
#error "FootHoled board_adapter.cpp requires LV_COLOR_DEPTH==16 (RGB565) to match the AXS15231B panel. Check firmware/src/lv_conf.h."
#endif

static const char *TAG = "FootHoled/board";

// ---------------------------------------------------------------------------
// Pin definitions — sourced from Waveshare bsp_display.h and bsp_i2c.h
// ---------------------------------------------------------------------------
#define FH_PIN_LCD_CS    GPIO_NUM_12
#define FH_PIN_LCD_SCLK  GPIO_NUM_5
#define FH_PIN_LCD_D0    GPIO_NUM_1
#define FH_PIN_LCD_D1    GPIO_NUM_2
#define FH_PIN_LCD_D2    GPIO_NUM_3
#define FH_PIN_LCD_D3    GPIO_NUM_4
#define FH_PIN_LCD_RST   GPIO_NUM_NC   // RST not wired on 3.5B
#define FH_PIN_LCD_BL    GPIO_NUM_6

#define FH_PIN_I2C_SDA   GPIO_NUM_8
#define FH_PIN_I2C_SCL   GPIO_NUM_7

// ---------------------------------------------------------------------------
// Display parameters
// ---------------------------------------------------------------------------
#define FH_LCD_H_RES          320
#define FH_LCD_V_RES          480
#define FH_LCD_PCLK_HZ        (40 * 1000 * 1000)
#define FH_SPI_HOST           SPI2_HOST

// LVGL draw-buffer size: 1/10 of the frame so PSRAM is not exhausted.
// Stored in SPIRAM via MALLOC_CAP_SPIRAM so internal SRAM is preserved for
// FreeRTOS stacks and driver DMA descriptors.
#define FH_BUF_LINES          (FH_LCD_V_RES / 10)
#define FH_BUF_SIZE           (FH_LCD_H_RES * FH_BUF_LINES)

// ---------------------------------------------------------------------------
// Backlight PWM
// ---------------------------------------------------------------------------
#define FH_BL_LEDC_TIMER      LEDC_TIMER_0
#define FH_BL_LEDC_MODE       LEDC_LOW_SPEED_MODE
#define FH_BL_LEDC_CHANNEL    LEDC_CHANNEL_0
#define FH_BL_DUTY_BITS       LEDC_TIMER_10_BIT   // 0–1023
#define FH_BL_FREQ_HZ         5000

// Touch I2C address (AXS15231B integrated controller)
#define FH_TOUCH_I2C_ADDR     0x3B

// ---------------------------------------------------------------------------
// AXS15231B panel-init command table
// Copied verbatim from Waveshare bsp_display.c to ensure identical behaviour.
// ---------------------------------------------------------------------------
static const axs15231b_lcd_init_cmd_t s_init_cmds[] = {
    {0xBB, (uint8_t[]){0x00,0x00,0x00,0x00,0x00,0x00,0x5A,0xA5}, 8, 0},
    {0xA0, (uint8_t[]){0xC0,0x10,0x00,0x02,0x00,0x00,0x04,0x3F,0x20,0x05,
                       0x3F,0x3F,0x00,0x00,0x00,0x00,0x00}, 17, 0},
    {0xA2, (uint8_t[]){0x30,0x3C,0x24,0x14,0xD0,0x20,0xFF,0xE0,0x40,0x19,
                       0x80,0x80,0x80,0x20,0xf9,0x10,0x02,0xff,0xff,0xF0,
                       0x90,0x01,0x32,0xA0,0x91,0xE0,0x20,0x7F,0xFF,0x00,
                       0x5A}, 31, 0},
    {0xD0, (uint8_t[]){0xE0,0x40,0x51,0x24,0x08,0x05,0x10,0x01,0x20,0x15,
                       0x42,0xC2,0x22,0x22,0xAA,0x03,0x10,0x12,0x60,0x14,
                       0x1E,0x51,0x15,0x00,0x8A,0x20,0x00,0x03,0x3A,0x12}, 30, 0},
    {0xA3, (uint8_t[]){0xA0,0x06,0xAa,0x00,0x08,0x02,0x0A,0x04,0x04,0x04,
                       0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x00,
                       0x55,0x55}, 22, 0},
    {0xC1, (uint8_t[]){0x31,0x04,0x02,0x02,0x71,0x05,0x24,0x55,0x02,0x00,
                       0x41,0x00,0x53,0xFF,0xFF,0xFF,0x4F,0x52,0x00,0x4F,
                       0x52,0x00,0x45,0x3B,0x0B,0x02,0x0d,0x00,0xFF,0x40}, 30, 0},
    {0xC3, (uint8_t[]){0x00,0x00,0x00,0x50,0x03,0x00,0x00,0x00,0x01,0x80,0x01}, 11, 0},
    {0xC4, (uint8_t[]){0x00,0x24,0x33,0x80,0x00,0xea,0x64,0x32,0xC8,0x64,
                       0xC8,0x32,0x90,0x90,0x11,0x06,0xDC,0xFA,0x00,0x00,
                       0x80,0xFE,0x10,0x10,0x00,0x0A,0x0A,0x44,0x50}, 29, 0},
    {0xC5, (uint8_t[]){0x18,0x00,0x00,0x03,0xFE,0x3A,0x4A,0x20,0x30,0x10,
                       0x88,0xDE,0x0D,0x08,0x0F,0x0F,0x01,0x3A,0x4A,0x20,
                       0x10,0x10,0x00}, 23, 0},
    {0xC6, (uint8_t[]){0x05,0x0A,0x05,0x0A,0x00,0xE0,0x2E,0x0B,0x12,0x22,
                       0x12,0x22,0x01,0x03,0x00,0x3F,0x6A,0x18,0xC8,0x22}, 20, 0},
    {0xC7, (uint8_t[]){0x50,0x32,0x28,0x00,0xa2,0x80,0x8f,0x00,0x80,0xff,
                       0x07,0x11,0x9c,0x67,0xff,0x24,0x0c,0x0d,0x0e,0x0f}, 20, 0},
    {0xC9, (uint8_t[]){0x33,0x44,0x44,0x01}, 4, 0},
    {0xCF, (uint8_t[]){0x2C,0x1E,0x88,0x58,0x13,0x18,0x56,0x18,0x1E,0x68,
                       0x88,0x00,0x65,0x09,0x22,0xC4,0x0C,0x77,0x22,0x44,
                       0xAA,0x55,0x08,0x08,0x12,0xA0,0x08}, 27, 0},
    {0xD5, (uint8_t[]){0x40,0x8E,0x8D,0x01,0x35,0x04,0x92,0x74,0x04,0x92,
                       0x74,0x04,0x08,0x6A,0x04,0x46,0x03,0x03,0x03,0x03,
                       0x82,0x01,0x03,0x00,0xE0,0x51,0xA1,0x00,0x00,0x00}, 30, 0},
    {0xD6, (uint8_t[]){0x10,0x32,0x54,0x76,0x98,0xBA,0xDC,0xFE,0x93,0x00,
                       0x01,0x83,0x07,0x07,0x00,0x07,0x07,0x00,0x03,0x03,
                       0x03,0x03,0x03,0x03,0x00,0x84,0x00,0x20,0x01,0x00}, 30, 0},
    {0xD7, (uint8_t[]){0x03,0x01,0x0b,0x09,0x0f,0x0d,0x1E,0x1F,0x18,0x1d,
                       0x1f,0x19,0x40,0x8E,0x04,0x00,0x20,0xA0,0x1F}, 19, 0},
    {0xD8, (uint8_t[]){0x02,0x00,0x0a,0x08,0x0e,0x0c,0x1E,0x1F,0x18,0x1d,
                       0x1f,0x19}, 12, 0},
    {0xD9, (uint8_t[]){0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,
                       0x1F,0x1F}, 12, 0},
    {0xDD, (uint8_t[]){0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,
                       0x1F,0x1F}, 12, 0},
    {0xDF, (uint8_t[]){0x44,0x73,0x4B,0x69,0x00,0x0A,0x02,0x90}, 8, 0},
    {0xE0, (uint8_t[]){0x3B,0x28,0x10,0x16,0x0c,0x06,0x11,0x28,0x5c,0x21,
                       0x0D,0x35,0x13,0x2C,0x33,0x28,0x0D}, 17, 0},
    {0xE1, (uint8_t[]){0x37,0x28,0x10,0x16,0x0b,0x06,0x11,0x28,0x5C,0x21,
                       0x0D,0x35,0x14,0x2C,0x33,0x28,0x0F}, 17, 0},
    {0xE2, (uint8_t[]){0x3B,0x07,0x12,0x18,0x0E,0x0D,0x17,0x35,0x44,0x32,
                       0x0C,0x14,0x14,0x36,0x3A,0x2F,0x0D}, 17, 0},
    {0xE3, (uint8_t[]){0x37,0x07,0x12,0x18,0x0E,0x0D,0x17,0x35,0x44,0x32,
                       0x0C,0x14,0x14,0x36,0x32,0x2F,0x0F}, 17, 0},
    {0xE4, (uint8_t[]){0x3B,0x07,0x12,0x18,0x0E,0x0D,0x17,0x39,0x44,0x2E,
                       0x0C,0x14,0x14,0x36,0x3A,0x2F,0x0D}, 17, 0},
    {0xE5, (uint8_t[]){0x37,0x07,0x12,0x18,0x0E,0x0D,0x17,0x39,0x44,0x2E,
                       0x0C,0x14,0x14,0x36,0x3A,0x2F,0x0F}, 17, 0},
    {0xA4, (uint8_t[]){0x85,0x85,0x95,0x82,0xAF,0xAA,0xAA,0x80,0x10,0x30,
                       0x40,0x40,0x20,0xFF,0x60,0x30}, 16, 0},
    {0xA4, (uint8_t[]){0x85,0x85,0x95,0x85}, 4, 0},
    {0xBB, (uint8_t[]){0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, 8, 0},
    {0x13, (uint8_t[]){0x00}, 0, 0},
    {0x11, (uint8_t[]){0x00}, 0, 120},  // sleep out + 120 ms delay
    {0x2C, (uint8_t[]){0x00,0x00,0x00,0x00}, 4, 0},
};

// ---------------------------------------------------------------------------
// Module-level handles — owned by this translation unit for the device lifetime
// ---------------------------------------------------------------------------
static esp_lcd_panel_io_handle_t  s_io_handle    = nullptr;
static esp_lcd_panel_handle_t     s_panel_handle = nullptr;
static i2c_master_bus_handle_t    s_i2c_bus      = nullptr;

// Active LVGL display, retained so the auto-rotation service can call
// lv_display_set_rotation() on it. LVGL rotates BOTH rendering and touch-input
// coordinates when the display rotation changes, so touch_read_cb() must NOT
// transform coordinates itself — that would double-rotate.
static lv_display_t *s_lvgl_disp = nullptr;

// QMI8658 IMU handle for display auto-rotation. nullptr when the IMU is absent
// or disabled; auto-rotation is skipped in that case and the UI still works.
static qmi8658_handle_t s_imu = nullptr;

// Last orientation actually applied to the display, so we only call
// lv_display_set_rotation() on an actual change (it forces a full redraw).
static qmi8658_orientation_t s_applied_orient = QMI8658_ORIENT_PORTRAIT;

// Set once we have attempted IMU bring-up and it failed (chip absent), so the
// self-gating poll does not re-probe and re-log a missing IMU every cycle.
static bool s_imu_init_failed = false;

// LVGL draw buffers (allocated once; stored in SPIRAM).  Two buffers enable
// pipelining: LVGL renders the next tile into buf2 while buf1's contents are
// still being DMA'd to the panel, and vice versa.  Safe only because the async
// flush now releases a buffer from colour_trans_done_cb() rather than
// immediately in flush_cb().
static lv_color_t *s_lvgl_buf1 = nullptr;
static lv_color_t *s_lvgl_buf2 = nullptr;

// Scratch buffer for software rotation. The AXS15231B is driven without
// hardware rotation, so when the display is rotated 90/180/270 deg each
// partial tile must be rotated here (per the LVGL rotation docs) before it is
// sent to the panel. Sized to one tile (FH_BUF_SIZE), same as a draw buffer.
// At rotation 0 this buffer is untouched and the fast path copies nothing.
static lv_color_t *s_rotate_buf = nullptr;

// ---------------------------------------------------------------------------
// LVGL 9 flush callback — called from the LVGL task to push pixels to the LCD
//
// esp_lcd_panel_draw_bitmap() on this QSPI panel is ASYNCHRONOUS: it queues a
// DMA transfer and returns before the pixels have actually reached the glass.
// We must NOT call lv_display_flush_ready() here — doing so would tell LVGL the
// draw buffer is free to reuse while DMA is still reading from it (tearing /
// corruption).  Instead LVGL is signalled from colour_trans_done_cb(), which
// the panel IO fires when the transfer genuinely completes.
//
// ROTATION: lv_display_set_rotation() only swaps LVGL's logical resolution and
// rotates touch input — it does NOT rotate the pixels. The AXS15231B is driven
// without hardware rotation, so for any non-zero rotation we rotate each
// partial tile into s_rotate_buf with lv_draw_rotate() before sending it, and
// send the ROTATED area. This follows the LVGL "rotate to another buffer and
// send to a display controller" pattern.
//
// If the transfer fails to even be queued we have no completion event coming,
// so we release the buffer here to avoid hanging the LVGL render task forever.
// ---------------------------------------------------------------------------
static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    lv_area_t out_area = *area;
    uint8_t  *out_px   = px_map;

    lv_display_rotation_t rot = lv_display_get_rotation(disp);
    if (rot != LV_DISPLAY_ROTATION_0 && s_rotate_buf) {
        lv_color_format_t cf = lv_display_get_color_format(disp);
        lv_display_rotate_area(disp, &out_area);  // where the tile lands rotated

        int32_t src_w = lv_area_get_width(area);
        int32_t src_h = lv_area_get_height(area);
        uint32_t src_stride  = lv_draw_buf_width_to_stride(src_w, cf);
        uint32_t dest_stride = lv_draw_buf_width_to_stride(lv_area_get_width(&out_area), cf);

        lv_draw_rotate(px_map, s_rotate_buf, src_w, src_h,
                       src_stride, dest_stride, rot, cf);
        out_px = (uint8_t *)s_rotate_buf;
    }

    esp_err_t err = esp_lcd_panel_draw_bitmap(
        s_panel_handle,
        out_area.x1, out_area.y1,
        out_area.x2 + 1, out_area.y2 + 1,
        out_px
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "draw_bitmap failed: %s — releasing buffer to avoid stall",
                 esp_err_to_name(err));
        // No colour_trans_done will fire for a transfer that never queued,
        // so free the buffer here or the LVGL task blocks indefinitely.
        lv_display_flush_ready(disp);
    }
    // On success: do NOT signal here. colour_trans_done_cb() releases the
    // buffer once DMA has finished reading it.
    //
    // NOTE: when rotating we DMA out of s_rotate_buf while LVGL's own draw
    // buffer is released on completion. Because flush_cb is not re-entered
    // until lv_display_flush_ready() fires, s_rotate_buf is not overwritten
    // mid-transfer. (A single rotate buffer is therefore safe but serialises
    // the pipeline for rotated frames — acceptable; rotation is infrequent.)
}

// ---------------------------------------------------------------------------
// Panel-IO transfer-complete callback — fires from the esp_lcd ISR/task when a
// draw_bitmap DMA transfer finishes.  This is the only safe place to release
// the LVGL draw buffer back for reuse.  Must stay ISR-safe (no logging, no
// blocking); lv_display_flush_ready() is safe to call from here.
// ---------------------------------------------------------------------------
static bool colour_trans_done_cb(esp_lcd_panel_io_handle_t io,
                                 esp_lcd_panel_io_event_data_t *edata,
                                 void *user_ctx)
{
    lv_display_t *disp = (lv_display_t *)user_ctx;
    if (disp) {
        lv_display_flush_ready(disp);
    }
    return false; // no higher-priority task woken
}

// ---------------------------------------------------------------------------
// LVGL 9 touch-read callback — polled by the LVGL indev task
// ---------------------------------------------------------------------------

/**
 * @brief Raw I2C read for the AXS15231B touch controller.
 *
 * The AXS15231B uses a proprietary read protocol: send an 11-byte command,
 * then receive 14 bytes (supports up to 2 touch points).
 *
 * Byte layout of the 14-byte response (from Waveshare bsp_touch.c):
 *   [0]   flags (0xFF = no touch)
 *   [1]   touch count (0–2)
 *   [2]   point 0 x_h  [3]   point 0 x_l
 *   [4]   point 0 y_h  [5]   point 0 y_l
 *   [6]   (unused)
 *   [8]   point 1 x_h  ...  (second touch point follows same layout)
 *
 * x = (data[2] & 0x0F) << 8 | data[3]
 * y = (data[4] & 0x0F) << 8 | data[5]
 *
 * The device handle is created once at init (s_touch_dev) and reused to
 * avoid the overhead of add/remove on every poll cycle.
 */

// Persistent touch device handle — created in footholed_board_init()
static i2c_master_dev_handle_t s_touch_dev = nullptr;

static bool read_touch_raw(uint16_t *x_out, uint16_t *y_out)
{
    if (!s_touch_dev) return false;

    // Command sourced verbatim from Waveshare bsp_touch.c
    static const uint8_t READ_CMD[11] = {
        0xb5, 0xab, 0xa5, 0x5a, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00
    };
    uint8_t buf[14] = {};

    esp_err_t ret = i2c_master_transmit_receive(
        s_touch_dev, READ_CMD, sizeof(READ_CMD), buf, sizeof(buf), 50);

    if (ret != ESP_OK) return false;

    // Validation: the only authoritative "is there a touch" signal is the
    // point-count byte buf[1] (bits 0-3). A valid report has 1 or 2 points.
    //
    // NOTE: the previous checks (buf[2]==0, buf[3]<2, buf[5]<2) were wrong and
    // silently dropped large regions of valid touches: buf[2] holds the X high
    // NIBBLE plus event bits, so it is legitimately 0x00 for any touch in the
    // left ~256 px; and buf[3]/buf[5] are the X/Y low bytes, so coordinates
    // ending in .0/.1 are perfectly valid. Those bytes carry no "minimum
    // sentinel" meaning. buf[0]==0xFF is kept only as a defensive no-touch
    // guard; the count byte is what actually decides.
    uint8_t point_count = buf[1] & 0x0F;
    if (buf[0] == 0xFF || point_count < 1 || point_count > 2) {
        return false;
    }

    *x_out = (uint16_t)(((buf[2] & 0x0F) << 8) | buf[3]);
    *y_out = (uint16_t)(((buf[4] & 0x0F) << 8) | buf[5]);
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
// Backlight — LEDC PWM, 50 % duty at init
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
    ch.duty       = 512;   // 50 % of 1023 — TODO(HW): tune after verifying brightness range
    ch.hpoint     = 0;
    ledc_channel_config(&ch);
    ESP_LOGI(TAG, "Backlight on at 50%%");
}

// ---------------------------------------------------------------------------
// footholed_board_init() — the one symbol main.cpp calls
// ---------------------------------------------------------------------------
bool footholed_board_init(void)
{
    ESP_LOGI(TAG, "Initialising Waveshare ESP32-S3-Touch-LCD-3.5B (AXS15231B QSPI)");

    // ------------------------------------------------------------------
    // 1. QSPI bus
    // ------------------------------------------------------------------
    const spi_bus_config_t bus = {
        .sclk_io_num     = FH_PIN_LCD_SCLK,
        .data0_io_num    = FH_PIN_LCD_D0,
        .data1_io_num    = FH_PIN_LCD_D1,
        .data2_io_num    = FH_PIN_LCD_D2,
        .data3_io_num    = FH_PIN_LCD_D3,
        .max_transfer_sz = FH_LCD_H_RES * FH_LCD_V_RES * sizeof(uint16_t),
    };
    if (spi_bus_initialize(FH_SPI_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed");
        return false;
    }

    // ------------------------------------------------------------------
    // 2. Panel IO (QSPI)
    // ------------------------------------------------------------------
    esp_lcd_panel_io_spi_config_t io_cfg =
        AXS15231B_PANEL_IO_QSPI_CONFIG(FH_PIN_LCD_CS, nullptr, nullptr);
    io_cfg.pclk_hz = FH_LCD_PCLK_HZ;

    if (esp_lcd_new_panel_io_spi(
            (esp_lcd_spi_bus_handle_t)FH_SPI_HOST,
            &io_cfg, &s_io_handle) != ESP_OK)
    {
        ESP_LOGE(TAG, "Panel IO create failed");
        return false;
    }

    // ------------------------------------------------------------------
    // 3. Panel driver (AXS15231B) with custom init commands
    // ------------------------------------------------------------------
    axs15231b_vendor_config_t vendor = {};
    vendor.init_cmds      = s_init_cmds;
    vendor.init_cmds_size = sizeof(s_init_cmds) / sizeof(s_init_cmds[0]);
    vendor.flags.use_qspi_interface = 1;

    esp_lcd_panel_dev_config_t panel_cfg = {};
    panel_cfg.reset_gpio_num  = FH_PIN_LCD_RST;
    panel_cfg.rgb_ele_order   = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_cfg.bits_per_pixel  = 16;
    panel_cfg.vendor_config   = &vendor;

    if (esp_lcd_new_panel_axs15231b(s_io_handle, &panel_cfg, &s_panel_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Panel create failed");
        return false;
    }

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel_handle));
    // Display stays off until LVGL is ready; turned on after disp registration.
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel_handle, false));

    // ------------------------------------------------------------------
    // 4. I2C bus for touch (also used by IMU, RTC, PMIC)
    // ------------------------------------------------------------------
    const i2c_master_bus_config_t i2c_cfg = {
        .i2c_port      = I2C_NUM_0,
        .sda_io_num    = FH_PIN_I2C_SDA,
        .scl_io_num    = FH_PIN_I2C_SCL,
        .clk_source    = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = { .enable_internal_pullup = true },
    };
    if (i2c_new_master_bus(&i2c_cfg, &s_i2c_bus) != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed");
        return false;
    }

    // Create a persistent I2C device handle for the AXS15231B touch controller.
    // Reused on every poll cycle to avoid add/remove overhead.
    const i2c_device_config_t touch_dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = FH_TOUCH_I2C_ADDR,
        .scl_speed_hz    = 400000,
    };
    if (i2c_master_bus_add_device(s_i2c_bus, &touch_dev_cfg, &s_touch_dev) != ESP_OK) {
        // Non-fatal: touch read will return false on every poll, UI still works.
        ESP_LOGW(TAG, "Touch device add failed — touch disabled");
        s_touch_dev = nullptr;
    }

    // ------------------------------------------------------------------
    // 5. Register LVGL 9 display
    // ------------------------------------------------------------------
    lv_display_t *lvgl_disp = lv_display_create(FH_LCD_H_RES, FH_LCD_V_RES);
    if (!lvgl_disp) {
        ESP_LOGE(TAG, "lv_display_create failed");
        return false;
    }
    s_lvgl_disp = lvgl_disp;   // retained for the auto-rotation service

    // Register the panel-IO transfer-complete callback now that we have a
    // display to signal.  colour_trans_done_cb() calls lv_display_flush_ready()
    // when each DMA transfer finishes, which is what makes the async flush in
    // flush_cb() correct.  user_ctx carries the display handle.
    const esp_lcd_panel_io_callbacks_t io_cbs = {
        .on_color_trans_done = colour_trans_done_cb,
    };
    if (esp_lcd_panel_io_register_event_callbacks(
            s_io_handle, &io_cbs, lvgl_disp) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register panel-IO transfer-done callback");
        return false;
    }

    // Allocate two draw buffers from SPIRAM (8 MB available via AXP2101-backed
    // PSRAM).  Dual buffers let LVGL pipeline rendering against DMA transfer.
    s_lvgl_buf1 = (lv_color_t *)heap_caps_malloc(
        FH_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    s_lvgl_buf2 = (lv_color_t *)heap_caps_malloc(
        FH_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    // Rotation scratch buffer (same tile size). Only used when the display is
    // rotated; allocating it up front keeps flush_cb allocation-free.
    s_rotate_buf = (lv_color_t *)heap_caps_malloc(
        FH_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
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

    // Turn display on now that LVGL owns it.
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel_handle, true));

    // ------------------------------------------------------------------
    // 6. Register LVGL 9 touch input device
    // ------------------------------------------------------------------
    lv_indev_t *indev = lv_indev_create();
    if (indev) {
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, touch_read_cb);
    } else {
        // Touch failure is non-fatal: the UI can run without touch input.
        ESP_LOGW(TAG, "Could not create LVGL touch indev");
    }

    // ------------------------------------------------------------------
    // 7. Backlight on
    // ------------------------------------------------------------------
    backlight_init();

    ESP_LOGI(TAG, "Board init complete — display %dx%d, touch %s",
             FH_LCD_H_RES, FH_LCD_V_RES,
             indev ? "registered" : "UNAVAILABLE");

    // TODO(HW): verify on physical hardware — do not treat as working until
    // compiled, flashed, and tested on the actual Waveshare 3.5B board.
    return true;
}


// ===========================================================================
// Display auto-rotation service (QMI8658 IMU)
// ---------------------------------------------------------------------------
// footholed_orient_init() brings up the IMU on the shared I2C0 bus after the
// display exists. footholed_orient_poll() is called periodically from the main
// loop; it reads the gravity vector, maps it to one of the four display
// rotations, and applies it to the LVGL display only when it changes.
//
// Rotation is applied with lv_display_set_rotation(), which swaps LVGL's
// logical resolution and transforms touch-input coordinates automatically.
// The actual PIXEL rotation is done per-tile in flush_cb() via lv_draw_rotate()
// (the AXS15231B is not driven with hardware rotation), so touch handling needs
// no change here — LVGL already rotates the input points.
// ===========================================================================

// Map our orientation enum onto LVGL's rotation enum.
static lv_display_rotation_t orient_to_lv(qmi8658_orientation_t o)
{
    switch (o) {
        case QMI8658_ORIENT_PORTRAIT:         return LV_DISPLAY_ROTATION_0;
        case QMI8658_ORIENT_LANDSCAPE_LEFT:   return LV_DISPLAY_ROTATION_90;
        case QMI8658_ORIENT_PORTRAIT_FLIPPED: return LV_DISPLAY_ROTATION_180;
        case QMI8658_ORIENT_LANDSCAPE_RIGHT:  return LV_DISPLAY_ROTATION_270;
        default:                              return LV_DISPLAY_ROTATION_0;
    }
}

bool footholed_orient_init(void)
{
    if (!s_i2c_bus) {
        ESP_LOGW(TAG, "Auto-rotate: I2C bus not ready, IMU not started");
        return false;
    }
    // Try the primary address first, then the alternate (SA0 strap varies by
    // board revision). TODO(HW): confirm which address the real board uses.
    esp_err_t err = qmi8658_init(s_i2c_bus, QMI8658_I2C_ADDR_LOW, &s_imu);
    if (err != ESP_OK) {
        err = qmi8658_init(s_i2c_bus, QMI8658_I2C_ADDR_HIGH, &s_imu);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Auto-rotate: QMI8658 not found — auto-rotation disabled");
        s_imu = nullptr;
        s_imu_init_failed = true;   // don't re-probe/re-log every poll
        return false;   // non-fatal: UI runs without auto-rotation
    }
    s_imu_init_failed = false;
    ESP_LOGI(TAG, "Auto-rotate: IMU ready");
    return true;
}

void footholed_orient_poll(void)
{
    if (!s_lvgl_disp) return;

    // Honour the live setting so the Settings toggle takes effect without a
    // reboot. When auto-rotation is turned OFF, snap back to upright portrait
    // once (so the UI is not left stuck in a rotated pose) and stop polling.
    if (!footholed_settings_get_auto_rotate()) {
        if (s_applied_orient != QMI8658_ORIENT_PORTRAIT) {
            lv_display_set_rotation(s_lvgl_disp, LV_DISPLAY_ROTATION_0);
            s_applied_orient = QMI8658_ORIENT_PORTRAIT;
        }
        return;
    }

    // Enabled but never brought up (toggled on at runtime after boot, or the
    // first poll): lazily initialise the IMU now. Non-fatal if absent, and we
    // only try once — s_imu_init_failed stops a missing IMU being re-probed.
    if (!s_imu) {
        if (s_imu_init_failed) return;
        if (!footholed_orient_init()) return;
    }

    qmi8658_orientation_t o;
    if (qmi8658_orientation(s_imu, &o) != ESP_OK) return;

    // FLAT (or an ambiguous diagonal) means "keep what we have": do nothing so
    // the screen holds its last orientation instead of snapping to a guess.
    if (o == QMI8658_ORIENT_FLAT) return;

    if (o != s_applied_orient) {
        lv_display_set_rotation(s_lvgl_disp, orient_to_lv(o));
        s_applied_orient = o;
        ESP_LOGI(TAG, "Auto-rotate: orientation -> %d", (int)o);
    }
}
