/**
 * @file qmi8658.cpp
 * @brief QMI8658 6-axis IMU driver implementation (see qmi8658.h).
 *
 * TODO(HW): none of this is compiled or verified on hardware yet — no ESP-IDF
 *           toolchain on the authoring host. Treat as a bring-up draft.
 */
#include "qmi8658.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdlib.h>

static const char *TAG = "FootHoled/qmi8658";

// ---------------------------------------------------------------------------
// Orientation tuning
// ---------------------------------------------------------------------------
// Gravity is ~1 g. We only switch orientation when the dominant in-plane axis
// clearly exceeds the other, AND the device is not lying too flat. These two
// thresholds give a deadzone that prevents flip-flopping on a boundary.
//
//   ORIENT_MIN_G   : the dominant in-plane axis must read at least this (g)
//                    before we trust it — rejects a near-flat device.
//   ORIENT_MARGIN_G: the dominant axis must beat the other in-plane axis by
//                    this margin — rejects a 45-degree diagonal hold.
#define ORIENT_MIN_G      0.40f
#define ORIENT_MARGIN_G   0.15f

// Opaque driver state
struct qmi8658_dev_s {
    i2c_master_dev_handle_t dev;
};

// ---------------------------------------------------------------------------
// Low-level register helpers
// ---------------------------------------------------------------------------
static esp_err_t reg_write(qmi8658_handle_t h, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(h->dev, buf, sizeof(buf), 100);
}

static esp_err_t reg_read(qmi8658_handle_t h, uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(h->dev, &reg, 1, val, 1, 100);
}

// Burst-read `len` bytes starting at `reg`.
static esp_err_t reg_read_burst(qmi8658_handle_t h, uint8_t reg,
                                 uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(h->dev, &reg, 1, buf, len, 100);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
esp_err_t qmi8658_init(i2c_master_bus_handle_t bus, uint8_t addr,
                        qmi8658_handle_t *out_handle)
{
    if (!bus || !out_handle) return ESP_ERR_INVALID_ARG;
    *out_handle = nullptr;

    qmi8658_handle_t h = (qmi8658_handle_t)calloc(1, sizeof(struct qmi8658_dev_s));
    if (!h) return ESP_ERR_NO_MEM;

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = 400000,
        .scl_wait_us = 0,
        .flags = {},
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &h->dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c add device (0x%02X) failed: %s", addr, esp_err_to_name(err));
        free(h);
        return err;
    }

    // Soft reset, then let the device settle before configuring.
    reg_write(h, QMI8658_REG_RESET, QMI8658_RESET_CMD);
    vTaskDelay(pdMS_TO_TICKS(15));

    // Presence check — WHO_AM_I must read 0x05.
    uint8_t who = 0;
    err = reg_read(h, QMI8658_REG_WHO_AM_I, &who);
    if (err != ESP_OK || who != QMI8658_WHO_AM_I_EXPECT) {
        ESP_LOGE(TAG, "WHO_AM_I check failed (read 0x%02X, want 0x%02X, err %s)",
                 who, QMI8658_WHO_AM_I_EXPECT,
                 err == ESP_OK ? "none" : esp_err_to_name(err));
        i2c_master_bus_rm_device(h->dev);
        free(h);
        return ESP_ERR_NOT_FOUND;
    }

    // CTRL1: enable address auto-increment for burst reads, 2-wire SPI off
    // (I2C mode). Datasheet default bit layout; 0x40 sets ADDR_AI.
    reg_write(h, QMI8658_REG_CTRL1, 0x40);

    // CTRL2: accelerometer +/-2 g full scale, ~250 Hz ODR. Low ODR is fine —
    // orientation does not need high bandwidth. (aFS=000 -> 2g, aODR=0101.)
    reg_write(h, QMI8658_REG_CTRL2, 0x05);

    // CTRL3: gyroscope modest range (+/-512 dps), matching ODR. Not used for
    // orientation but enabled for future motion features.
    reg_write(h, QMI8658_REG_CTRL3, 0x55);

    // CTRL7: enable accelerometer (bit0) and gyroscope (bit1).
    reg_write(h, QMI8658_REG_CTRL7, 0x03);

    vTaskDelay(pdMS_TO_TICKS(5));

    ESP_LOGI(TAG, "QMI8658 initialised at 0x%02X (WHO_AM_I 0x%02X)", addr, who);
    *out_handle = h;
    return ESP_OK;  // TODO(HW): verify on real hardware
}

void qmi8658_deinit(qmi8658_handle_t h)
{
    if (!h) return;
    if (h->dev) i2c_master_bus_rm_device(h->dev);
    free(h);
}

// ---------------------------------------------------------------------------
// Measurements
// ---------------------------------------------------------------------------
static inline int16_t le16(const uint8_t *p)
{
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

esp_err_t qmi8658_read(qmi8658_handle_t h, qmi8658_sample_t *out)
{
    if (!h || !out) return ESP_ERR_INVALID_ARG;

    // Burst-read 12 bytes: AX_L..GZ_H (0x35..0x40).
    uint8_t raw[12] = {};
    esp_err_t err = reg_read_burst(h, QMI8658_REG_AX_L, raw, sizeof(raw));
    if (err != ESP_OK) return err;

    const float acc_lsb = QMI8658_ACC_SENSITIVITY_2G;  // LSB per g
    out->ax = le16(&raw[0]) / acc_lsb;
    out->ay = le16(&raw[2]) / acc_lsb;
    out->az = le16(&raw[4]) / acc_lsb;

    // Gyro scale for +/-512 dps on a 16-bit output = 64 LSB/(deg/s).
    const float gyro_lsb = 64.0f;
    out->gx = le16(&raw[6])  / gyro_lsb;
    out->gy = le16(&raw[8])  / gyro_lsb;
    out->gz = le16(&raw[10]) / gyro_lsb;

    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Orientation classification
// ---------------------------------------------------------------------------
static qmi8658_orientation_t classify_orientation(float ax, float ay)
{
    // =====================================================================
    // AXIS-MAPPING CALIBRATION BLOCK  (TODO(HW): the ONE place to adjust)
    // ---------------------------------------------------------------------
    // Assumption for the natural portrait pose (device upright, USB at the
    // bottom): gravity pulls DOWN, so the accel Y axis reads about -1 g, and
    // the in-plane axes are:
    //     +ax -> screen right
    //     +ay -> screen top
    // If the physical IMU mounting differs on the real board, swap/negate ax
    // and ay HERE (and nowhere else) until the four poses map correctly.
    // =====================================================================
    const float gx = ax;   // in-plane horizontal gravity component
    const float gy = ay;   // in-plane vertical gravity component

    const float agx = fabsf(gx);
    const float agy = fabsf(gy);

    // Deadzone: both in-plane components too small -> device is ~flat.
    if (agx < ORIENT_MIN_G && agy < ORIENT_MIN_G) {
        return QMI8658_ORIENT_FLAT;
    }

    if (agy >= agx) {
        // Vertical axis dominates -> portrait or portrait-flipped.
        if (agy - agx < ORIENT_MARGIN_G) return QMI8658_ORIENT_FLAT; // ambiguous diagonal
        // gy < 0 means gravity pulls toward screen bottom -> upright portrait.
        return (gy < 0.0f) ? QMI8658_ORIENT_PORTRAIT
                           : QMI8658_ORIENT_PORTRAIT_FLIPPED;
    } else {
        // Horizontal axis dominates -> one of the two landscapes.
        if (agx - agy < ORIENT_MARGIN_G) return QMI8658_ORIENT_FLAT;
        // gx > 0 means gravity pulls toward screen right -> rotate so top is
        // on the left edge = landscape-left (90 deg).
        return (gx > 0.0f) ? QMI8658_ORIENT_LANDSCAPE_LEFT
                           : QMI8658_ORIENT_LANDSCAPE_RIGHT;
    }
}

esp_err_t qmi8658_orientation(qmi8658_handle_t h, qmi8658_orientation_t *orientation)
{
    if (!h || !orientation) return ESP_ERR_INVALID_ARG;
    qmi8658_sample_t s;
    esp_err_t err = qmi8658_read(h, &s);
    if (err != ESP_OK) return err;
    *orientation = classify_orientation(s.ax, s.ay);
    return ESP_OK;
}
