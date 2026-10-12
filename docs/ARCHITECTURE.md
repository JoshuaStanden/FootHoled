# Architecture

`prototype/` is a dependency-free browser interaction prototype. It is **not** the firmware and does not read real hardware.

`firmware/src/ui.cpp` defines LVGL screens and navigation. `firmware/src/main.cpp` initializes LVGL and runs the UI task in a **headless scaffold** until `firmware/src/board_adapter.cpp` is replaced with Waveshare 3.5B display/touch registration. The firmware scaffold must not be represented as operational on the LCD.

Future layers:

```
LVGL UI -> navigation / settings -> service interfaces -> hardware drivers
                                      |-> observation store
                                      |-> privacy formatter
                                      |-> module registry / Expand
```

No UI event callback should own a bus or perform a long scan. Use async worker tasks, a bounded event queue and UI-thread updates. All external interfaces should return `UNAVAILABLE`, `NOT_INITIALIZED`, `BUSY`, or `OK` with explicit status. Module descriptors need stable ID, supported buses, driver version, capability list, initialization state, and diagnostics.

Firmware driver porting priority: AXS15231B display + touch, LVGL tick and flush, then NVS, I2C, INA228, MCP23017, GNSS UART, SD, remaining radios. Avoid changing board support based on assumptions from a non-B board.
