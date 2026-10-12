/**
 * @file ina228.h
 * @brief INA228 85V, 20-bit power/energy/charge monitor driver for FootHoled H1.
 *
 * The INA228 measures shunt voltage, bus voltage, die temperature, current,
 * power, energy, and charge. Used to show battery voltage, current draw, and
 * estimated remaining capacity in the FootHoled UI.
 *
 * Hardware: Texas Instruments INA228, I2C address 0x40 (A0=A1=GND).
 * Bus: I2C0, shared with touch, IMU, RTC, PMIC (SDA GPIO 8, SCL GPIO 7).
 *
 * Shunt resistor: TODO(HW) — confirm value on physical board.
 *                 A 10 mΩ shunt gives ~1 µA CURRENT_LSB at 1A max.
 *
 * Reference: Texas Instruments SBOS925C (INA228 datasheet).
 *
 * TODO(HW): Verify I2C address on physical board.
 * TODO(HW): Measure shunt resistor value and update SHUNT_RESISTANCE_OHM.
 * TODO(HW): Write SHUNT_CAL register before reading CURRENT or POWER.
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
// Register addresses — from INA228 datasheet Table 7-6
// ---------------------------------------------------------------------------
#define INA228_REG_CONFIG        0x00  ///< Configuration (RST, RSTACC, CONVDLY, ADCRANGE)
#define INA228_REG_ADC_CONFIG    0x01  ///< ADC mode, conversion time, averaging count
#define INA228_REG_SHUNT_CAL     0x02  ///< Shunt calibration — MUST set before reading CURRENT/POWER
#define INA228_REG_SHUNT_TEMPCO  0x03  ///< Shunt temperature coefficient (ppm/°C)
#define INA228_REG_VSHUNT        0x04  ///< Differential shunt voltage, 24-bit signed (bits[23:4])
#define INA228_REG_VBUS          0x05  ///< Bus voltage, 24-bit unsigned (bits[23:4])
#define INA228_REG_DIETEMP       0x06  ///< Die temperature, 16-bit signed
#define INA228_REG_CURRENT       0x07  ///< Calculated current, 24-bit signed (bits[23:4])
#define INA228_REG_POWER         0x08  ///< Calculated power, 24-bit unsigned
#define INA228_REG_ENERGY        0x09  ///< Accumulated energy, 40-bit unsigned
#define INA228_REG_CHARGE        0x0A  ///< Accumulated charge, 40-bit signed
#define INA228_REG_DIAG_ALRT     0x0B  ///< Diagnostic flags and alert enables
#define INA228_REG_SOVL          0x0C  ///< Shunt over-voltage limit
#define INA228_REG_SUVL          0x0D  ///< Shunt under-voltage limit
#define INA228_REG_BOVL          0x0E  ///< Bus over-voltage limit
#define INA228_REG_BUVL          0x0F  ///< Bus under-voltage limit
#define INA228_REG_TEMP_LIMIT    0x10  ///< Temperature over-limit threshold
#define INA228_REG_PWR_LIMIT     0x11  ///< Power over-limit threshold
#define INA228_REG_MANUFACTURER  0x3E  ///< Manufacturer ID (read: 0x5449 = "TI")
#define INA228_REG_DEVICE_ID     0x3F  ///< Device ID (read: 0x2281, upper 12 bits = die ID)

// Default I2C address (A0=A1=GND)
#define INA228_I2C_ADDR_DEFAULT  0x40

// Expected manufacturer and device ID values for presence check
#define INA228_MANUFACTURER_ID   0x5449  // ASCII "TI"
#define INA228_DEVICE_ID_MASK    0xFFF0  // Upper 12 bits identify INA228
#define INA228_DEVICE_ID_EXPECT  0x2280  // Expected after masking lower 4 (revision) bits

// ---------------------------------------------------------------------------
// Configuration constants
// ---------------------------------------------------------------------------

/**
 * @brief Shunt resistance in ohms.
 *
 * TODO(HW): Measure the physical shunt resistor on the INA228 breakout board.
 * A typical value for a 1A-range system is 0.01 Ω (10 mΩ).
 * Update this before calling ina228_init() for accurate current readings.
 */
#define INA228_SHUNT_RESISTANCE_OHM  0.01f

/**
 * @brief Maximum expected current in amps.
 *
 * Sets the CURRENT_LSB resolution.  CURRENT_LSB = MAX_CURRENT / 2^19.
 * TODO(HW): set to the realistic maximum draw of the complete FootHoled system.
 */
#define INA228_MAX_CURRENT_A  2.0f

// LSB values (fixed — from datasheet)
#define INA228_VBUS_LSB_UV    195.3125f  ///< VBUS LSB in µV (= 0.1953125 mV)
#define INA228_TEMP_LSB_MC    7.8125f    ///< DIETEMP LSB in m°C

// Driver handle — opaque
typedef struct ina228_dev_s *ina228_handle_t;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

/**
 * @brief Initialise the INA228 driver, verify chip presence, and calibrate.
 *
 * Reads MANUFACTURER_ID and DEVICE_ID to confirm the chip is present.
 * Calculates and writes SHUNT_CAL based on INA228_SHUNT_RESISTANCE_OHM
 * and INA228_MAX_CURRENT_A so CURRENT and POWER readings are valid.
 *
 * TODO(HW): verify this returns ESP_OK on real hardware.
 *
 * @param bus       I2C master bus handle from board_adapter (shared I2C0).
 * @param addr      7-bit I2C address (INA228_I2C_ADDR_DEFAULT = 0x40).
 * @param[out] handle  Returned driver handle.
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if chip does not respond or
 *         ID check fails.
 */
esp_err_t ina228_init(i2c_master_bus_handle_t bus, uint8_t addr,
                       ina228_handle_t *handle);

/**
 * @brief Release the driver handle and remove the I2C device.
 */
void ina228_deinit(ina228_handle_t handle);

// ---------------------------------------------------------------------------
// Measurements
// ---------------------------------------------------------------------------

/**
 * @brief Read bus voltage in millivolts.
 *
 * VBUS register: 24-bit unsigned, bits[23:4], LSB = 195.3125 µV.
 * Practical range: 0 – ~16.3 V (covers single and dual 18650 packs).
 *
 * @param handle      Driver handle.
 * @param[out] mv     Bus voltage in millivolts.
 * @return ESP_OK on success.
 */
esp_err_t ina228_get_bus_voltage_mv(ina228_handle_t handle, float *mv);

/**
 * @brief Read current in milliamps.
 *
 * CURRENT register: 24-bit signed, bits[23:4].
 * Requires SHUNT_CAL to be set (done in ina228_init).
 * Positive = current flowing into V+; negative = reverse current.
 *
 * TODO(HW): verify sign convention matches physical wiring.
 *
 * @param handle     Driver handle.
 * @param[out] ma    Current in milliamps.
 * @return ESP_OK on success.
 */
esp_err_t ina228_get_current_ma(ina228_handle_t handle, float *ma);

/**
 * @brief Read power in milliwatts.
 *
 * POWER register: 24-bit unsigned. LSB = 3.2 × CURRENT_LSB.
 *
 * @param handle     Driver handle.
 * @param[out] mw    Power in milliwatts.
 * @return ESP_OK on success.
 */
esp_err_t ina228_get_power_mw(ina228_handle_t handle, float *mw);

/**
 * @brief Read die temperature in degrees Celsius.
 *
 * DIETEMP register: 16-bit signed, LSB = 7.8125 m°C.
 * Useful for thermal monitoring of the power path.
 *
 * @param handle      Driver handle.
 * @param[out] degc   Temperature in °C.
 * @return ESP_OK on success.
 */
esp_err_t ina228_get_temperature_c(ina228_handle_t handle, float *degc);

/**
 * @brief Reset the energy and charge accumulators.
 *
 * Sets RSTACC bit in CONFIG register. Use before starting a new measurement
 * session or after a full charge.
 *
 * @param handle  Driver handle.
 * @return ESP_OK on success.
 */
esp_err_t ina228_reset_accumulators(ina228_handle_t handle);

#ifdef __cplusplus
}
#endif
