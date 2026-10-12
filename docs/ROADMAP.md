# Roadmap

## UI Alpha (this archive)
- [x] Document architecture and verified-versus-unverified hardware inventory
- [x] Preserve original README ASCII header and PNG logo
- [x] Clickable offline browser prototype with Recon / Analyze / Expand, Settings, privacy preview, and simulated module setup
- [x] LVGL 9 UI source foundation
- [ ] **Integrate and test Waveshare 3.5B QSPI LCD/touch BSP on physical board**
- [ ] Verify PlatformIO/ESP-IDF firmware build and flashing on actual board

## Hardware bringup
- [ ] Display brightness control and NVS settings persistence
- [ ] MCP23017 and INA228 drivers, tested on real board
- [ ] Foriot electrical and thermal validation
- [ ] Legacy GNSS UART bringup
- [ ] Real Expand module registry and safe probing
- [ ] SD and observation storage

## Later
- [ ] Wi-Fi/BLE, NFC/RFID, IR, NRF24, Sub-GHz interfaces
- [ ] Signed firmware update strategy
- [ ] Sandboxed SD application runtime and stable plugin API
- [ ] Standard-edition PCB and enclosure publication
