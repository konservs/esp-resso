# ESP32-only concrete drivers and HAL implementations. Builds only under
# ESP-IDF; the host test build never compiles this component.
idf_component_register(
    SRCS
        "src/hal_esp32.c"
        "src/hal_esp32_time.c"
        "src/hal_esp32_temp.c"
        "src/hal_esp32_actuators.c"
        "src/hal_esp32_sensors.c"
        "src/hal_esp32_input.c"
        "src/hal_esp32_display.c"
        "src/hal_esp32_storage.c"
        "src/max31865.c"
        "src/ssd1306.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        core
        espresso_hal
    PRIV_REQUIRES
        driver
        esp_timer
        nvs_flash)
