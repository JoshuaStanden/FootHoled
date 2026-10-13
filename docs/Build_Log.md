# Build Log

Chronological record of build attempts, hardware observations, and verified milestones.
Mark each entry with status: **[PLANNED]**, **[IN PROGRESS]**, **[DONE — SOFTWARE ONLY]**, **[DONE — HW VERIFIED]**, or **[BLOCKED]**.

---

## 2026-10-11 — Board bringup session  `[DONE — SOFTWARE ONLY]`

**What was done (no physical hardware used):**

| File | Change |
|---|---|
| `firmware/src/board_adapter.cpp` | Full ST7796 SPI display + FT6336 touch init via TCA9554 reset, LVGL 9 registration, LEDC backlight (non-B board) |
| `firmware/src/settings.h/.cpp` | NVS settings service — brightness, privacy, mask length |
| `firmware/src/main.cpp` | Wired settings init + brightness apply into startup sequence |
| `firmware/src/ui.cpp` | Settings screen reads/writes via settings service (no more RAM-only state) |
| `firmware/src/idf_component.yml` | Added `espressif/esp_lcd_st7796` dependency (non-B board) |
| `firmware/src/CMakeLists.txt` | Added `settings.cpp` to build sources |
| `firmware/platformio.ini` | Board target → `esp32-s3-devkitc-1-n16r8v`; SPIRAM + flash flags |
| `firmware/sdkconfig.defaults` | PSRAM octal, SPI DMA, stack size |
| `firmware/partitions.csv` | 16 MB layout: NVS + factory 6M + OTA 6M + SPIFFS + NVS keys |
| `docs/H0.1_Pin_Map.md` | Complete verified GPIO table from Waveshare BSP |
| `docs/H0.1_System_Architecture.md` | Board init sequence and subsystem overview |

**Pin sources verified:** All GPIO numbers sourced from Waveshare `05_lvgl_example` BSP (`bsp_display.h`, `bsp_i2c.h`, `bsp_touch.h`, `bsp_sdcard.c`, `bsp_camera.c`).

**Touch protocol correction:** Initial `board_adapter.cpp` used a simplified register read. Corrected to the actual 11-byte command protocol found in `bsp_touch.c` (`{0xb5, 0xab, 0xa5, 0x5a, ...}`).

**Branch:** `feat/waveshare-3.5b-board-bringup` (2 commits: `599fb95`, `21a456d`)

**Build status:** NOT COMPILED. PlatformIO + ESP-IDF v5.5+ toolchain not installed in this environment. Do not treat firmware as functional until the steps below are completed.

---

## Next required hardware steps  `[PLANNED]`

1. **Install toolchain:** ESP-IDF v5.5.0+ (required per Waveshare docs) + PlatformIO.
   - Windows: use the ESP-IDF Installation Manager (offline installer) from [dl.espressif.com/dl/eim/](https://dl.espressif.com/dl/eim/)
   - Then: `cd firmware && pio run` to verify the build compiles clean.

2. **Check PSRAM mode:** Before flashing, confirm whether the physical board uses octal or quad PSRAM. If `CONFIG_SPIRAM_MODE_OCT` is wrong the board will fail to boot. Check the Waveshare schematic for your board revision.

3. **First flash:** `pio run -t upload` (USB-C, ensure the correct COM port is selected).

4. **Serial monitor:** `pio device monitor` — look for:
   - `FootHoled/settings: Settings loaded — brightness=75%...`
   - `FootHoled/board: Board init complete — display 320x480, touch registered`
   - Any `ESP_LOGE` lines indicate a driver failure.

5. **Display check:** Screen should light up and show the FootHoled home screen (Recon / Analyze / Expand buttons).

6. **Touch check:** Tap a button — serial output should show the touch callback firing. Calibrate X/Y axis swap/mirror if coordinates are rotated.

7. **Settings persistence check:** Change brightness in Settings, reboot, confirm it restores.

8. **Mark this entry `[DONE — HW VERIFIED]` and record observed behaviour.**

---

## Hardware validation checklist (fill in on physical board)

- [ ] Firmware compiled without errors
- [ ] Board boots (no panic / reset loop)
- [ ] PSRAM mode confirmed: _______ (octal / quad)
- [ ] Display initialises and shows UI
- [ ] Backlight at correct brightness (75% default)
- [ ] Touch responds to taps
- [ ] Touch coordinates correct orientation (portrait 320×480)
- [ ] Settings survive reboot (NVS working)
- [ ] I2C bus: QMI8658 IMU responds at address 0x6A or 0x6B
- [ ] Auto-rotate: WHO_AM_I reads 0x05 (serial log "QMI8658 initialised")
- [ ] Auto-rotate: tilting the device rotates the UI through all 4 orientations
- [ ] Auto-rotate: axis mapping correct (if a pose rotates the WRONG way, adjust
      the single calibration block in qmi8658.cpp:classify_orientation())
- [ ] Auto-rotate: touch still lands on the right widget after rotating
- [ ] Auto-rotate: lying flat holds the last orientation (no jitter)
- [ ] Auto-rotate: can be disabled via the auto_rotate setting
- [ ] I2C bus: PCF85063 RTC responds at 0x51
- [ ] I2C bus: AXP2101 PMIC responds at 0x34
- [ ] SD card mounts at `/sdcard`
- [ ] MCP23017 responds at I2C address (default 0x20)
- [ ] INA228 responds at I2C address (default 0x40)
