/**
 * @file hal_esp32_sensors.c
 * @brief Flow meter (pulse counter) and water-level probes.
 */
#include "hal/hal_flow.h"
#include "hal/hal_level.h"

#include "driver/gpio.h"
#include "driver/pulse_cnt.h"

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

static int level_gpio(hal_level_id_t id)
{
    switch (id) {
    case HAL_LEVEL_BREW:      return PIN_LEVEL_BREW;
    case HAL_LEVEL_STEAM:     return PIN_LEVEL_STEAM;
    case HAL_LEVEL_RESERVOIR: return PIN_LEVEL_RESERVOIR;
    default:                  return -1;
    }
}

espresso_result_t hal_level_init(void)
{
    const hal_level_id_t ids[] = { HAL_LEVEL_BREW, HAL_LEVEL_STEAM, HAL_LEVEL_RESERVOIR };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        const int gpio = level_gpio(ids[i]);
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << gpio,
            .mode = GPIO_MODE_INPUT,
            /* Input-only pins (34-39) need EXTERNAL pulls; configured none. */
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
        };
        gpio_config(&cfg);
    }
    return ESPRESSO_OK;
}

bool hal_level_present(hal_level_id_t level)
{
    const int gpio = level_gpio(level);
    if (gpio < 0) {
        return false;
    }
    /* Probe conducts to a high level when water is present.
     * TODO: invert here if your level circuit is active-low. */
    return gpio_get_level(gpio) != 0;
}
