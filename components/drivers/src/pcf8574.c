/**
 * @file pcf8574.c
 * @brief PCF8574 I2C input-expander driver (see pcf8574.h).
 */
#include "drivers/pcf8574.h"

#include "drivers/i2c_bus.h"
#include "drivers/pins.h"

#include "esp_log.h"

static const char *TAG = "pcf8574";

#define I2C_TIMEOUT_MS 100

/* Consecutive failed reads before we suspect the bus rather than the read.
 * The UI task polls once per 125 ms frame, so this is a bit under half a
 * second of dead buttons before recovery kicks in. */
#define FAIL_LIMIT 3

static i2c_master_dev_handle_t s_dev;
static uint8_t s_addr;
static int s_fails; /* consecutive failed reads */

/* Put every pin in the high (input) state. Quasi-bidirectional pins read as
 * inputs only while their output latch holds '1', so this has to be re-run
 * whenever the chip may have been reset or left mid-transfer — which is
 * exactly what the attach hook is for. */
static esp_err_t port_idle(void)
{
    const uint8_t all_high = 0xFF;
    return i2c_master_transmit(s_dev, &all_high, 1, I2C_TIMEOUT_MS);
}

static esp_err_t dev_detach(void)
{
    if (s_dev == NULL) {
        return ESP_OK;
    }
    const esp_err_t err = i2c_master_bus_rm_device(s_dev);
    if (err == ESP_OK) {
        s_dev = NULL;
    }
    return err;
}

static esp_err_t dev_attach(void)
{
    if (s_dev != NULL) {
        return ESP_OK;
    }
    /* The PCF8574 maxes out at 100 kHz; the new driver clocks each device
     * independently, so this is fine alongside the 400 kHz OLED. */
    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = s_addr,
        .scl_speed_hz = 100000,
    };
    esp_err_t err = i2c_master_bus_add_device(i2c_bus_handle(), &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        s_dev = NULL;
        return err;
    }
    return port_idle();
}

static const i2c_bus_client_t S_CLIENT = {
    .name = "pcf8574",
    .detach = dev_detach,
    .attach = dev_attach,
};

esp_err_t pcf8574_init(uint8_t i2c_addr)
{
    /* Share the bus the OLED also lives on; create it if we are first up. */
    esp_err_t err = i2c_bus_init(PIN_I2C_SDA, PIN_I2C_SCL);
    if (err != ESP_OK) {
        return err;
    }

    s_addr = i2c_addr;
    err = i2c_bus_register_client(&S_CLIENT);
    if (err != ESP_OK) {
        return err;
    }

    return dev_attach();
}

esp_err_t pcf8574_read(uint8_t *out)
{
    /* A NULL handle means a rebuild could not re-attach us. That still has to
     * feed the failure path below, or the one state only another rebuild can
     * clear would be the one state that never asks for one. */
    uint8_t port;
    const esp_err_t err = (s_dev == NULL)
                              ? ESP_ERR_INVALID_STATE
                              : i2c_master_receive(s_dev, &port, 1, I2C_TIMEOUT_MS);
    if (err == ESP_OK) {
        *out = port;
        s_fails = 0;
        i2c_bus_report_ok();
        return ESP_OK;
    }

    if (s_fails < FAIL_LIMIT) {
        s_fails++;
    }
    if (s_fails >= FAIL_LIMIT) {
        /* Paced inside i2c_bus_recover; most calls return without touching
         * the bus, so calling it on every failed read is cheap. */
        if (i2c_bus_recover()) {
            ESP_LOGW(TAG, "bus recovery run after %d failed read(s)", s_fails);
            s_fails = 0;
        }
    }
    return err;
}
