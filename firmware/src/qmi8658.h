/**
 * @file qmi8658.h
 * @brief QMI8658 6-axis IMU (accelerometer + gyroscope) driver for FootHoled H1.
 *
 * The QMI8658 provides a 3-axis accelerometer and 3-axis gyroscope. For
 * FootHoled it is used primarily for display auto-rotation: the accelerometer
 * reports the gravity vector, which tells us how the device is being held so
 * the LVGL display can reorient to match. The gyroscope is read as well and
 * exposed for future use (gesture/motion features), but auto-rotation only
 * needs the accelerometer.
 *
 * Hardware: QST QMI8658, I2C address 0x6A (SA0 = GND) or 0x6B (SA0 = VDD).
 * Bus: I2C0, shared with touch (FT6336), RTC (PCF85063), PMIC (AXP2101),
 *      audio (ES8311) — SDA GPIO 8, SCL GPIO 7.
 *
 * Reference: QST QMI8658A datasheet (Rev 1.x), register map section.
 *
 * TODO(HW): verify the I2C address on the physical board (0x6A vs 0x6B).
 * TODO(HW): confirm WHO_AM_I reads 0x05 on the received silicon.
 * TODO(HW): calibrate the accel-axis -> screen-orientation mapping once, on a
 *           real board — the IMU's physical mounting relative to the panel is
 *           not knowable from the schematic alone. See qmi8658_orientation().
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Register addresses — from QMI8658A datasheet register map
// ---------------------------------------------------------------------------
#define QMI8658_REG_WHO_AM_I     0x00  ///< Device identifier (reads 0x05)
#define QMI8658_REG_REVISION     0x01  ///< Revision ID
#define QMI8658_REG_CTRL1        0x02  ///< Serial interface & sensor enable
#define QMI8658_REG_CTRL2        0x03  ///< Accelerometer: ODR + full-scale range
#define QMI8658_REG_CTRL3        0x04  ///< Gyroscope: ODR + full-scale range
#define QMI8658_REG_CTRL5        0x06  ///< Low-pass filter settings
#define QMI8658_REG_CTRL7        0x08  ///< Enable accel (bit 0) / gyro (bit 1)
#define QMI8658_REG_STATUS0      0x2E  ///< Data-ready flags
#define QMI8658_REG_AX_L         0x35  ///< Accel X low byte  (16-bit signed, LE)
#define QMI8658_REG_AX_H         0x36  ///< Accel X high byte
#define QMI8658_REG_AY_L         0x37  ///< Accel Y low byte
#define QMI8658_REG_AY_H         0x38  ///< Accel Y high byte
#define QMI8658_REG_AZ_L         0x39  ///< Accel Z low byte
#define QMI8658_REG_AZ_H         0x3A  ///< Accel Z high byte
#define QMI8658_REG_GX_L         0x3B  ///< Gyro X low byte
#define QMI8658_REG_GX_H         0x3C
#define QMI8658_REG_GY_L         0x3D
#define QMI8658_REG_GY_H         0x3E
#define QMI8658_REG_GZ_L         0x3F
#define QMI8658_REG_GZ_H         0x40
#define QMI8658_REG_RESET        0x60  ///< Soft-reset (write 0xB0)

// I2C addresses
#define QMI8658_I2C_ADDR_LOW     0x6A  ///< SA0 tied low
#define QMI8658_I2C_ADDR_HIGH    0x6B  ///< SA0 tied high

// Expected WHO_AM_I value — presence check
#define QMI8658_WHO_AM_I_EXPECT  0x05

// Soft-reset command written to QMI8658_REG_RESET
#define QMI8658_RESET_CMD        0xB0

// ---------------------------------------------------------------------------
// Accelerometer scale
// ---------------------------------------------------------------------------
// We configure the accel for +/-2 g full scale. With a 16-bit signed output
// that gives a sensitivity of 16384 LSB/g. Orientation only cares about the
// SIGN and RELATIVE magnitude of the axes, so the exact scale is not critical,
// but it is set explicitly for predictable raw values.
#define QMI8658_ACC_SENSITIVITY_2G  16384.0f  ///< LSB per g at +/-2 g range

// ---------------------------------------------------------------------------
// Orientation result
// ---------------------------------------------------------------------------
/**
 * @brief Physical orientation of the device, expressed as the LVGL display
 *        rotation that makes the UI appear upright.
 *
 * These values map directly onto lv_display_rotation_t:
 *   PORTRAIT          -> LV_DISPLAY_ROTATION_0
 *   LANDSCAPE_LEFT    -> LV_DISPLAY_ROTATION_90
 *   PORTRAIT_FLIPPED  -> LV_DISPLAY_ROTATION_180
 *   LANDSCAPE_RIGHT   -> LV_DISPLAY_ROTATION_270
 *
 * FLAT is returned when the device is lying face-up/down and no in-plane
 * gravity component dominates; the caller should keep the previous rotation
 * in that case rather than snapping to a guess.
 */
typedef enum {
    QMI8658_ORIENT_PORTRAIT         = 0,  ///< Natural upright (rotation 0)
    QMI8658_ORIENT_LANDSCAPE_LEFT   = 1,  ///< Rotated 90 deg
    QMI8658_ORIENT_PORTRAIT_FLIPPED = 2,  ///< Upside down (rotation 180)
    QMI8658_ORIENT_LANDSCAPE_RIGHT  = 3,  ///< Rotated 270 deg
    QMI8658_ORIENT_FLAT             = 4,  ///< Face up/down — hold previous
} qmi8658_orientation_t;

// Driver handle — opaque
typedef struct qmi8658_dev_s *qmi8658_handle_t;

// Raw / scaled sample
typedef struct {
    float ax, ay, az;  ///< Accelerometer, in g
    float gx, gy, gz;  ///< Gyroscope, in deg/s
} qmi8658_sample_t;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

/**
 * @brief Initialise the QMI8658 driver, verify presence, enable accel + gyro.
 *
 * Performs a soft reset, confirms WHO_AM_I == 0x05, configures the
 * accelerometer for +/-2 g and the gyroscope for a modest range, and enables
 * both sensors via CTRL7.
 *
 * TODO(HW): verify this returns ESP_OK on real hardware.
 *
 * @param bus          Shared I2C0 master bus handle from board_adapter.
 * @param addr         7-bit I2C address (QMI8658_I2C_ADDR_LOW / _HIGH).
 * @param[out] handle  Returned driver handle.
 * @return ESP_OK on success; ESP_ERR_NOT_FOUND if the chip does not respond or
 *         the WHO_AM_I check fails.
 */
esp_err_t qmi8658_init(i2c_master_bus_handle_t bus, uint8_t addr,
                        qmi8658_handle_t *handle);

/**
 * @brief Release the driver handle and remove the I2C device.
 */
void qmi8658_deinit(qmi8658_handle_t handle);

// ---------------------------------------------------------------------------
// Measurements
// ---------------------------------------------------------------------------

/**
 * @brief Read one accelerometer + gyroscope sample.
 *
 * Accel values are returned in g, gyro values in deg/s. For auto-rotation only
 * the accelerometer fields are consulted.
 *
 * @param handle     Driver handle.
 * @param[out] out   Filled with the latest sample.
 * @return ESP_OK on success.
 */
esp_err_t qmi8658_read(qmi8658_handle_t handle, qmi8658_sample_t *out);

/**
 * @brief Read the accelerometer and classify the device orientation.
 *
 * Reads the gravity vector and decides which of the four screen orientations
 * (or FLAT) the device is in. Applies a deadzone so a near-flat device does
 * not jitter between orientations.
 *
 * NOTE ON AXIS MAPPING (TODO(HW) calibration):
 *   The decision below assumes the IMU is mounted so that +X points to the
 *   right of the screen and +Y points toward the top in the natural portrait
 *   pose. If the physical mounting differs, the ONLY change needed is the
 *   axis-selection / sign in qmi8658.cpp:classify_orientation() — it is kept
 *   as a single clearly-marked block for exactly this reason.
 *
 * @param handle            Driver handle.
 * @param[out] orientation  Classified orientation.
 * @return ESP_OK on success.
 */
esp_err_t qmi8658_orientation(qmi8658_handle_t handle,
                               qmi8658_orientation_t *orientation);

#ifdef __cplusplus
}
#endif
