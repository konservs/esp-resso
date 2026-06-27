/**
 * @file pcf8574.h
 * @brief Minimal driver for a PCF8574 8-bit I2C I/O expander used for inputs.
 *
 * The PCF8574 has quasi-bidirectional pins: a pin reads as an input when its
 * output latch holds '1' (its weak ~100 uA pull-up sources current), and an
 * external contact to GND pulls it low. We drive all eight pins high once at
 * init and only ever read, so every line is a free input — buttons and
 * switches wire straight to GND, active-low, exactly like the old native GPIOs.
 *
 * The whole port is read in a single I2C byte transfer; ::pcf8574_read returns
 * that raw snapshot and the caller masks out the bits it cares about. Eight
 * inputs fit on one chip; chain a second expander (different address) for more.
 */
#ifndef ESPRESSO_DRIVERS_PCF8574_H
#define ESPRESSO_DRIVERS_PCF8574_H

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Attach the expander at @p i2c_addr to the shared I2C bus.
 *
 * Brings up the shared bus if needed, adds the device, and writes 0xFF so all
 * pins are in the high (input) state.
 */
esp_err_t pcf8574_init(uint8_t i2c_addr);

/**
 * @brief Read the raw 8-bit port (P0 in bit 0 .. P7 in bit 7).
 * @param[out] out Receives the port byte; a low bit means that pin is pulled to GND.
 * @return ESP_OK on success; on an I2C error @p out is left unchanged.
 */
esp_err_t pcf8574_read(uint8_t *out);

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_DRIVERS_PCF8574_H */
