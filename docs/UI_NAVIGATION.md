# UI navigation

## Primary dashboard (exactly three)
- **Recon**: Wi-Fi, Bluetooth/BLE, GNSS, NFC/RFID, infrared receive, radio interfaces. Tools show honest unavailable states until implemented.
- **Analyze**: observations, captured sessions, comparisons, saved logs, statistics, analysis tools. Empty states until data exists.
- **Expand**: module discovery, uninitialized modules, installed modules, manual module registration, setup wizard, module diagnostics. Auto-discovery is bus dependent; the prototype only simulates it.

Utility: **Settings** icon in toolbar (not fourth main tile): Display, Privacy, Storage, Device, Developer, About.

## Behavior
- Every screen has back and home navigation.
- All UI switches/sliders/buttons in the browser prototype respond; settings persist locally in browser storage, not on ESP32.
- A banner identifies demonstration data.
- Unimplemented operations show a clear status rather than fabricated results.
- Privacy mask length is 1–15, with a preview using synthetic identifiers.
