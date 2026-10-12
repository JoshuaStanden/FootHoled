# Development workflow

Target branch: `dev`. Work on short-lived feature branches and open PRs into `dev`. Keep `main` unchanged until separately reviewed.

## Browser UI prototype
Open `prototype/index.html` locally (no server required). This tests **interaction design**, not LVGL or hardware. Settings persist in browser localStorage; reset with the UI button.

## Firmware foundation
PlatformIO and ESP-IDF project is under `firmware/`. The default target is `esp32-s3-devkitc-1` as a **toolchain placeholder**, not a verified Waveshare board definition. `firmware/src/board_adapter.cpp` deliberately does not register a display or touch device. Until replaced, there is **no physical LCD output**. See Waveshare's official `05_lvgl_example` for the **3.5B** variant: https://docs.waveshare.com/ESP32-S3-Touch-LCD-3.5B/ESP-IDF

Expected commands after installing PlatformIO: `cd firmware && pio run`, `pio run -t upload`, `pio device monitor`. Build status is **not verified in this environment** (no ESP-IDF/PlatformIO toolchain installed). Do not flash until pin mappings and display drivers are checked.

## Contribution checklist
- Code is divided into services/UI, with no blocking scan inside touch callbacks
- Feature state is labeled implemented/experimental/planned
- Update hardware inventory for any new purchase or discovered revision
- Test navigation and settings in browser prototype
- Document on-device build/flash/test evidence separately


## Code documentation standards

This experimental branch aims for professional, explanatory comments rather than a comment on every line. Public interfaces should document inputs, outputs, ownership and failure cases; drivers should explain verified pin mappings, voltage assumptions and initialization order. Each unimplemented feature must say what is missing and must not masquerade as a working feature. See `AGENTS.md` for AI-assistant guidance.
