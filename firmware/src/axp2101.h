/**
 * @file axp2101.h
 * @brief Minimal AXP2101 PMU reader for the FootHoled H1 (battery telemetry).
 *
 * This board's power path is the X-Powers AXP2101 single-cell Li PMU with a
 * built-in fuel gauge ("E-gauge"), on the shared I2C0 bus at address 0x34
 * (SDA GPIO 8 / SCL GPIO 7) — verified against the ESP32-S3-Touch-LCD-3.5
 * (non-B) Rev2.0 schematic. There is NO INA228 on this board.
 *
 * Deliberately minimal: presence check + the two values that are directly
 * readable without ADC calibration or fuel-gauge reconfiguration — the fuel
 * gauge's battery PERCENTAGE (reg 0xA4) and the CHARGE STATUS (Status2 reg
 * 0x01, bits[2:0]). This is enough for a diagnostics line and a future battery
 * indicator; full voltage/current ADC + charge control are left for later and
 * MUST be validated on hardware.
 *
 * TODO(HW): confirm the fuel gauge is enabled by the vendor power-on config so
 * reg 0xA4 returns a meaningful 0..100 (it reads 0xFF/invalid if the gauge is
 * off). If so, enabling BattDetection + the gauge is a follow-up.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AXP2101_I2C_ADDR_DEFAULT  0x34

// Registers (X-Powers AXP2101 datasheet V1.0).
#define AXP2101_REG_STATUS1       0x00  ///< VBUS/battery present + power state
#define AXP2101_REG_STATUS2       0x01  ///< Charge status in bits[2:0]
#define AXP2101_REG_CHIP_ID       0x03  ///< IC_TYPE (read for logging; presence is probed)
#define AXP2101_REG_BATT_PERCENT  0xA4  ///< Fuel-gauge battery level, 0..100 (%)

// Charge state decoded from STATUS2 bits[2:0].
typedef enum {
    AXP2101_CHG_TRICKLE = 0,
    AXP2101_CHG_PRECHARGE,
    AXP2101_CHG_CONSTANT_CURRENT,
    AXP2101_CHG_CONSTANT_VOLTAGE,
    AXP2101_CHG_DONE,
    AXP2101_CHG_NOT_CHARGING,
    AXP2101_CHG_UNKNOWN,
} axp2101_charge_state_t;

typedef struct axp2101_dev_s *axp2101_handle_t;

// Init + presence check. Returns ESP_ERR_NOT_FOUND if the chip does not ACK.
esp_err_t axp2101_init(i2c_master_bus_handle_t bus, uint8_t addr,
                       axp2101_handle_t *handle);
void axp2101_deinit(axp2101_handle_t handle);

// Battery charge level 0..100 (%). Returns ESP_ERR_INVALID_RESPONSE if the
// fuel gauge reports an out-of-range value (e.g. gauge disabled).
esp_err_t axp2101_get_battery_percent(axp2101_handle_t handle, uint8_t *pct);

// Decoded charge state from STATUS2.
esp_err_t axp2101_get_charge_state(axp2101_handle_t handle,
                                   axp2101_charge_state_t *state);

// Human-readable charge-state text.
const char *axp2101_charge_state_text(axp2101_charge_state_t s);

#ifdef __cplusplus
}
#endif
