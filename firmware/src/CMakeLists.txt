idf_component_register(
    SRCS
        "main.cpp"
        "ui.cpp"
        "splash.cpp"
        "wifi_scan.cpp"
        "ble_scan.cpp"
        "board_adapter.cpp"
        "settings.cpp"
        "mcp23017.cpp"
        "ina228.cpp"
        "axp2101.cpp"
        "qmi8658.cpp"
        "recon_log.cpp"
        "sd_format.cpp"
        "diagnostics.cpp"
        # splash_logo.cpp defines the static RGB565 logo image
        # (footholed_splash_logo). ui.cpp's boot splash draws it via
        # lv_image_set_src(), so this MUST be compiled in or the link fails
        # with "undefined reference to footholed_splash_logo".
        "splash_logo.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        esp_driver_ledc
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_spi
        esp_lcd
        esp_timer
        esp_wifi
        esp_event
        esp_netif
        bt
        nvs_flash
        fatfs
        esp_driver_sdmmc
        spi_flash
        esp_psram
        lvgl
        espressif__esp_lcd_st7796
        espressif__esp_io_expander_tca9554
)
