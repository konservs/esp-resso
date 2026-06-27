/**
 * @file pcf8574.c
 * @brief PCF8574 I2C input-expander driver (see pcf8574.h).
 */
#include "drivers/pcf8574.h"

#include "drivers/i2c_bus.h"
#include "drivers/pins.h"

#define I2C_TIMEOUT_MS 100

static i2c_master_dev_handle_t s_dev;

esp_err_t pcf8574_init(uint8_t i2c_addr)
{
    /* Share the bus the OLED also lives on; create it if we are first up. */
    esp_err_t err = i2c_bus_init(PIN_I2C_SDA, PIN_I2C_SCL);
    if (err != ESP_OK) {
        return err;
    }

    /* PCF8574 maxes out at 100 kHz; the new driver clocks each device
     * independently, so this is fine alongside the 400 kHz OLED. */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = i2c_addr,
        .scl_speed_hz = 100000,
    };
    err = i2c_master_bus_add_device(i2c_bus_handle(), &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        return err;
    }

    /* Write all-ones so every pin is in the high (input) state. */
    const uint8_t all_high = 0xFF;
    return i2c_master_transmit(s_dev, &all_high, 1, I2C_TIMEOUT_MS);
}

esp_err_t pcf8574_read(uint8_t *out)
{
    uint8_t port;
    esp_err_t err = i2c_master_receive(s_dev, &port, 1, I2C_TIMEOUT_MS);
    if (err == ESP_OK) {
        *out = port;
    }
    return err;
}
