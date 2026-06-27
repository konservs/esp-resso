/**
 * @file i2c_bus.h
 * @brief Single shared ESP32 I2C master bus for all on-board I2C peripherals.
 *
 * The SSD1306 OLED and the PCF8574 input expander sit on the same physical bus
 * (PIN_I2C_SDA / PIN_I2C_SCL). The ESP-IDF v5 I2C driver models this as one
 * bus handle with many device handles, so the bus must be created exactly once
 * and shared. This module owns that single bus.
 *
 * ::i2c_bus_init is idempotent and order-independent: whichever driver comes up
 * first creates the bus, the rest reuse it. Each device driver then calls
 * ::i2c_bus_handle and adds itself with i2c_master_bus_add_device().
 */
#ifndef ESPRESSO_DRIVERS_I2C_BUS_H
#define ESPRESSO_DRIVERS_I2C_BUS_H

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bring up the shared I2C master bus on @p sda_gpio / @p scl_gpio.
 *
 * Safe to call multiple times. The first call creates the bus; later calls are
 * no-ops and return ESP_OK (a mismatched pin request returns ESP_ERR_INVALID_STATE
 * so a wiring mistake is caught rather than silently ignored).
 */
esp_err_t i2c_bus_init(int sda_gpio, int scl_gpio);

/** @brief The shared bus handle, or NULL if ::i2c_bus_init has not succeeded. */
i2c_master_bus_handle_t i2c_bus_handle(void);

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_DRIVERS_I2C_BUS_H */
