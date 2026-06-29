/**
 * @file max31865.h
 * @brief Minimal driver for the MAX31865 RTD-to-digital converter (SPI).
 *
 * Handles the SPI transport and fault detection; the resistance-to-temperature
 * maths lives in the portable core (see core/rtd.h) so it can be unit-tested.
 */
#ifndef ESPRESSO_DRIVERS_MAX31865_H
#define ESPRESSO_DRIVERS_MAX31865_H

#include "core/types.h"
#include "driver/spi_master.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    spi_device_handle_t spi;
    float rref;     /**< Reference resistor in ohms (e.g. 430 for PT100). */
    float rnominal; /**< RTD nominal resistance at 0 C (RTD_PT100_R0).    */
} max31865_t;

/** Attach a MAX31865 to an already-initialised SPI bus. */
esp_err_t max31865_add(max31865_t *dev, spi_host_device_t host, int cs_gpio,
                       float rref, float rnominal);

/**
 * @brief Read and convert one temperature sample.
 * @param out_c     Receives the temperature on success.
 * @param out_fault Optional (may be NULL). On failure, receives the MAX31865
 *                  fault-status byte (register 0x07 bitmask), or 0xFF if the SPI
 *                  read itself failed (no chip / bus fault). 0 on success.
 * @return true on success; false if the chip reports an RTD fault (open/short).
 */
bool max31865_read_celsius(max31865_t *dev, temp_c_t *out_c, uint8_t *out_fault);

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_DRIVERS_MAX31865_H */
