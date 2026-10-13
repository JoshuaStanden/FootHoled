# Hardware inventory

The authoritative structured inventory is [`../hardware/components.yaml`](../hardware/components.yaml). **Ownership is not equivalent to compatibility or integration.**

| Component | Exact identification | Possession | H1 integration |
|---|---|---|---|
| Main board | Waveshare ESP32-S3-Touch-LCD-3.5 (non-B) | Received | Not yet verified |
| GPIO expander | MCP23017 (module revision unverified) | Received | Pending |
| Power monitor | INA228 (module revision unverified) | Received | Pending |
| Battery holder | Foriot dual 18650, 3V/5V outputs, level indicator, charging and protection; exact model unverified | Received | Electrical verification pending |
| GNSS legacy | GY-GPS6MV2 (GNSS chip marking unverified) | Owned | Candidate prototype |
| External Wi-Fi antenna/cable | Connector and band support unverified | Owned | Compatibility pending |
| Original touchscreen | ESP32-2432S028 CYD | Owned | Legacy |
| Original ESP32 | HiLetgo ESP32-D/ESP32-S v1.1 | Owned | Legacy |
| Shift registers | SN74HC595N | Owned | Legacy |
| Booster | HW-183B | Owned | Legacy |
| Charger | HW-373 v1.2.1 | Owned | Legacy |
| GNSS upgrade | MAX-M10S | Planned | Not purchased |
| NFC | PN532 | Planned | Not purchased |
| LF RFID | 125 kHz reader, model TBD | Planned | Not purchased |
| Extra 2.4 GHz radio | NRF24L01+ | Planned | Not purchased |
| Sub-GHz radio | CC1101 | Planned | Not purchased |
| Magnetometer | Model TBD | Planned | Not purchased |
| Infrared | TX/RX modules TBD | Planned | Not purchased |
| Expansion port | 12-pin interface, electrical specification TBD | Planned | Not built |

### Confirmed 3.5 (non-B) board family details
ESP32-S3R8, 320×480 capacitive LCD, **ST7796 SPI LCD controller, FT6336 I2C touch, TCA9554 I/O expander**, QMI8658 IMU, PCF85063 RTC, AXP2101 PMIC, ES8311 audio codec, TF card slot. **Flash/PSRAM capacities and revision should be checked on the exact received board before hard-coding memory assumptions.**

Sources: https://docs.waveshare.com/ESP32-S3-Touch-LCD-3.5 and https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-3.5

### Electrical warnings
- Do **not** connect the Foriot 3V/5V outputs to the Waveshare battery input without a verified schematic and rail measurement. Battery inputs can expect a single-cell Li-ion voltage; 5V is not equivalent.
- Confirm Foriot current capacity, charge behavior, protection, cell arrangement and voltage regulation under load before integrating.
- Confirm RF antenna connector, impedance, and frequency compatibility before connection.
- I2C devices require bus voltage/pullup compatibility; 3.3V GPIO is not automatically 5V tolerant.
- SPI and UART modules cannot generally be discovered by an address scan. Do not blindly toggle unknown pins.
