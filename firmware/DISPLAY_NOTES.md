# FootHoled H1 — Display Regression Notes (READ BEFORE TOUCHING `board_adapter.cpp`)

Board: Waveshare ESP32-S3-Touch-LCD-3.5 (NON-B) — **ESP32-S3R8**, ST7796 over
**standard SPI**, FT6336 touch over I2C. Toolchain: ESP-IDF 6.1.x, LVGL 9.2.x.

The display has repeatedly stopped working after firmware edits, forcing a
rollback to the known-good `justjoshin` branch. This note records every cause
found so far and the rule that prevents a recurrence. **`board_adapter.cpp`'s
display/touch init is a protected baseline.** New features (BLE, SD, diagnostics)
live in their own files and must not alter it.

## Confirmed causes of the "blank display" / display-breaks loop

### 1. LVGL draw buffers — location vs. SPI-DMA bounce (UPDATED after HW test)
History (recorded before the board existed): a build changed the draw buffers
from `MALLOC_CAP_SPIRAM` to `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`; the display
broke and it was rolled back to PSRAM. The failure mode was never captured, and
that attempt also coincided with other unknowns (possibly the QUAD-PSRAM mistake
below or a pre-TCA9554 build), so "internal RAM" may have been mis-blamed.

HARDWARE FINDING (2026-10-18, real board): with the buffers in PSRAM, once
Wi-Fi + NimBLE are running the SPI master can no longer allocate its per-flush
internal bounce buffer for the PSRAM source, and the display dies with a storm of:
```
E spi_common: spicommon_dma_setup_priv_buffer: Failed to allocate priv TX buffer
E lcd_panel.io.spi: panel_io_spi_tx_color: spi transmit (queue) color failed
E st7796_general: panel_st7796_draw_bitmap: send command failed
E FootHoled/board: draw_bitmap failed: ESP_ERR_NO_MEM
```
S3 SPI-DMA cannot read PSRAM directly, so a PSRAM source forces a bounce copy
that starves under radio memory pressure. Fix: allocate all three draw buffers
(buf1, buf2, and s_rotate_buf — all can reach SPI DMA) in
`MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`, reserved at boot before the radios
start (~90 KB total at `V_RES/10`), with a PSRAM fallback if internal is short.
**Rule (current): draw buffers in internal DMA RAM. If this ever reintroduces a
blank screen, capture the EXACT error before reverting — a silent blank is a
different fault than the NO_MEM storm above, and reverting to PSRAM brings the
NO_MEM storm back.**

FOLLOW-UP (same day): simply switching the buffers to internal DMA RAM was NOT
enough — the allocation itself failed and fell back to PSRAM
("internal DMA draw-buffer alloc failed; falling back to PSRAM"), because the
radios start before board init and fragment internal RAM, leaving no contiguous
block. ROBUST FIX (in `sdkconfig.defaults`), which removes the race entirely:
- `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=131072` — a 128 KB internal DMA pool
  that normal malloc() cannot consume, so the draw buffers always fit.
- `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` — Wi-Fi/LWIP buffers go to PSRAM.
- `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y` — net80211/pp/bt/lwip .bss to
  PSRAM.
Plus: board init is ordered before the radios in main.cpp, and buffers are
`V_RES/20`. The pre-alloc heap log in board_adapter.cpp prints the internal DMA
free/largest-block so a regression is diagnosable, not silent.

### 2. Missing TCA9554 I/O-expander reset pulse  (fixed — integrated)
On this board the ST7796 LCD reset and FT6336 touch reset are **not** on ESP32
GPIOs (`GPIO_NUM_NC`) — they are driven by a **TCA9554 I2C expander**. The vendor
factory firmware (`waveshareteam/ESP32-S3-Touch-LCD-3.5`, `ESP-IDF/01_factory`:
`i2c_bus_init(); io_expander_init(); display_port_init();`) pulses expander pin 1
low→high to release the panel/touch from reset **before** `esp_lcd_panel_reset/
init`. Without it the panel is held in reset and comes up **blank even though
every `esp_lcd_*` call returns `ESP_OK`.**
**Rule: `board_adapter.cpp` step 1b creates the I2C bus and pulses TCA9554 pin 1
(assert 100 ms → release 100 ms) before the panel driver. Keep it.**

### 3. Duplicate `esp_lcd_panel_mirror()` call  (fixed)
One build called `esp_lcd_panel_mirror(..., true, false)` twice back-to-back.
**Rule: configure the mirror exactly ONCE in board init, never on the flush path.**

## PSRAM mode — do NOT "fix" this to QUAD

The ESP32-S3**R8** uses **8 MB OCTAL** PSRAM ("anything larger than 4 MB is
octal"; the R8V is also octal). `sdkconfig.defaults` MUST keep
`CONFIG_SPIRAM_MODE_OCT=y`. A well-intentioned external review proposed switching
to `CONFIG_SPIRAM_MODE_QUAD=y` on the claim that "R8 is quad" — **that is
backwards.** QUAD mode never initialises the R8's octal PSRAM, PSRAM init fails
at boot, the SPIRAM draw buffers can't allocate, and the display dies. Keep OCT.

## Current state of `board_adapter.cpp` (as integrated)

The live adapter is the **safe combination** of the known-good baseline plus the
one genuinely new fix from the external review:
- PSRAM draw buffers (`MALLOC_CAP_SPIRAM`, `V_RES/10`) — from baseline.
- TCA9554 reset pulse before panel init (step 1b) — from the review (adopted).
- Enum-named `orient_to_lv` (behaviourally identical to the tested integer
  remap) — from the review (cosmetic).
- Single `esp_lcd_panel_mirror` — duplicate removed.
- `sdkconfig` PSRAM left on OCT — the review's QUAD change was rejected.

## Known open item (not display-fatal)

**Touch coordinate mapping.** `read_touch_raw()` applies no swap/mirror, while
the vendor driver sets `swap_xy`/`mirror_x`/`mirror_y` per rotation. Touch may be
mis-mapped relative to the drawn UI. The robust fix is the vendor
`esp_lcd_touch_ft6336` component. Needs a hardware test to tune — left as-is.

## Feature status

BLE scanning (`ble_scan`), SD recon log (`recon_log`), SD format (`sd_format`),
diagnostics (`diagnostics`), boot splash (`splash`), IMU/sensors (`qmi8658`,
`ina228`, `mcp23017`) are all implemented and **do not touch display init** —
they ride on top of the restored display path.
