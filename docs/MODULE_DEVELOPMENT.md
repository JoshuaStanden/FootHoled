# Module development and Expand

Define module metadata: `id`, `display_name`, `bus`, `driver_version`, `capabilities`, `state`, `error`, `setup_steps`. Lifecycle: `unknown -> discovered -> uninitialized -> initializing -> ready` or `error/disabled`. `Expand` shows all states and allows manual selection where bus discovery is impossible.

Safe I2C probing may enumerate responding addresses but **cannot reliably identify chips from address alone**; identification must use known device IDs or explicit user selection. SPI and UART generally require manual configuration or dedicated identification hardware. No probing on unknown GPIO or voltage rails.

A driver should expose `probe()`, `initialize(config)`, `self_test()`, `shutdown()`, and `capabilities()`. UI calls a registry/service, not driver internals. Demonstration modules in the browser are synthetic and not actual scans.
