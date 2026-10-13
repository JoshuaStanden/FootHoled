/**
 * @file mcp23017.cpp
 * @brief MCP23017 16-bit I2C GPIO expander driver implementation.
 *
 * All register addresses from Microchip DS20001952C Table 3-3, BANK=0 layout.
 * Device communicates via ESP-IDF new I2C master driver (i2c_master_*).
 *
 * TODO(HW): none of these functions have been tested on physical hardware.
 *           Mark each function [HW verified] in this file after validation.
 */

#include "mcp23017.h"
#include "esp_log.h"
#include "esp_check.h"
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "FootHoled/mcp23017";

// ---------------------------------------------------------------------------
// Internal device structure
// ---------------------------------------------------------------------------
struct mcp23017_dev_s {
    i2c_master_dev_handle_t i2c_dev;
    uint8_t addr;
};

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/** Write one byte to a register. */
static esp_err_t reg_write(mcp23017_handle_t h, uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(h->i2c_dev, buf, 2, pdMS_TO_TICKS(50));
}

/** Read one byte from a register. */
static esp_err_t reg_read(mcp23017_handle_t h, uint8_t reg, uint8_t *val) {
    return i2c_master_transmit_receive(h->i2c_dev, &reg, 1, val, 1,
                                       pdMS_TO_TICKS(50));
}

/** Read-modify-write a single bit in a register. */
static esp_err_t reg_rmw_bit(mcp23017_handle_t h, uint8_t reg,
                               uint8_t bit, bool set) {
    uint8_t cur = 0;
    ESP_RETURN_ON_ERROR(reg_read(h, reg, &cur), TAG, "rmw read failed");
    if (set) cur |=  (1u << bit);
    else     cur &= ~(1u << bit);
    return reg_write(h, reg, cur);
}

/** Return IODIR register address for a port. */
static inline uint8_t iodir_reg(mcp23017_port_t port) {
    return (port == MCP23017_PORT_A) ? MCP23017_REG_IODIRA : MCP23017_REG_IODIRB;
}

/** Return GPPU register address for a port. */
static inline uint8_t gppu_reg(mcp23017_port_t port) {
    return (port == MCP23017_PORT_A) ? MCP23017_REG_GPPUA : MCP23017_REG_GPPUB;
}

/** Return OLAT register address for a port. */
static inline uint8_t olat_reg(mcp23017_port_t port) {
    return (port == MCP23017_PORT_A) ? MCP23017_REG_OLATA : MCP23017_REG_OLATB;
}

/** Return GPIO register address for a port. */
static inline uint8_t gpio_reg(mcp23017_port_t port) {
    return (port == MCP23017_PORT_A) ? MCP23017_REG_GPIOA : MCP23017_REG_GPIOB;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

esp_err_t mcp23017_init(i2c_master_bus_handle_t bus, uint8_t addr,
                         mcp23017_handle_t *handle)
{
    ESP_RETURN_ON_FALSE(bus && handle, ESP_ERR_INVALID_ARG, TAG, "null arg");

    mcp23017_handle_t h = (mcp23017_handle_t)calloc(1, sizeof(struct mcp23017_dev_s));
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

    // Verify the chip is present by writing IOCON (harmless — all bits 0 by default)
    // and reading it back. A non-responding device will return a bus error.
    // TODO(HW): confirm this check works on real hardware; some I2C devices
    // ACK even when absent due to bus pull-ups.
    uint8_t iocon = 0;
    ret = reg_write(h, MCP23017_REG_IOCON, 0x00);  // ensure BANK=0, no mirror
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MCP23017 not found at 0x%02X", addr);
        i2c_master_bus_rm_device(h->i2c_dev);
        free(h);
        return ESP_ERR_NOT_FOUND;
    }
    reg_read(h, MCP23017_REG_IOCON, &iocon);
    ESP_LOGI(TAG, "MCP23017 found at 0x%02X, IOCON=0x%02X", addr, iocon);

    // Power-on state: IODIRA/B = 0xFF (all inputs), OLATA/B = 0x00, GPPUA/B = 0x00.
    // No reset needed; just confirm by reading IODIRA.
    uint8_t iodira = 0;
    reg_read(h, MCP23017_REG_IODIRA, &iodira);
    ESP_LOGD(TAG, "IODIRA=0x%02X (0xFF = all inputs, power-on default)", iodira);

    *handle = h;
    return ESP_OK;
}

void mcp23017_deinit(mcp23017_handle_t handle) {
    if (!handle) return;
    i2c_master_bus_rm_device(handle->i2c_dev);
    free(handle);
}

// ---------------------------------------------------------------------------
// Pin configuration
// ---------------------------------------------------------------------------

esp_err_t mcp23017_set_port_direction(mcp23017_handle_t handle,
                                       mcp23017_port_t port, uint8_t mask)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "null handle");
    return reg_write(handle, iodir_reg(port), mask);
}

esp_err_t mcp23017_set_pin_direction(mcp23017_handle_t handle,
                                      mcp23017_port_t port, uint8_t pin,
                                      mcp23017_dir_t dir)
{
    ESP_RETURN_ON_FALSE(handle && pin < 8, ESP_ERR_INVALID_ARG, TAG, "bad arg");
    // IODIR: 1=input, 0=output — matches mcp23017_dir_t enum values
    return reg_rmw_bit(handle, iodir_reg(port), pin, (bool)dir);
}

esp_err_t mcp23017_set_pullups(mcp23017_handle_t handle,
                                mcp23017_port_t port, uint8_t mask)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "null handle");
    return reg_write(handle, gppu_reg(port), mask);
}

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

esp_err_t mcp23017_write_port(mcp23017_handle_t handle,
                               mcp23017_port_t port, uint8_t value)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "null handle");
    return reg_write(handle, olat_reg(port), value);
}

esp_err_t mcp23017_write_pin(mcp23017_handle_t handle,
                              mcp23017_port_t port, uint8_t pin, bool high)
{
    ESP_RETURN_ON_FALSE(handle && pin < 8, ESP_ERR_INVALID_ARG, TAG, "bad arg");
    return reg_rmw_bit(handle, olat_reg(port), pin, high);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

esp_err_t mcp23017_read_port(mcp23017_handle_t handle,
                              mcp23017_port_t port, uint8_t *val)
{
    ESP_RETURN_ON_FALSE(handle && val, ESP_ERR_INVALID_ARG, TAG, "null arg");
    return reg_read(handle, gpio_reg(port), val);
}

esp_err_t mcp23017_read_pin(mcp23017_handle_t handle,
                             mcp23017_port_t port, uint8_t pin, bool *high)
{
    ESP_RETURN_ON_FALSE(handle && high && pin < 8, ESP_ERR_INVALID_ARG,
                        TAG, "bad arg");
    uint8_t val = 0;
    ESP_RETURN_ON_ERROR(reg_read(handle, gpio_reg(port), &val), TAG, "read failed");
    *high = (val >> pin) & 0x01;
    return ESP_OK;
}
