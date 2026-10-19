/**
 * @file axp2101.cpp
 * @brief Minimal AXP2101 PMU reader (battery %, charge status). See axp2101.h.
 *
 * Reads the AXP2101 over I2C0 at 0x34. Deliberately small: presence + the
 * fuel-gauge battery level (reg 0xA4) + charge status (Status2 reg 0x01).
 * Register map from the X-Powers AXP2101 datasheet V1.0.
 */
#include "axp2101.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"   // pdMS_TO_TICKS
#include "freertos/task.h"
#include <stdlib.h>

namespace {
const char *TAG = "FootHoled/axp2101";
}

struct axp2101_dev_s {
    i2c_master_dev_handle_t i2c_dev;
    uint8_t addr;
};

// Read one 8-bit register.
static esp_err_t reg_read8(axp2101_handle_t h, uint8_t reg, uint8_t *val) {
    return i2c_master_transmit_receive(h->i2c_dev, &reg, 1, val, 1,
                                       pdMS_TO_TICKS(50));
}

esp_err_t axp2101_init(i2c_master_bus_handle_t bus, uint8_t addr,
                       axp2101_handle_t *handle)
{
    ESP_RETURN_ON_FALSE(bus && handle, ESP_ERR_INVALID_ARG, TAG, "null arg");

    axp2101_handle_t h = (axp2101_handle_t)calloc(1, sizeof(struct axp2101_dev_s));
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
        ESP_LOGE(TAG, "add I2C device 0x%02X failed: %s", addr, esp_err_to_name(ret));
        free(h);
        return ret;
    }

    // Presence check: a readable chip-ID register means the PMU ACKs. We don't
    // hard-match a specific ID value (vendor silicon revisions vary); any
    // successful read at 0x34 confirms the AXP2101 is on the bus.
    uint8_t id = 0;
    ret = reg_read8(h, AXP2101_REG_CHIP_ID, &id);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "AXP2101 not present at 0x%02X (%s)", addr, esp_err_to_name(ret));
        i2c_master_bus_rm_device(h->i2c_dev);
        free(h);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "AXP2101 present at 0x%02X (chip id 0x%02X)", addr, id);
    *handle = h;
    return ESP_OK;
}

void axp2101_deinit(axp2101_handle_t handle)
{
    if (!handle) return;
    if (handle->i2c_dev) i2c_master_bus_rm_device(handle->i2c_dev);
    free(handle);
}

esp_err_t axp2101_get_battery_percent(axp2101_handle_t handle, uint8_t *pct)
{
    ESP_RETURN_ON_FALSE(handle && pct, ESP_ERR_INVALID_ARG, TAG, "null arg");
    uint8_t v = 0;
    ESP_RETURN_ON_ERROR(reg_read8(handle, AXP2101_REG_BATT_PERCENT, &v),
                        TAG, "batt%% read failed");
    // The fuel gauge returns 0..100 when enabled; 0xFF (or >100) means the
    // gauge is off / no battery / not yet converged.
    if (v > 100) return ESP_ERR_INVALID_RESPONSE;
    *pct = v;
    return ESP_OK;
}

esp_err_t axp2101_get_charge_state(axp2101_handle_t handle,
                                   axp2101_charge_state_t *state)
{
    ESP_RETURN_ON_FALSE(handle && state, ESP_ERR_INVALID_ARG, TAG, "null arg");
    uint8_t s2 = 0;
    ESP_RETURN_ON_ERROR(reg_read8(handle, AXP2101_REG_STATUS2, &s2),
                        TAG, "status2 read failed");
    switch (s2 & 0x07) {
        case 0: *state = AXP2101_CHG_TRICKLE;          break;
        case 1: *state = AXP2101_CHG_PRECHARGE;        break;
        case 2: *state = AXP2101_CHG_CONSTANT_CURRENT; break;
        case 3: *state = AXP2101_CHG_CONSTANT_VOLTAGE; break;
        case 4: *state = AXP2101_CHG_DONE;             break;
        case 5: *state = AXP2101_CHG_NOT_CHARGING;     break;
        default: *state = AXP2101_CHG_UNKNOWN;         break;
    }
    return ESP_OK;
}

const char *axp2101_charge_state_text(axp2101_charge_state_t s)
{
    switch (s) {
        case AXP2101_CHG_TRICKLE:          return "trickle";
        case AXP2101_CHG_PRECHARGE:        return "pre-charge";
        case AXP2101_CHG_CONSTANT_CURRENT: return "charging (CC)";
        case AXP2101_CHG_CONSTANT_VOLTAGE: return "charging (CV)";
        case AXP2101_CHG_DONE:             return "charged";
        case AXP2101_CHG_NOT_CHARGING:     return "not charging";
        default:                           return "unknown";
    }
}
