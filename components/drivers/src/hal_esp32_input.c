/**
 * @file hal_esp32_input.c
 * @brief The two UI buttons and the brew/steam control switches.
 *
 * All are wired active-low to ground with the internal pull-ups enabled.
 * Reads are raw; the UI task polls slowly (~125 ms), which inherently filters
 * mechanical bounce, and core/buttons interprets gestures from there.
 */
#include "hal/hal_input.h"

#include "driver/gpio.h"

#include "drivers/pins.h"

static void config_input_pullup(int gpio)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&cfg);
}

espresso_result_t hal_input_init(void)
{
    config_input_pullup(PIN_BTN_A);
    config_input_pullup(PIN_BTN_B);
    config_input_pullup(PIN_SWITCH_BREW);
    config_input_pullup(PIN_SWITCH_STEAM);
    return ESPRESSO_OK;
}

/* Active-low: a pressed/engaged contact pulls the pin to 0. */
static bool pressed(int gpio)
{
    return gpio_get_level(gpio) == 0;
}

hal_buttons_t hal_buttons_read(void)
{
    hal_buttons_t b;
    b.a = pressed(PIN_BTN_A);
    b.b = pressed(PIN_BTN_B);
    return b;
}

bool hal_switch_brew(void)
{
    return pressed(PIN_SWITCH_BREW);
}

bool hal_switch_steam(void)
{
    return pressed(PIN_SWITCH_STEAM);
}
