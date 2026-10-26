#include "hal/hal_temp.h"

#if TEMP_SIM

#include "esp_log.h"

/* Bench mode: no SPI, no MAX31865, no probes — just a fixed reading per boiler.
 * See the TEMP_SIM block in hal/hal_temp.h for what else this turns off. */
espresso_result_t hal_temp_init(void)
{
    ESP_LOGW("temp", "TEMP_SIM: RTDs NOT read — brew %.1fC / steam %.1fC simulated, "
                     "heaters disabled",
             (double)TEMP_SIM_BREW_C, (double)TEMP_SIM_STEAM_C);
    return ESPRESSO_OK;
}

hal_temp_reading_t hal_temp_read(hal_boiler_id_t boiler)
{
    const hal_temp_reading_t r = {
        .celsius = (boiler == HAL_BOILER_STEAM) ? TEMP_SIM_STEAM_C : TEMP_SIM_BREW_C,
        .ok = true,
        .fault = 0,
    };
    return r;
}

#else /* !TEMP_SIM — the real front-ends */

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "core/rtd.h"
#include "drivers/max31865.h"
#include "drivers/pins.h"

/* PT100 with a 400 ohm precision reference resistor (R13 / R14 as fitted on
 * the board; the schematic still says 430). The MAX31865 is ratiometric, so this
 * value's absolute error lands directly on the temperature: at ~110 ohm
 * (roughly 26 C) 1% here is about 2.9 C of reading. If a different part is ever
 * fitted, measure it and put the measured value here. */
#define RTD_RREF 400.0f

/* Both boilers use 3-wire probes. This must match what is soldered: a 3-wire
 * probe read in 2-wire mode reports a permanent open-circuit fault. */
#define RTD_WIRES MAX31865_3WIRE

#define RTD_SPI_HOST SPI2_HOST

static max31865_t s_brew;
static max31865_t s_steam;

/* Bench bring-up: while a probe reads as faulted, dump that front-end's
 * register map every few seconds so the fault can be told apart from a bad
 * SPI link (config readback != what was written) or a bad RTD hookup. */
#define TEMP_REG_DUMP 1
#define TEMP_REG_DUMP_PERIOD_US (5 * 1000 * 1000)

/* Bench bring-up: before the SPI bus comes up, hold the steam chip-select LOW
 * for a few seconds so its DC level can be read on U7 pin 16 with a meter. A
 * CS pulse during a real transfer is far too short to see. Set to 0 (or delete)
 * once the question is answered. */
#define TEMP_CS_BENCH_TEST 0
#define TEMP_CS_BENCH_HOLD_MS 15000

espresso_result_t hal_temp_init(void)
{
#if TEMP_CS_BENCH_TEST
    gpio_reset_pin(PIN_RTD_STEAM_CS);
    gpio_set_direction(PIN_RTD_STEAM_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_RTD_STEAM_CS, 0);
    ESP_LOGW("temp", "BENCH: steam CS (GPIO %d) held LOW for %d s - measure U7 pin 16 now",
             PIN_RTD_STEAM_CS, TEMP_CS_BENCH_HOLD_MS / 1000);
    vTaskDelay(pdMS_TO_TICKS(TEMP_CS_BENCH_HOLD_MS));
    gpio_set_level(PIN_RTD_STEAM_CS, 1);
    ESP_LOGW("temp", "BENCH: steam CS released HIGH, starting SPI");
#endif

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

#if TEMP_REG_DUMP
    static int64_t s_next_dump[2] = { 0, 0 };
    const int idx = (boiler == HAL_BOILER_STEAM) ? 1 : 0;
    const int64_t now = esp_timer_get_time();
    if (!r.ok && now >= s_next_dump[idx]) {
        s_next_dump[idx] = now + TEMP_REG_DUMP_PERIOD_US;
        uint8_t regs[8] = { 0 };
        const esp_err_t err = max31865_read_raw_regs(dev, regs);
        const uint16_t raw = (uint16_t)((regs[1] << 8) | regs[2]);
        ESP_LOGW("temp", "%s regs: cfg=0x%02X (wrote 0x%02X) rtd=0x%04X (adc=%u -> %.1f ohm) "
                         "hft=0x%02X%02X lft=0x%02X%02X fault=0x%02X spi=%s",
                 idx ? "steam" : "brew", regs[0], dev->config, raw, raw >> 1,
                 (double)(((float)(raw >> 1) * RTD_RREF) / 32768.0f),
                 regs[3], regs[4], regs[5], regs[6], regs[7], esp_err_to_name(err));
    }
#endif
    return r;
}

#endif /* TEMP_SIM */
