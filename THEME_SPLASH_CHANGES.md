# FootHoled purple theme and startup splash

- `firmware/src/ui.cpp`: charcoal and purple button/panel styles, purple switches and slider, and a nonblocking two-second startup splash.
- `firmware/src/splash_logo.cpp` and `.h`: embedded RGB565 version of the supplied logo.
- `firmware/src/CMakeLists.txt`: compiles the new splash asset.
- `firmware/src/board_adapter.cpp`: calls `esp_lcd_panel_mirror(..., true, false)` once after panel initialization rather than on each LVGL flush.
- `assets/footholed-logo.png`: original user-supplied artwork.

Flash from the ESP-IDF terminal in `firmware`:

```powershell
idf.py -p COM7 build flash monitor
```

The existing calibrated IMU rotation mapping is unchanged. The touchscreen coordinates have not been independently verified for all rotations. No ESP-IDF compilation or hardware test was run in this environment.
