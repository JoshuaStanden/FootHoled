# Architecture

`prototype/` is a dependency-free browser interaction prototype. It is **not** the firmware and does not read real hardware.

`firmware/src/ui.cpp` defines LVGL screens and navigation. `firmware/src/main.cpp` initializes LVGL and runs the UI task; `firmware/src/board_adapter.cpp` registers the Waveshare 3.5 (non-B) ST7796 display + FT6336 touch. Until compiled and verified on hardware, the firmware must not be represented as operational on the LCD.

Future layers:

```
LVGL UI -> navigation / settings -> service interfaces -> hardware drivers
                                      |-> observation store
                                      |-> privacy formatter
                                      |-> module registry / Expand
```

No UI event callback should own a bus or perform a long scan. Use async worker tasks, a bounded event queue and UI-thread updates. All external interfaces should return `UNAVAILABLE`, `NOT_INITIALIZED`, `BUSY`, or `OK` with explicit status. Module descriptors need stable ID, supported buses, driver version, capability list, initialization state, and diagnostics.

Firmware driver porting priority: ST7796 display + FT6336 touch (via TCA9554 reset), LVGL tick and flush, then NVS, I2C, INA228, MCP23017, GNSS UART, SD, remaining radios. This repo targets the non-B 3.5 board; do not reintroduce the 3.5B's AXS15231B QSPI driver.
