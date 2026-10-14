#include "drivers/max31865.h"

#include <string.h>

#include "core/rtd.h"

/* Register map (subset). */
#define MAX31865_REG_CONFIG    0x00
#define MAX31865_REG_RTD_MSB   0x01
#define MAX31865_REG_FAULT     0x07
#define MAX31865_WRITE_FLAG    0x80
#define MAX31865_FAULT_CLEAR   0x02 /* config D1: clear the latched fault */

/* Config: Vbias on (D7), auto conversion (D6), 50 Hz filter (D0). The wiring
 * mode (D4) is added per device by max31865_add().
 * TODO: expose 50/60 Hz selection if this ever ships outside 50 Hz mains. */
#define MAX31865_CONFIG_BASE   0xC1
#define MAX31865_CFG_3WIRE     0x10 /* config D4: 3-wire lead compensation */
#define MAX31865_CONFIG_STATIC_MASK 0xF1 /* D7..D4 and D0: bits that must read back as written */

static esp_err_t write_reg(max31865_t *dev, uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = { (uint8_t)(reg | MAX31865_WRITE_FLAG), val };
    spi_transaction_t t = { 0 };
    t.length = 8 * sizeof(tx);
    t.tx_buffer = tx;
    return spi_device_polling_transmit(dev->spi, &t);
}

static esp_err_t read_regs(max31865_t *dev, uint8_t reg, uint8_t *out, size_t n)
{
    uint8_t tx[1 + 4] = { reg, 0, 0, 0, 0 };
    uint8_t rx[1 + 4] = { 0 };
    if (n > 4) {
        return ESP_ERR_INVALID_ARG;
    }
    spi_transaction_t t = { 0 };
    t.length = 8 * (n + 1);
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    const esp_err_t err = spi_device_polling_transmit(dev->spi, &t);
    if (err == ESP_OK) {
        memcpy(out, &rx[1], n);
    }
    return err;
}

esp_err_t max31865_add(max31865_t *dev, spi_host_device_t host, int cs_gpio,
                       float rref, float rnominal, max31865_wires_t wires)
{
    dev->rref = rref;
    dev->rnominal = rnominal;
    /* 2- and 4-wire share D4=0; only 3-wire enables lead compensation. */
    dev->config = MAX31865_CONFIG_BASE |
                  ((wires == MAX31865_3WIRE) ? MAX31865_CFG_3WIRE : 0);

    spi_device_interface_config_t cfg = { 0 };
    cfg.clock_speed_hz = 1 * 1000 * 1000; /* 1 MHz; MAX31865 max is ~5 MHz */
    cfg.mode = 1;                          /* CPOL=0, CPHA=1 */
    cfg.spics_io_num = cs_gpio;
    cfg.queue_size = 1;

    const esp_err_t err = spi_bus_add_device(host, &cfg, &dev->spi);
    if (err != ESP_OK) {
        return err;
    }
    return write_reg(dev, MAX31865_REG_CONFIG, dev->config);
}

esp_err_t max31865_read_raw_regs(max31865_t *dev, uint8_t out[8])
{
    /* The chip auto-increments the address, but read_regs() caps a burst at
     * four bytes, so take the map in two halves. */
    esp_err_t err = read_regs(dev, MAX31865_REG_CONFIG, &out[0], 4);
    if (err == ESP_OK) {
        err = read_regs(dev, MAX31865_REG_CONFIG + 4, &out[4], 4);
    }
    return err;
}

bool max31865_read_celsius(max31865_t *dev, temp_c_t *out_c, uint8_t *out_fault)
{
    if (out_fault != NULL) {
        *out_fault = 0;
    }

    /* Config and RTD word in one burst (the address auto-increments). The
     * config readback is the integrity check: a chip that is not holding what
     * was written is unpowered, reset, or on a garbled SPI link, and whatever
     * follows it is not a measurement even when it happens to decode to a
     * plausible resistance. Bits D3..D1 are self-clearing (fault-detect cycle,
     * fault clear) so they are masked out of the comparison. */
    uint8_t regs[3] = { 0 };
    if (read_regs(dev, MAX31865_REG_CONFIG, regs, sizeof(regs)) != ESP_OK) {
        if (out_fault != NULL) {
            *out_fault = 0xFF; /* SPI/bus failure — distinct from an RTD fault */
        }
        return false;
    }
    if ((regs[0] & MAX31865_CONFIG_STATIC_MASK) !=
        (dev->config & MAX31865_CONFIG_STATIC_MASK)) {
        if (out_fault != NULL) {
            *out_fault = 0xFE; /* config readback mismatch: chip absent/reset/garbled */
        }
        /* Re-arm in case the chip simply lost its configuration (brown-out,
         * reset); a healthy part then recovers on the next read. */
        write_reg(dev, MAX31865_REG_CONFIG, dev->config);
        return false;
    }

    const uint16_t raw = (uint16_t)((regs[1] << 8) | regs[2]);
    if (raw & 0x0001) {
        /* Bit 0 set => an RTD fault was detected; surface the detail and clear
         * the latch so a transient fault can recover on the next read. */
        if (out_fault != NULL) {
            uint8_t f = 0;
            read_regs(dev, MAX31865_REG_FAULT, &f, 1);
            *out_fault = f;
        }
        write_reg(dev, MAX31865_REG_CONFIG, dev->config | MAX31865_FAULT_CLEAR);
        return false;
    }

    const uint16_t adc = raw >> 1; /* 15-bit ratio */
    const float resistance = ((float)adc * dev->rref) / 32768.0f;

    /* A near-zero (or absurdly high) resistance is not a real temperature — it's
     * a dead/absent front-end returning all-zero registers (which would convert
     * to a bogus ~-247 C and read as valid), or an open/short the chip's own
     * detector didn't latch. Report it as a fault so the safety supervisor cuts
     * the heaters instead of trusting it. Reuse the MAX31865 RTD low/high
     * threshold bits so the fault decodes as "out of range". */
    if (!rtd_resistance_plausible(resistance, dev->rnominal)) {
        if (out_fault != NULL) {
            *out_fault = (resistance < dev->rnominal) ? 0x40 : 0x80;
        }
        return false;
    }

    *out_c = rtd_resistance_to_celsius(resistance, dev->rnominal);
    return true;
}
