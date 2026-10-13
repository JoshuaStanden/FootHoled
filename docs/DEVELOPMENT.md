# Development workflow

Target branch: `dev`. Work on short-lived feature branches and open PRs into `dev`. Keep `main` unchanged until separately reviewed.

## Browser UI prototype
Open `prototype/index.html` locally (no server required). This tests **interaction design**, not LVGL or hardware. Settings persist in browser localStorage; reset with the UI button.

## Firmware foundation
PlatformIO and ESP-IDF project is under `firmware/`. The target is `esp32-s3-devkitc-1-n16r8v` as a **stock match** for the ESP32-S3R8 (no dedicated Waveshare 3.5 board JSON exists yet). `firmware/src/board_adapter.cpp` registers an ST7796 SPI display + FT6336 touch, but GPIO numbers are **unverified placeholders** until confirmed from the schematic/demo, so there is **no guaranteed LCD output** until that is done and the firmware is flashed. See Waveshare's official examples for the **non-B 3.5** variant: https://docs.waveshare.com/ESP32-S3-Touch-LCD-3.5

Expected commands after installing PlatformIO: `cd firmware && pio run`, `pio run -t upload`, `pio device monitor`. Build status is **not verified in this environment** (no ESP-IDF/PlatformIO toolchain installed). Do not flash until pin mappings and display drivers are checked.

## Contribution checklist
- Code is divided into services/UI, with no blocking scan inside touch callbacks
- Feature state is labeled implemented/experimental/planned
- Update hardware inventory for any new purchase or discovered revision
- Test navigation and settings in browser prototype
- Document on-device build/flash/test evidence separately


## Code documentation standards

This experimental branch aims for professional, explanatory comments rather than a comment on every line. Public interfaces should document inputs, outputs, ownership and failure cases; drivers should explain verified pin mappings, voltage assumptions and initialization order. Each unimplemented feature must say what is missing and must not masquerade as a working feature. See `AGENTS.md` for AI-assistant guidance.
