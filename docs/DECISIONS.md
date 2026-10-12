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
