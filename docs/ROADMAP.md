# Roadmap

## UI Alpha (this archive)
- [x] Document architecture and verified-versus-unverified hardware inventory
- [x] Preserve original README ASCII header and PNG logo
- [x] Clickable offline browser prototype with Recon / Analyze / Expand, Settings, privacy preview, and simulated module setup
- [x] LVGL 9 UI source foundation
- [x] ST7796 SPI display + FT6336 capacitive touch BSP integration (`board_adapter.cpp`) — non-B board, via TCA9554 reset expander; **GPIOs unverified, awaiting compile + flash test on physical board**
- [x] PlatformIO board target corrected (`esp32-s3-devkitc-1-n16r8v`), SPIRAM/flash flags set
- [x] `partitions.csv` for 16 MB flash (NVS + factory 6 MB + OTA 6 MB + SPIFFS + NVS keys)
- [x] NVS settings service — brightness, privacy masking, mask length persisted across reboots
- [x] Verified GPIO pin map documented (`docs/H0.1_Pin_Map.md`)
- [ ] **Compile, flash, and verify firmware on physical Waveshare 3.5 (non-B) board** ← next hardware step
- [ ] Confirm PSRAM mode (octal vs quad) on physical board revision
- [ ] Verify PlatformIO/ESP-IDF build with `espressif/esp_lcd_st7796` component

## Hardware bringup
- [ ] Display brightness control — LEDC code written; NVS persistence wired; TODO(HW): tune on real board
- [ ] MCP23017 GPIO expander driver (I2C, received — driver skeleton pending)
- [ ] INA228 power/current monitor driver (I2C, received — driver skeleton pending)
- [ ] Foriot dual 18650 electrical and thermal validation
- [ ] Legacy GNSS UART bringup (GY-GPS6MV2 candidate; MAX-M10S upgrade planned)
- [ ] Real Expand module registry and safe probing
- [ ] SD card mount and observation storage (SPIFFS partition reserved)

## Later
- [ ] Wi-Fi/BLE, NFC/RFID, IR, NRF24, Sub-GHz interfaces
- [ ] Signed firmware OTA update strategy (OTA partition reserved)
- [ ] Sandboxed SD application runtime and stable plugin API
- [ ] Standard-edition PCB and enclosure publication
- [ ] FAFO (Freq Around, Find Out) — separate project, targeting DC35; ~100 units planned
