/**
 * @file ina228.cpp
 * @brief INA228 power/energy/charge monitor driver implementation.
 *
 * Register addresses and LSB values from TI SBOS925C datasheet.
 * SHUNT_CAL formula: SHUNT_CAL = 13107200 × CURRENT_LSB × R_shunt (ADCRANGE=0)
 * CURRENT_LSB = MAX_CURRENT / 2^19
 *
 * TODO(HW): none of these functions have been tested on physical hardware.
 *           Mark each function [HW verified] after validation.
 */

#include "ina228.h"
#include "esp_log.h"
#include "esp_check.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "FootHoled/ina228";

// ---------------------------------------------------------------------------
// Internal device structure
// ---------------------------------------------------------------------------
struct ina228_dev_s {
    i2c_master_dev_handle_t i2c_dev;
    uint8_t  addr;
    float    current_lsb_ma;  ///< Calculated CURRENT_LSB in milliamps
    float    power_lsb_mw;    ///< 3.2 × current_lsb_ma
};

// ---------------------------------------------------------------------------
// Internal I2C helpers
// ---------------------------------------------------------------------------

/**
 * @brief Write a 16-bit value to a register (big-endian, MSB first).
 */
static esp_err_t reg_write16(ina228_handle_t h, uint8_t reg, uint16_t val) {
    uint8_t buf[3] = {reg, (uint8_t)(val >> 8), (uint8_t)(val & 0xFF)};
    return i2c_master_transmit(h->i2c_dev, buf, 3, pdMS_TO_TICKS(50));
}

/**
 * @brief Read a 16-bit register (big-endian).
 */
static esp_err_t reg_read16(ina228_handle_t h, uint8_t reg, uint16_t *val) {
    uint8_t buf[2] = {};
    ESP_RETURN_ON_ERROR(
        i2c_master_transmit_receive(h->i2c_dev, &reg, 1, buf, 2, pdMS_TO_TICKS(50)),
        TAG, "read16 failed");
    *val = ((uint16_t)buf[0] << 8) | buf[1];
    return ESP_OK;
}

/**
 * @brief Read a 24-bit register (big-endian, 3 bytes).
 *
 * INA228 stores VBUS, VSHUNT, CURRENT, POWER as 24-bit values.
 * Data is in bits[23:4]; lower 4 bits are reserved.
 */
static esp_err_t reg_read24(ina228_handle_t h, uint8_t reg, int32_t *val,
                              bool is_signed) {
    uint8_t buf[3] = {};
    ESP_RETURN_ON_ERROR(
        i2c_master_transmit_receive(h->i2c_dev, &reg, 1, buf, 3, pdMS_TO_TICKS(50)),
        TAG, "read24 failed");

    // Reconstruct 24-bit value; data is in bits[23:4]
    int32_t raw = ((int32_t)buf[0] << 16) | ((int32_t)buf[1] << 8) | buf[2];

    if (is_signed) {
        // Sign-extend from 24-bit to 32-bit
        if (raw & 0x800000) raw |= (int32_t)0xFF000000;
        // Arithmetic right-shift by 4 to align data bits
        *val = raw >> 4;
    } else {
        *val = (raw >> 4) & 0x0FFFFF;  // 20 meaningful bits
    }
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

esp_err_t ina228_init(i2c_master_bus_handle_t bus, uint8_t addr,
                       ina228_handle_t *handle)
{
    ESP_RETURN_ON_FALSE(bus && handle, ESP_ERR_INVALID_ARG, TAG, "null arg");

    ina228_handle_t h = (ina228_handle_t)calloc(1, sizeof(struct ina228_dev_s));
    ESP_RETURN_ON_FALSE(h, ESP_ERR_NO_MEM, TAG, "alloc failed");
    h->addr = addr;

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = 400000,
        .scl_wait_us = 0,
        .flags = {},
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &h->i2c_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device at 0x%02X: %s",
                 addr, esp_err_to_name(ret));
        free(h);
        return ret;
    }

    // ------------------------------------------------------------------
    // Verify chip identity
    // ------------------------------------------------------------------
    uint16_t mfr_id = 0, dev_id = 0;
    ret = reg_read16(h, INA228_REG_MANUFACTURER, &mfr_id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "INA228 not found at 0x%02X", addr);
        i2c_master_bus_rm_device(h->i2c_dev);
        free(h);
        return ESP_ERR_NOT_FOUND;
    }
    reg_read16(h, INA228_REG_DEVICE_ID, &dev_id);

    if (mfr_id != INA228_MANUFACTURER_ID ||
        (dev_id & INA228_DEVICE_ID_MASK) != INA228_DEVICE_ID_EXPECT) {
        ESP_LOGW(TAG, "Unexpected IDs: MFR=0x%04X DEV=0x%04X — "
                      "proceeding anyway (TODO(HW): verify on real hardware)",
                 mfr_id, dev_id);
        // Non-fatal: breakout boards can have different revision bits.
    } else {
        ESP_LOGI(TAG, "INA228 confirmed at 0x%02X (MFR=0x%04X DEV=0x%04X)",
                 addr, mfr_id, dev_id);
    }

    // ------------------------------------------------------------------
    // Calculate and write SHUNT_CAL
    //
    // CURRENT_LSB = MAX_CURRENT_A / 2^19
    // SHUNT_CAL   = 13107200 × CURRENT_LSB × R_shunt  (ADCRANGE=0)
    //
    // Example: 2A max, 10 mΩ shunt:
    //   CURRENT_LSB = 2.0 / 524288 ≈ 3.815e-6 A = 3.815 µA
    //   SHUNT_CAL   = 13107200 × 3.815e-6 × 0.01 ≈ 500 (0x01F4)
    // ------------------------------------------------------------------
    const float current_lsb_a = INA228_MAX_CURRENT_A / 524288.0f;  // 2^19
    h->current_lsb_ma = current_lsb_a * 1000.0f;
    h->power_lsb_mw   = 3.2f * h->current_lsb_ma;

    uint16_t shunt_cal = (uint16_t)(13107200.0f * current_lsb_a *
                                    INA228_SHUNT_RESISTANCE_OHM);
    ESP_LOGI(TAG, "SHUNT_CAL=0x%04X (CURRENT_LSB=%.4f µA, R_shunt=%.4f Ω)",
             shunt_cal, current_lsb_a * 1e6f, INA228_SHUNT_RESISTANCE_OHM);

    ret = reg_write16(h, INA228_REG_SHUNT_CAL, shunt_cal);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write SHUNT_CAL: %s", esp_err_to_name(ret));
        i2c_master_bus_rm_device(h->i2c_dev);
        free(h);
        return ret;
    }

    // ADC_CONFIG: default 0xFB68 = continuous mode, 1052µs conversion,
    // 16× averaging — fine for a battery monitor with slow-changing values.
    // Leave at default; TODO(HW): tune averaging for power-consumption tradeoff.

    *handle = h;
    return ESP_OK;
}

void ina228_deinit(ina228_handle_t handle) {
    if (!handle) return;
    i2c_master_bus_rm_device(handle->i2c_dev);
    free(handle);
}

// ---------------------------------------------------------------------------
// Measurements
// ---------------------------------------------------------------------------

esp_err_t ina228_get_bus_voltage_mv(ina228_handle_t handle, float *mv)
{
    ESP_RETURN_ON_FALSE(handle && mv, ESP_ERR_INVALID_ARG, TAG, "null arg");
    int32_t raw = 0;
    ESP_RETURN_ON_ERROR(
        reg_read24(handle, INA228_REG_VBUS, &raw, false), TAG, "vbus read");
    // LSB = 195.3125 µV → convert to mV
    *mv = (float)raw * INA228_VBUS_LSB_UV / 1000.0f;
    return ESP_OK;
}

esp_err_t ina228_get_current_ma(ina228_handle_t handle, float *ma)
{
    ESP_RETURN_ON_FALSE(handle && ma, ESP_ERR_INVALID_ARG, TAG, "null arg");
    int32_t raw = 0;
    ESP_RETURN_ON_ERROR(
        reg_read24(handle, INA228_REG_CURRENT, &raw, true), TAG, "current read");
    *ma = (float)raw * handle->current_lsb_ma;
    return ESP_OK;
}

esp_err_t ina228_get_power_mw(ina228_handle_t handle, float *mw)
{
    ESP_RETURN_ON_FALSE(handle && mw, ESP_ERR_INVALID_ARG, TAG, "null arg");
    int32_t raw = 0;
    ESP_RETURN_ON_ERROR(
        reg_read24(handle, INA228_REG_POWER, &raw, false), TAG, "power read");
    *mw = (float)raw * handle->power_lsb_mw;
    return ESP_OK;
}

esp_err_t ina228_get_temperature_c(ina228_handle_t handle, float *degc)
{
    ESP_RETURN_ON_FALSE(handle && degc, ESP_ERR_INVALID_ARG, TAG, "null arg");
    uint16_t raw16 = 0;
    ESP_RETURN_ON_ERROR(
        reg_read16(handle, INA228_REG_DIETEMP, &raw16), TAG, "temp read");
    // 16-bit signed, bits[15:4], LSB = 7.8125 m°C → arithmetic shift right 4
    int16_t raw = (int16_t)raw16;
    raw >>= 4;  // keep sign via arithmetic shift
    *degc = (float)raw * INA228_TEMP_LSB_MC / 1000.0f;
    return ESP_OK;
}

esp_err_t ina228_reset_accumulators(ina228_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "null handle");
    uint16_t cfg = 0;
    ESP_RETURN_ON_ERROR(
        reg_read16(handle, INA228_REG_CONFIG, &cfg), TAG, "config read");
    cfg |= (1u << 14);  // RSTACC bit
    ESP_RETURN_ON_ERROR(
        reg_write16(handle, INA228_REG_CONFIG, cfg), TAG, "config write");
    ESP_LOGI(TAG, "Energy and charge accumulators reset");
    return ESP_OK;
}
