# FootHoled
```
______          _   _   _       _          _
|  ___|        | | | | | |     | |        | |
| |_ ___   ___ | |_| |_| | ___ | | ___  __| |
|  _/ _ \ / _ \| __|  _  |/ _ \| |/ _ \/ _` |
| || (_) | (_) | |_| | | | (_) | |  __/ (_| |
\_| \___/ \___/ \__\_| |_/\___/|_|\___|\__,_|
```


![FootHoled logo](assets/footholed-logo.png)

**Recon. Analyze. Expand.**

FootHoled is designed to be a modular handheld platform for wireless security research, GPS-enabled reconnaissance, and hardware experimentation.
Or you can think of it as a location-aware, privacy-conscious reconnaissance platform that correlates observations from multiple sources, can be physically and programmatically expanded, and integrates with a desktop analysis environment.

FootHoled H1 is an experimental, modular ESP32-S3 handheld for authorized wireless-security research, hardware experimentation and offline-first field observations.

## Current state
- **Interactive browser prototype:** Open [`prototype/index.html`](prototype/index.html) in a browser. Navigation, settings, demo module discovery, and sample privacy masking work. This is not device firmware.
- **LVGL 9 source:** `firmware/` contains native UI code and a PlatformIO/ESP-IDF foundation. **This repo targets the NON-B Waveshare 3.5 board — ST7796 SPI display + FT6336 I2C touch. The board-support adapter is written but not yet compiled, flashed, or tested on hardware.** It is not yet a flash-and-use touchscreen release.
- **Hardware functions:** Radio, GNSS, NFC, IR, storage and module probing are currently stubs; demonstration data is explicitly labeled.

## Hardware direction
- ESP32-based platform
- Waveshare 3.5" 320x480 IPS capacitive touchscreen w/ ESP32-S3 (replaces the earlier "Cheap Yellow Display" CYD plan)
- GPS support — dedicated GPS board with helical stubby antenna
- Rechargeable dual 18650 battery system
- External Wi-Fi antenna — at least one, possibly two
- Battery monitoring — LED status lights for charge level/status today; adding in-UI usage monitoring and remaining-battery calculation
- Open hardware
- 3D-printable enclosure — custom designs, published once hardware/software is tested
- (Dropped from the original plan: SAO expansion)

### Longer-term goals
- Smart battery validation
- Modular expansion ecosystem (12-pin add-on module connector)
- Plugin architecture
- Custom PCB
- FootHoled OS

## Development entry points
1. Read [`AGENTS.md`](AGENTS.md), [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md), and [`hardware/components.yaml`](hardware/components.yaml).
2. Open `prototype/index.html` and explore Recon / Analyze / Expand and Settings.
3. Verify the Waveshare **ESP32-S3-Touch-LCD-3.5 (non-B)** board adapter — fill in the unverified GPIOs/TCA9554 bits from the *non-B* vendor demo/schematic, then compile, flash, and confirm real LVGL display/touch output.
4. Add real hardware modules through the service interface, never directly in UI event callbacks.

## Documents
[Vision](docs/PROJECT_VISION.md) · [Architecture](docs/ARCHITECTURE.md) · [Hardware](docs/HARDWARE.md) · [UI](docs/UI_NAVIGATION.md) · [Module development](docs/MODULE_DEVELOPMENT.md) · [Privacy](docs/PRIVACY.md) · [Roadmap](docs/ROADMAP.md) · [Development](docs/DEVELOPMENT.md) · [Project history](docs/PROJECT_HISTORY.md)

## Licensing
The upstream repository has used MIT; retain the existing repository `LICENSE`. This archive does **not** relicense existing code. The FootHoled name/logo and DEF CON-exclusive enclosure/PCB artwork are **not** granted under the code license. See `docs/BRANDING.md`.

---

## Project updates (history)

**DEFCON 34 Update:** Project is incomplete at the moment, but the idea and desire to complete it are strong, it will just have to be post con. Whether you randomly found one of my stickers or if we spoke and I gave you one — keep an eye out over the next 4-6 weeks. In my rush to get it done before DC, I failed but after speaking with a few people, I am encouraged to see it through. I will return to the FootHoled development when I get home next week. Stay Hydrated and HACK THE PLANET!

**Post-DEFCON Update:** Ok, I've had a chance to look everything over and take a step back. The original idea was great for a "3 weeks until Defcon, can I complete it" but it lacked thinking some of it through. Why reinvent the wheel? In light of my most recent research, I'm taking this project in a slightly different direction. I am changing it to be a modular device, meaning any piece can be replaced or upgraded later. I am completely changing the hardware list. Moving up to a 3.5" screen and an ESP32-S3 with added GPIO options. It will have a 12-pin connector for add-on modules later. It will have GPS with stubby antenna, 2 wifi cards, a magnetometer, accelerometer, IR receiver and transmitter, sub-1 GHz, NFC and more. First I'm starting with the basics and the UI for it. As that progresses I will add more and more functionality until it's ready for download. No idea what the case design for this revision will look like, but having 16MB to work in will make my life easier. Having a slightly larger, capacitive touch screen doesn't hurt either. With the project change I will publish anew but will keep this in a historical branch for personal inspiration's sake. Updated current features list to reflect known changes. UI will likely be LVGL based. Buckle up, Buttercup.

**Latest:** This project is on hold as I try to tackle another project before DC35 — FAFO (Freq Around, Find Out). Details coming soon, waiting on slow boat from China so development on that can resume near the end of October. The plan is to have 100 units for sale at DC35.
