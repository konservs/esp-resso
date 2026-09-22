#include "hal/hal_temp.h"

#include "driver/spi_master.h"

#include "core/rtd.h"
#include "drivers/max31865.h"
#include "drivers/pins.h"

/* PT100 with a 430 ohm reference resistor (the common Adafruit value). The
 * MAX31865 is ratiometric, so this value's absolute error lands directly on the
 * temperature: at ~110 ohm (roughly 26 C) 1% here is about 2.9 C of reading. If
 * you fit anything other than a precision part, measure it and put the measured
 * value here — that costs nothing and buys back most of the accuracy. */
#define RTD_RREF 430.0f

/* Both boilers use 3-wire probes. This must match what is soldered: a 3-wire
 * probe read in 2-wire mode reports a permanent open-circuit fault. */
#define RTD_WIRES MAX31865_3WIRE

#define RTD_SPI_HOST SPI2_HOST

static max31865_t s_brew;
static max31865_t s_steam;

espresso_result_t hal_temp_init(void)
{
    spi_bus_config_t bus = { 0 };
    bus.sclk_io_num = PIN_SPI_SCLK;
    bus.mosi_io_num = PIN_SPI_MOSI;
    bus.miso_io_num = PIN_SPI_MISO;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;

    if (spi_bus_initialize(RTD_SPI_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
        return ESPRESSO_ERR_STATE;
    }
    if (max31865_add(&s_brew, RTD_SPI_HOST, PIN_RTD_BREW_CS, RTD_RREF, RTD_PT100_R0,
                     RTD_WIRES) != ESP_OK) {
        return ESPRESSO_ERR_STATE;
    }
    if (max31865_add(&s_steam, RTD_SPI_HOST, PIN_RTD_STEAM_CS, RTD_RREF, RTD_PT100_R0,
                     RTD_WIRES) != ESP_OK) {
        return ESPRESSO_ERR_STATE;
    }
    return ESPRESSO_OK;
}

hal_temp_reading_t hal_temp_read(hal_boiler_id_t boiler)
{
    hal_temp_reading_t r = { .celsius = 0.0f, .ok = false, .fault = 0 };
    max31865_t *dev = (boiler == HAL_BOILER_STEAM) ? &s_steam : &s_brew;
    r.ok = max31865_read_celsius(dev, &r.celsius, &r.fault);
    return r;
}
