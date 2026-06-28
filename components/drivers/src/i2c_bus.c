/**
 * @file i2c_bus.c
 * @brief Implementation of the single shared I2C master bus.
 */
#include "drivers/i2c_bus.h"

#include "esp_log.h"

static const char *TAG = "i2c_bus";

static i2c_master_bus_handle_t s_bus;   /* NULL until the first init.        */
static int s_sda = -1, s_scl = -1;      /* Pins the bus was created on.      */

/* Probe every 7-bit address and log which devices ACK. Cheap bring-up aid:
 * expect the SSD1306 (0x3C) and the PCF8574 (0x20). Nothing here => a wiring
 * or pull-up problem; the expected address missing => that one chip. */
static void i2c_bus_scan(void)
{
    ESP_LOGI(TAG, "scanning bus on SDA=%d SCL=%d ...", s_sda, s_scl);
    int found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (i2c_master_probe(s_bus, addr, 50) == ESP_OK) {
            ESP_LOGI(TAG, "  device at 0x%02X", addr);
            found++;
        }
    }
    ESP_LOGI(TAG, "scan complete: %d device(s)", found);
}

esp_err_t i2c_bus_init(int sda_gpio, int scl_gpio)
{
    if (s_bus != NULL) {
        /* Already up — only valid if the caller wants the same pins. */
        return (sda_gpio == s_sda && scl_gpio == s_scl) ? ESP_OK
                                                        : ESP_ERR_INVALID_STATE;
    }

    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = -1, /* auto-select a free port */
        .sda_io_num = sda_gpio,
        .scl_io_num = scl_gpio,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        s_bus = NULL;
        return err;
    }
    s_sda = sda_gpio;
    s_scl = scl_gpio;
    i2c_bus_scan();
    return ESP_OK;
}

i2c_master_bus_handle_t i2c_bus_handle(void)
{
    return s_bus;
}
