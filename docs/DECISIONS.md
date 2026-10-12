# Architecture decision log

- 2026-10-11: Initial release target is `dev`, not `main`; experimental implementations permitted.
- Home UI has exactly three main destinations: Recon, Analyze, Expand. Settings is a toolbar utility.
- Expand discovers and initializes newly connected/uninitialized modules; some modules need manual setup.
- Use Waveshare ESP32-S3-Touch-LCD-3.5B as target; do not substitute the non-B 3.5 BSP.
- Keep approved PNG only and original README ASCII header.
- Distinguish owned/received/planned/legacy and tested/unverified hardware status.
- Browser prototype is immediately navigable; LVGL firmware still needs verified board support.
