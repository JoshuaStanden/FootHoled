# FootHoled H1 Firmware

Firmware for the **Waveshare ESP32-S3-Touch-LCD-3.5 (non-B)** — ESP32-S3R8
(16 MB flash, 8 MB octal PSRAM), ST7796 display over standard SPI, FT6336 touch
over I2C.

- **Toolchain:** ESP-IDF **6.1.x**, LVGL **9.2.x** (both pinned in
  `src/idf_component.yml` and `dependencies.lock`).
- **Framework:** ESP-IDF (PlatformIO env in `platformio.ini`).

## Features

- Live display + touch UI (LVGL 9)
- Wi-Fi recon scanning (`wifi_scan`)
- Bluetooth LE recon scanning (`ble_scan`, NimBLE passive observer)
- SD-card recon log, CSV (`recon_log`) + Format SD Card (`sd_format`)
- Hardware diagnostics screen (`diagnostics`)
- IMU auto-rotation (`qmi8658`), power monitor (`ina228`), I/O expander
  (`mcp23017`)
- Boot splash (`splash`)

## Build & flash

With ESP-IDF 6.1 installed:

```sh
cd firmware
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

Or with PlatformIO:

```sh
cd firmware
pio run -t upload
```

The component manager pulls LVGL, `esp_lcd_st7796`, and
`esp_io_expander_tca9554` automatically from `src/idf_component.yml` /
`dependencies.lock` on first build — **`managed_components/` is intentionally
not committed.**

## Before you flash — read `DISPLAY_NOTES.md`

`DISPLAY_NOTES.md` documents the display-bring-up invariants that have broken the
screen before and must not be changed casually:

- LVGL draw buffers stay in **PSRAM** (`MALLOC_CAP_SPIRAM`).
- The **TCA9554** expander reset pulse must run before panel init.
- PSRAM stays in **OCT** mode (`CONFIG_SPIRAM_MODE_OCT=y`) — the R8 is octal.

`board_adapter.cpp`'s display/touch init is a protected baseline; features live
in their own files and must not alter it.

> Hardware note: this tree has not been compiled/flashed from this working
> environment (no toolchain here). Per the project's AGENTS.md rule, treat
> hardware behaviour as unverified until built and flashed on the physical board.
> Known open item: FT6336 touch swap/mirror-per-rotation mapping is untested.
