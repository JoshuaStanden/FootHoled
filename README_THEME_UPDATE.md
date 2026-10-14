# FootHoled H1 UI update

Changes:
- Dark charcoal/purple LVGL styling for buttons and navigation panels.
- Original `assets/footholed-logo.png` converted to embedded RGB565 C image (`firmware/src/splash_logo.cpp`) for a 2-second nonblocking startup splash.
- Panel mirroring moved from `flush_cb()` to panel initialization, preserving the verified `true, false` setting.
- Previously calibrated IMU orientation mapping (3/2/1/0) preserved.

Build in the ESP-IDF 6.1 terminal:

    cd firmware
    idf.py -p COM7 build flash monitor

This source change has not been hardware-tested. The existing managed LVGL component is preserved in the full project ZIP.
