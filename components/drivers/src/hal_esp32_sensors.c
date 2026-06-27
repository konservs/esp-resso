/**
 * @file hal_esp32_sensors.c
 * @brief Flow meter (pulse counter) and water-level probes.
 */
#include "hal/hal_flow.h"
#include "hal/hal_level.h"

#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_rom_sys.h"

#include "drivers/pins.h"

/* Flow meter K-factor. TODO: calibrate against a measured volume for your
 * specific meter (e.g. weigh the output and divide pulses by grams). */
#define FLOW_PULSES_PER_ML 5.0f

static pcnt_unit_handle_t s_flow_unit;

espresso_result_t hal_flow_init(void)
{
    pcnt_unit_config_t unit_cfg = {
        .high_limit = 32767,
        .low_limit = -1,
    };
    if (pcnt_new_unit(&unit_cfg, &s_flow_unit) != ESP_OK) {
        return ESPRESSO_ERR_STATE;
    }

    pcnt_chan_config_t chan_cfg = {
        .edge_gpio_num = PIN_FLOW_PULSE,
        .level_gpio_num = -1,
    };
    pcnt_channel_handle_t chan = NULL;
    if (pcnt_new_channel(s_flow_unit, &chan_cfg, &chan) != ESP_OK) {
        return ESPRESSO_ERR_STATE;
    }
    pcnt_channel_set_edge_action(chan, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                 PCNT_CHANNEL_EDGE_ACTION_HOLD);

    if (pcnt_unit_enable(s_flow_unit) != ESP_OK ||
        pcnt_unit_clear_count(s_flow_unit) != ESP_OK ||
        pcnt_unit_start(s_flow_unit) != ESP_OK) {
        return ESPRESSO_ERR_STATE;
    }
    return ESPRESSO_OK;
}

uint32_t hal_flow_pulses(void)
{
    int count = 0;
    pcnt_unit_get_count(s_flow_unit, &count);
    return (uint32_t)(count < 0 ? 0 : count);
}

float hal_flow_ml(void)
{
    return (float)hal_flow_pulses() / FLOW_PULSES_PER_ML;
}

void hal_flow_reset(void)
{
    pcnt_unit_clear_count(s_flow_unit);
}

/* -------------------------------------------------------------------------- */
/* Water level - isolated H-bridge AC conductivity sensing.                    */
/* See docs/level-sensing.md.                                                  */
/*                                                                            */
/* Two pins drive an opto-isolated H-bridge (floating 12 V) anti-phase, so the */
/* probe sees symmetric AC across probe<->shell with zero net DC (no           */
/* electrolysis). When water conducts, a per-probe AC optocoupler asserts a    */
/* DIGITAL sense input. Sensing is pulsed: a short anti-phase burst per read,  */
/* then both drive pins idle low. EXC_A and EXC_B are NEVER both high (that    */
/* would short the bridge) - prefer a driver IC with dead-time as well.        */
/* -------------------------------------------------------------------------- */

#define LEVEL_EXC_CYCLES   8     /* anti-phase cycles per read            */
#define LEVEL_SETTLE_US    200   /* opto + 12 V side settle before sample  */
/* Of the 2*LEVEL_EXC_CYCLES half-cycle samples, this many "conducting"
 * readings mean WET. Calibrate on the bench. */
#define LEVEL_WET_MIN_HITS 6

/* Sense input GPIO for a boiler probe (opto-coupler output). */
static int sense_gpio(hal_level_id_t id)
{
    switch (id) {
    case HAL_LEVEL_BREW:  return PIN_LEVEL_BREW;
    case HAL_LEVEL_STEAM: return PIN_LEVEL_STEAM;
    default:              return -1;
    }
}

/* Opto output is active-low (its transistor pulls the pulled-up input to GND
 * while the water conducts). Invert here if your optocoupler wiring differs. */
static bool conducting(int gpio)
{
    return gpio_get_level(gpio) == 0;
}

espresso_result_t hal_level_init(void)
{
    /* H-bridge inputs, both idle low (bridge off). */
    gpio_config_t exc = {
        .pin_bit_mask = (1ULL << PIN_LEVEL_EXC_A) | (1ULL << PIN_LEVEL_EXC_B),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&exc);
    gpio_set_level(PIN_LEVEL_EXC_A, 0);
    gpio_set_level(PIN_LEVEL_EXC_B, 0);

    /* Sense + reservoir inputs. These need EXTERNAL pull-ups (GPIO35/36/39 are
     * input-only with no internal pulls); the opto output / float switch pull
     * the line to GND when active. */
    gpio_config_t in = {
        .pin_bit_mask = (1ULL << PIN_LEVEL_BREW) | (1ULL << PIN_LEVEL_STEAM) |
                        (1ULL << PIN_LEVEL_RESERVOIR),
        .mode = GPIO_MODE_INPUT,
    };
    gpio_config(&in);
    return ESPRESSO_OK;
}

/* Run an anti-phase burst and count conducting half-cycles. */
static int count_conducting(int sense)
{
    int hits = 0;
    for (int i = 0; i < LEVEL_EXC_CYCLES; i++) {
        /* Phase +: A high, B low (drop B first to avoid overlap). */
        gpio_set_level(PIN_LEVEL_EXC_B, 0);
        gpio_set_level(PIN_LEVEL_EXC_A, 1);
        esp_rom_delay_us(LEVEL_SETTLE_US);
        if (conducting(sense)) {
            hits++;
        }
        /* Phase -: B high, A low. */
        gpio_set_level(PIN_LEVEL_EXC_A, 0);
        gpio_set_level(PIN_LEVEL_EXC_B, 1);
        esp_rom_delay_us(LEVEL_SETTLE_US);
        if (conducting(sense)) {
            hits++;
        }
    }
    /* Idle the bridge (both low) - pulsed sensing. */
    gpio_set_level(PIN_LEVEL_EXC_A, 0);
    gpio_set_level(PIN_LEVEL_EXC_B, 0);
    return hits;
}

bool hal_level_present(hal_level_id_t level)
{
    if (level == HAL_LEVEL_RESERVOIR) {
        /* Float switch closed to GND => water present. Adjust polarity to
         * match your switch wiring. */
        return gpio_get_level(PIN_LEVEL_RESERVOIR) == 0;
    }

    const int sense = sense_gpio(level);
    if (sense < 0) {
        return true; /* unknown probe: fail safe against overfilling */
    }
    return count_conducting(sense) >= LEVEL_WET_MIN_HITS;
}
