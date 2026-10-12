# Proposed FootHoled application API (design only)

Future applications stored in `/footholed/apps/<app_id>/` on SD. A manifest will declare ID, version, minimum API, entry point, permissions, and required capabilities. Runtime (Lua, WASM, or another constrained interpreter) is **not selected**. No arbitrary native binary loading is promised.

Proposed services: `ui`, `events`, `observations`, `storage`, `privacy`, `gnss`, `nfc`, `infrared`, `power`, `gpio_expander`, `module_registry`. Each needs versioning, permission checks, resource quotas, and a documented lifecycle. Raw identifiers require explicit permissions and are never silently included in exported logs.
