# Architecture Decision Log

Entries are appended chronologically. Each records what was decided, why, and any known tradeoffs.

---

## 2026-10-11 — Initial architecture decisions (original archive)

- **Target branch is `dev`, not `main`** — experimental implementations permitted; `main` is untouched until separately reviewed.
- **Three home-screen destinations: Recon, Analyze, Expand.** Settings is a toolbar utility, not a primary destination. Keeps the core workflow visible without clutter.
- **Expand discovers uninitialized modules; some need manual setup.** SPI/UART devices may not be safe to auto-probe. No blind probing that can damage hardware.
- **Use Waveshare ESP32-S3-Touch-LCD-3.5B as target.** Do not substitute the non-B 3.5 BSP — different display controller (ST7789 vs AXS15231B QSPI), different touch interface. The B suffix is load-bearing.
- **Keep approved PNG logo only; retain original README ASCII header.** DEF CON-exclusive enclosure and PCB artwork stay outside the repository.
- **Distinguish hardware status explicitly:** owned/received/planned/legacy and tested/unverified. Nothing is claimed to work until compiled, flashed, and verified on physical hardware.
- **Browser prototype is immediately navigable; LVGL firmware still needs verified board support.** The prototype tests interaction design only, not hardware behaviour.

---

## 2026-10-11 — Board bringup session decisions

- **Pin mappings sourced verbatim from Waveshare's official BSP** (`github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5B`, `ESP-IDF/05_lvgl_example/components/esp_bsp/`). No pin assignments were invented or inferred from the non-B board. Rationale: AGENTS.md rule 4 — do not invent GPIO mappings.

- **AXS15231B init-command table copied verbatim from Waveshare `bsp_display.c`.** The 30-command sequence is opaque (no public register map); deviating from the vendor table risks a blank or corrupted display on first boot.

- **Touch read uses the 11-byte proprietary command protocol from `bsp_touch.c`**, not a simple register read. The AXS15231B touch interface does not follow a standard I2C register layout; using the wrong protocol silently returns no touches.

- **Persistent I2C device handle for touch** (`s_touch_dev` created at init, reused every poll). Creating/destroying an I2C device handle on every 10 ms poll cycle adds unnecessary overhead and risks bus contention with IMU/RTC reads.

- **LVGL draw buffer allocated from SPIRAM** (`MALLOC_CAP_SPIRAM`), not internal SRAM. The 320×48-pixel (1/10 frame) buffer is ~30 KB — too large for internal SRAM alongside FreeRTOS stacks and driver DMA descriptors. 8 MB PSRAM is available via AXP2101.

- **PlatformIO board: `esp32-s3-devkitc-1-n16r8v`** (closest stock match — ESP32-S3 with 16 MB flash, 8 MB PSRAM). No dedicated Waveshare 3.5B board JSON exists in PlatformIO yet. A custom `boards/waveshare_esp32s3_35b.json` should be created once the exact flash/PSRAM mode is confirmed on physical hardware.

- **SPIRAM mode set to octal (`CONFIG_SPIRAM_MODE_OCT=y`) in `sdkconfig.defaults`.** The ESP32-S3R8 supports octal PSRAM; the Waveshare 3.5B product page lists 8 MB PSRAM stacked. TODO(HW): confirm octal vs quad on the physical board revision — a mismatch will prevent boot.

- **Partition table: factory (6 MB) + OTA_0 (6 MB) + SPIFFS (~3.8 MB)** on 16 MB flash. Rationale: 6 MB app slots give headroom for LVGL + radio stacks + future plugin runtime. SPIFFS reserves space for observation logs before an SD card is added. OTA slot enables signed updates (future roadmap item).

- **NVS settings service: immediate write on every set.** Alternative was a deferred flush; immediate write chosen for simplicity — settings change infrequently (brightness, privacy toggle) so NVS write wear is not a concern. Each set() opens its own handle so no flush/close is forgotten.

- **Settings defaults: brightness=75%, privacy=on, mask_length=4.** 75% gives visible output without burning the backlight; privacy on by default aligns with the privacy-first design principle in PROJECT_VISION.md.

---

## 2026-10-13 — RE-TARGET to the non-B 3.5 board (ST7796 / FT6336)

**Context.** A first flash of the 3.5B-targeted firmware produced a completely
black screen on the physical board. Investigation traced it to a hardware-target
mismatch, not a build-config problem: the board in hand is the **plain
ESP32-S3-Touch-LCD-3.5 (non-B)**, confirmed by Waveshare's own docs
(docs.waveshare.com/ESP32-S3-Touch-LCD-3.5: "Embedded with ST7796 driver chip
and FT6336 capacitive touch chip, using SPI and I2C communication"). The 3.5B
firmware set up a **QSPI** bus and sent the **AXS15231B** 30-command init table;
an ST7796 panel on **standard SPI** never received a valid command, so it stayed
dark. This is exactly the substitution AGENTS.md rule #3 warned against.

**Decisions made in this re-target (new "NOT B" repo):**

- **Display driver swapped to ST7796 over standard SPI.** Uses the stock
  `espressif/esp_lcd_st7796` component init table — no proprietary vendor table
  is needed (unlike the AXS15231B). A DC/RS line is now required (QSPI had none).

- **Touch driver swapped to FT6336, standard FT6x36 register protocol.** Write
  TD_STATUS (0x02), read 5 bytes. This replaces the AXS15231B 11-byte proprietary
  command sequence entirely. FT6336 fixed address 0x38.

- **Reset path moved to a TCA9554 I2C expander.** The vendor factory demo calls
  `io_expander_init()` before the display port; on this board the LCD/touch reset
  (and likely backlight enable) hang off the expander, not direct GPIOs. The I2C
  bus is therefore brought up FIRST, the expander configured and reset pulsed,
  before the SPI panel is initialised.

- **GPIO numbers and TCA9554 bits left as `unverified` placeholders.** Waveshare's
  wiki publishes the controller family but NOT the GPIO map in text; it is in the
  schematic and `ESP32-S3-Touch-LCD-3.5-Demo.zip` only. Per AGENTS.md rule #4 these
  are NOT invented — they are marked `unverified`/`GPIO_NUM_NC` and MUST be filled
  in from the demo/schematic before the first flash. The firmware will log and
  degrade gracefully rather than claim to work.

- **ST7796 colour defaults: inversion ON, BGR order.** Common for Waveshare ST7796
  panels; flip either in `board_adapter.cpp` if colours look wrong on HW.

- **platformio.ini `-D CONFIG_*` flags removed.** On an ESP-IDF-framework project,
  Kconfig options belong in `sdkconfig.defaults`, not `-D` preprocessor defines.
  The 3.5B repo wrongly duplicated them as `-D` flags; fixed here.

- **QMI8658 IMU, NVS settings, MCP23017/INA228 skeletons, partitions, PSRAM config
  carried over unchanged** — those are board-revision-independent and the IMU/RTC/
  PMIC/codec are the same parts on both boards.

**Still TODO(HW) before this can be claimed to work:** fill in verified GPIOs +
TCA9554 bits, compile with ESP-IDF v5.4+, flash, and confirm display + touch on
the physical non-B board.

---

## 2026-10-13 — GPIO pins VERIFIED from vendor demo

Pulled `ESP32-S3-Touch-LCD-3.5-Demo.zip` and read the vendor Arduino example
(`08_gfx_helloworld.ino`) plus the official expansion-header pinout. The display
and touch GPIOs are now **verified**, replacing the earlier `unverified`
placeholders in `board_adapter.cpp`:

- Display SPI: **SCLK=5, MOSI=1, MISO=2, DC=3, CS=-1, RST=-1** — from
  `Arduino_ESP32SPI(DC=3, CS=-1, SCK=5, MOSI=1, MISO=2)` +
  `Arduino_ST7796(bus, RST=-1, 0, IPS=true, 320, 480)`.
- Backlight: **GPIO6** direct (`#define GFX_BL 6`), driven as LEDC PWM.
- I2C: **SDA=8, SCL=7** — corroborated by the official 2.54 mm header (pins 28/26).
- `IPS=true` in the vendor constructor confirms colour inversion ON, matching the
  `esp_lcd_panel_invert_color(..., true)` default.

**Key correction to the earlier re-target assumption:** the display does NOT
require the TCA9554 expander. The vendor GFX path drives the panel with CS=-1,
RST=-1 and a plain GPIO backlight, touching no expander. So the TCA9554 bring-up
in `board_adapter.cpp` was made **optional / non-fatal** and no longer pulses an
LCD reset. The expander remains best-effort for touch reset and the PWR-button
EXIO (ESP-IDF factory demo), with its address/bit numbers still `unverified`.

The remaining unverified items (TCA9554 address + bits, FT6336 INT, SD GPIOs) do
not block display or touch bring-up.

---

## 2026-10-13 — Aligned board_adapter.cpp to the vendor ESP-IDF port

Read the vendor's own ESP-IDF bring-up source from the demo zip
(`ESP-IDF/01_factory/components/esp_port/esp_3inch5_lcd_port.cpp` plus the
bundled `esp_lcd_st7796` and `esp_lcd_touch_ft6336` components) and aligned the
adapter to it exactly, so it matches a known-working configuration:

- **Removed the TCA9554 expander path entirely.** The vendor port uses
  CS/RST/TP_RST/TP_INT = `GPIO_NUM_NC` and no expander for display or touch. The
  earlier draft's speculative expander reset was unnecessary and a liability (its
  address/bits were guessed), so it is gone. The TCA9554 is documented as
  present-but-unused.
- **Pixel clock corrected 40 → 80 MHz** to match the vendor value.
- **MISO corrected to `GPIO_NUM_NC`** (vendor sets MISO = NC; the earlier draft
  had GPIO2 from the Arduino bus macro).
- **Backlight LEDC timer set to `LEDC_TIMER_1`** to match the vendor port.
- Kept BGR element order + `invert_color(true)` (both match the vendor).
- Init order now mirrors the vendor: SPI bus → panel IO → ST7796 reset/init/
  invert → I2C + FT6336 → LVGL → backlight.

The FootHoled-specific LVGL 9 integration (async flush via trans-done callback,
software rotation, QMI8658 auto-rotation) is retained on top of the vendor base.
Touch uses an inline FT6x36 register read rather than pulling in the
`esp_lcd_touch_ft6336` component; that component is the documented fallback if
touch misbehaves on hardware.

Still NOT compiled/flashed/tested — but the configuration now matches the vendor's
working factory firmware, so a first flash has a real chance of lighting up.
