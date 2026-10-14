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

/**
 * @brief How the probe is wired to the front-end.
 *
 * This must match the physical wiring (and the board's RTDIN/FORCE jumpers).
 * A mismatch is not a small error: a 3-wire probe read in 2-wire mode reports a
 * permanent open-circuit fault, because the chip expects the lead-compensation
 * connection it is not configured to use.
 *
 * The chip itself only distinguishes 3-wire from the other two — 2- and 4-wire
 * share a setting — but naming all three keeps the call site honest about what
 * is actually soldered.
 */
typedef enum {
    MAX31865_2WIRE = 0,
    MAX31865_3WIRE,
    MAX31865_4WIRE
} max31865_wires_t;

typedef struct {
    spi_device_handle_t spi;
    float rref;     /**< Reference resistor in ohms (e.g. 430 for PT100). */
    float rnominal; /**< RTD nominal resistance at 0 C (RTD_PT100_R0).    */
    uint8_t config; /**< Configuration byte held for this device, so a fault
                     *   clear rewrites its wiring mode rather than a default. */
} max31865_t;

/**
 * @brief Attach a MAX31865 to an already-initialised SPI bus.
 * @param wires How the probe is physically wired; see ::max31865_wires_t.
 */
esp_err_t max31865_add(max31865_t *dev, spi_host_device_t host, int cs_gpio,
                       float rref, float rnominal, max31865_wires_t wires);

/**
 * @brief Read and convert one temperature sample.
 * @param out_c     Receives the temperature on success.
 * @param out_fault Optional (may be NULL). On failure, receives the MAX31865
 *                  fault-status byte (register 0x07 bitmask); 0xFF if the SPI
 *                  read itself failed (no chip / bus fault); 0xFE if the config
 *                  register did not read back as written (chip unpowered, reset
 *                  or on a garbled link — the RTD word is then not trusted);
 *                  0x40 / 0x80 for a resistance outside the plausible band.
 *                  0 on success.
 * @return true on success; false if the chip reports an RTD fault (open/short).
 */
bool max31865_read_celsius(max31865_t *dev, temp_c_t *out_c, uint8_t *out_fault);

/**
 * @brief Bench diagnostic: read registers 0x00..0x07 verbatim.
 *
 * out[0] is the configuration register as the chip holds it (compare with what
 * was written: a mismatch means the SPI link is garbling), out[1..2] the raw RTD
 * word, out[3..6] the fault thresholds, out[7] the fault status.
 */
esp_err_t max31865_read_raw_regs(max31865_t *dev, uint8_t out[8]);

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_DRIVERS_MAX31865_H */
