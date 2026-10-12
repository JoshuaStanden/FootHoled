/**
 * @file mcp23017.h
 * @brief MCP23017 16-bit I2C GPIO expander driver for FootHoled H1.
 *
 * The MCP23017 adds 16 GPIO pins (two 8-bit ports A and B) over I2C.
 * It is used to expand the ESP32-S3's available GPIO for module connectors,
 * LEDs, buttons, and the 12-pin expansion port.
 *
 * Hardware: Microchip MCP23017, I2C address 0x20 (A2=A1=A0=GND).
 * Bus: I2C0, shared with touch, IMU, RTC, PMIC (SDA GPIO 8, SCL GPIO 7).
 * Register layout: BANK=0 (power-on default, interleaved A/B pairs).
 *
 * Reference: Microchip DS20001952C (MCP23017 datasheet), Table 3-3.
 *
 * TODO(HW): Verify I2C address on physical board (all address pins grounded = 0x20).
 * TODO(HW): Verify no address conflict with other I2C devices on the bus.
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
// Register addresses — BANK=0 mode (default, A/B interleaved)
// Source: MCP23017 datasheet Table 3-3
// ---------------------------------------------------------------------------
#define MCP23017_REG_IODIRA   0x00  ///< I/O direction Port A (1=input, 0=output); default 0xFF (all inputs)
#define MCP23017_REG_IODIRB   0x01  ///< I/O direction Port B
#define MCP23017_REG_IPOLA    0x02  ///< Input polarity Port A
#define MCP23017_REG_IPOLB    0x03  ///< Input polarity Port B
#define MCP23017_REG_GPINTENA 0x04  ///< Interrupt-on-change enable Port A
#define MCP23017_REG_GPINTENB 0x05  ///< Interrupt-on-change enable Port B
#define MCP23017_REG_DEFVALA  0x06  ///< Default compare value Port A (for interrupt)
#define MCP23017_REG_DEFVALB  0x07  ///< Default compare value Port B
#define MCP23017_REG_INTCONA  0x08  ///< Interrupt control Port A
#define MCP23017_REG_INTCONB  0x09  ///< Interrupt control Port B
#define MCP23017_REG_IOCON    0x0A  ///< I/O expander configuration (shared, also at 0x0B)
#define MCP23017_REG_GPPUA    0x0C  ///< Pull-up resistor config Port A (1=100k pull-up enabled)
#define MCP23017_REG_GPPUB    0x0D  ///< Pull-up resistor config Port B
#define MCP23017_REG_INTFA    0x0E  ///< Interrupt flag Port A (read-only; 1=triggered)
#define MCP23017_REG_INTFB    0x0F  ///< Interrupt flag Port B (read-only)
#define MCP23017_REG_INTCAPA  0x10  ///< Interrupt capture Port A (read-only; reading clears INT)
#define MCP23017_REG_INTCAPB  0x11  ///< Interrupt capture Port B (read-only)
#define MCP23017_REG_GPIOA    0x12  ///< GPIO Port A: read = pin logic state, write = output latch
#define MCP23017_REG_GPIOB    0x13  ///< GPIO Port B
#define MCP23017_REG_OLATA    0x14  ///< Output latch Port A (direct latch read/write)
#define MCP23017_REG_OLATB    0x15  ///< Output latch Port B

// Default I2C address (A2=A1=A0=GND)
#define MCP23017_I2C_ADDR_DEFAULT  0x20

// Port identifiers
typedef enum {
    MCP23017_PORT_A = 0,
    MCP23017_PORT_B = 1,
} mcp23017_port_t;

// Pin direction
typedef enum {
    MCP23017_OUTPUT = 0,
    MCP23017_INPUT  = 1,
} mcp23017_dir_t;

// Driver handle — opaque; created by mcp23017_init()
typedef struct mcp23017_dev_s *mcp23017_handle_t;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

/**
 * @brief Initialise the MCP23017 driver and create a device handle.
 *
 * Adds a device to the shared I2C bus, reads back MANUFACTURER_ID to verify
 * the chip is present (by writing and reading IOCON), and sets all pins to
 * inputs (power-on default, confirmed by IODIRA/B = 0xFF).
 *
 * TODO(HW): verify this returns ESP_OK on real hardware before using any
 * other function.
 *
 * @param bus      I2C master bus handle from board_adapter (shared I2C0).
 * @param addr     7-bit I2C address (MCP23017_I2C_ADDR_DEFAULT = 0x20).
 * @param[out] handle  Returned driver handle; pass to all other functions.
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if device does not respond.
 */
esp_err_t mcp23017_init(i2c_master_bus_handle_t bus, uint8_t addr,
                         mcp23017_handle_t *handle);

/**
 * @brief Release the driver handle and remove the I2C device.
 * @param handle  Handle returned by mcp23017_init().
 */
void mcp23017_deinit(mcp23017_handle_t handle);

// ---------------------------------------------------------------------------
// Pin configuration
// ---------------------------------------------------------------------------

/**
 * @brief Set the direction of all 8 pins on one port at once.
 *
 * @param handle  Driver handle.
 * @param port    MCP23017_PORT_A or MCP23017_PORT_B.
 * @param mask    Bitmask: 1=input, 0=output (matches IODIR register).
 *                Example: 0xFF = all inputs, 0x00 = all outputs.
 * @return ESP_OK on success.
 */
esp_err_t mcp23017_set_port_direction(mcp23017_handle_t handle,
                                       mcp23017_port_t port, uint8_t mask);

/**
 * @brief Set the direction of a single pin.
 *
 * @param handle  Driver handle.
 * @param port    MCP23017_PORT_A or MCP23017_PORT_B.
 * @param pin     Pin number 0–7.
 * @param dir     MCP23017_INPUT or MCP23017_OUTPUT.
 * @return ESP_OK on success.
 */
esp_err_t mcp23017_set_pin_direction(mcp23017_handle_t handle,
                                      mcp23017_port_t port, uint8_t pin,
                                      mcp23017_dir_t dir);

/**
 * @brief Enable or disable the 100 kΩ internal pull-up on input pins.
 *
 * @param handle  Driver handle.
 * @param port    MCP23017_PORT_A or MCP23017_PORT_B.
 * @param mask    Bitmask: 1=pull-up enabled, 0=disabled. Only affects input pins.
 * @return ESP_OK on success.
 */
esp_err_t mcp23017_set_pullups(mcp23017_handle_t handle,
                                mcp23017_port_t port, uint8_t mask);

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

/**
 * @brief Write all 8 output pins on one port at once.
 *
 * Writes to OLAT (output latch), which only affects pins configured as output.
 *
 * @param handle  Driver handle.
 * @param port    Port to write.
 * @param value   Bitmask: 1=high, 0=low.
 * @return ESP_OK on success.
 */
esp_err_t mcp23017_write_port(mcp23017_handle_t handle,
                               mcp23017_port_t port, uint8_t value);

/**
 * @brief Set or clear a single output pin (read-modify-write on OLAT).
 *
 * @param handle  Driver handle.
 * @param port    MCP23017_PORT_A or MCP23017_PORT_B.
 * @param pin     Pin number 0–7.
 * @param high    true = drive high, false = drive low.
 * @return ESP_OK on success.
 */
esp_err_t mcp23017_write_pin(mcp23017_handle_t handle,
                              mcp23017_port_t port, uint8_t pin, bool high);

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

/**
 * @brief Read the logic state of all 8 pins on one port.
 *
 * Reads GPIO register (reflects actual pin state, not latch).
 *
 * @param handle    Driver handle.
 * @param port      Port to read.
 * @param[out] val  Bitmask: 1=high, 0=low.
 * @return ESP_OK on success.
 */
esp_err_t mcp23017_read_port(mcp23017_handle_t handle,
                              mcp23017_port_t port, uint8_t *val);

/**
 * @brief Read the logic state of a single pin.
 *
 * @param handle      Driver handle.
 * @param port        MCP23017_PORT_A or MCP23017_PORT_B.
 * @param pin         Pin number 0–7.
 * @param[out] high   true if pin is high.
 * @return ESP_OK on success.
 */
esp_err_t mcp23017_read_pin(mcp23017_handle_t handle,
                             mcp23017_port_t port, uint8_t pin, bool *high);

#ifdef __cplusplus
}
#endif
