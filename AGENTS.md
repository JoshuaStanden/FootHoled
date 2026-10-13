# Instructions for contributors and AI coding assistants

1. This package targets the **dev** branch. Never force-push or modify `main` as part of this work.
2. Read `hardware/components.yaml`, `docs/ARCHITECTURE.md`, and `docs/UI_NAVIGATION.md` before changing source.
3. Target **Waveshare ESP32-S3-Touch-LCD-3.5** (the NON-B / plain 3.5 model), not the similarly named 3.5B. Display controller **ST7796 (standard SPI)**, touch controller **FT6336 (I2C)**. The LCD/touch reset lines route through a **TCA9554 I2C expander**. Never substitute the 3.5B's AXS15231B QSPI driver, or ST7789/ILI9488 drivers, without verification.
4. Do not invent GPIO mappings, voltage ratings, pinouts, board revisions, or hardware support. Mark unknowns `unverified`.
5. The only primary home-screen destinations are **Recon**, **Analyze**, **Expand**. Settings is a utility destination.
6. Separate UI and hardware services. UI may display **DEMO** data but must never claim it is live.
7. Expand discovers new/uninitialized modules where buses permit; SPI/UART devices may require manual registration. No blind probing that can damage hardware.
8. Maintain privacy masking at presentation boundaries. Raw logs are sensitive; don't expose raw identifiers in demos, exports or telemetry by default.
9. No active RF operations without explicit authorized user action. No covert collection or cloud uploads.
10. For each change update docs, status, test notes and build instructions. Prefer small modules and descriptive names.
11. The PNG logo is the only approved logo asset. Do not generate other variants. DEF CON-exclusive designs remain outside this repository.
12. **Do not claim board firmware works until it has been compiled, flashed, and tested on the actual hardware.**

## Commenting and maintainability conventions

- Prefer descriptive names, small functions, and clear interfaces over narrating every statement.
- Document the **why**, non-obvious hardware constraints, ownership/lifetime, units, thread safety, and failure behavior.
- Add Doxygen-style `@file`, `@brief`, `@param`, and `@return` comments to public C/C++ APIs as they are implemented.
- Use `TODO(HW)`, `TODO(UI)`, or `TODO(TEST)` with a specific missing requirement; never imply a stub is complete.
- Distinguish simulated browser behavior, RAM-only firmware state, NVS persistence, and real hardware observations in comments and UI.
- Keep comments accurate when refactoring; remove stale comments instead of preserving them for history.
- Avoid commenting obvious syntax, duplicating documentation verbatim, or adding speculative electrical specifications.
