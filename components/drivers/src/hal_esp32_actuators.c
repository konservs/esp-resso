/**
 * @file hal_esp32_actuators.c
 * @brief Heaters (slow-PWM over SSRs), the pump, and the solenoid valves.
 */
#include "hal/hal_heater.h"
#include "hal/hal_pump.h"
#include "hal/hal_valve.h"

#include "driver/gpio.h"
#include "esp_timer.h"

#include "core/types.h"
#include "drivers/pins.h"

/* Slow-PWM window: 100 steps of 10 ms => a 1 s window per heater. Each step the
 * timer compares a rolling counter against the commanded duty. For tighter
 * mains synchronisation, drive these transitions from a zero-cross interrupt. */
#define HEATER_STEPS    100
#define HEATER_STEP_US  10000

static const int s_heater_gpio[HAL_BOILER_COUNT] = {
    [HAL_BOILER_BREW] = PIN_SSR_BREW,
    [HAL_BOILER_STEAM] = PIN_SSR_STEAM,
};
static volatile float s_heater_duty[HAL_BOILER_COUNT];
static esp_timer_handle_t s_pwm_timer;

static int valve_gpio(hal_valve_id_t v)
{
    switch (v) {
    case HAL_VALVE_FILL_BREW:  return PIN_VALVE_FILL_BREW;
    case HAL_VALVE_FILL_STEAM: return PIN_VALVE_FILL_STEAM;
    default:                   return -1;
    }
}

static void pwm_tick(void *arg)
{
    (void)arg;
    static uint32_t step;
    step = (step + 1) % HEATER_STEPS;
    for (int i = 0; i < HAL_BOILER_COUNT; i++) {
        const uint32_t on_steps = (uint32_t)(s_heater_duty[i] * HEATER_STEPS);
        gpio_set_level(s_heater_gpio[i], step < on_steps ? 1 : 0);
    }
}

espresso_result_t hal_heater_init(void)
{
    for (int i = 0; i < HAL_BOILER_COUNT; i++) {
        gpio_reset_pin(s_heater_gpio[i]);
        gpio_set_direction(s_heater_gpio[i], GPIO_MODE_OUTPUT);
        gpio_set_level(s_heater_gpio[i], 0);
        s_heater_duty[i] = 0.0f;
    }
    const esp_timer_create_args_t args = {
        .callback = pwm_tick,
        .name = "heater_pwm",
    };
    if (esp_timer_create(&args, &s_pwm_timer) != ESP_OK) {
        return ESPRESSO_ERR_STATE;
    }
    return esp_timer_start_periodic(s_pwm_timer, HEATER_STEP_US) == ESP_OK
               ? ESPRESSO_OK
               : ESPRESSO_ERR_STATE;
}

void hal_heater_set_duty(hal_boiler_id_t boiler, float duty)
{
    if (boiler < HAL_BOILER_COUNT) {
        s_heater_duty[boiler] = espresso_clampf(duty, 0.0f, 1.0f);
    }
}

void hal_heater_all_off(void)
{
    for (int i = 0; i < HAL_BOILER_COUNT; i++) {
        s_heater_duty[i] = 0.0f;
        gpio_set_level(s_heater_gpio[i], 0);
    }
}

espresso_result_t hal_pump_init(void)
{
    gpio_reset_pin(PIN_PUMP);
    gpio_set_direction(PIN_PUMP, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_PUMP, 0);
    return ESPRESSO_OK;
}

void hal_pump_set(float duty)
{
    /* On/off for a plain vibratory pump. TODO: phase-angle / pulse-skip
     * modulation for true flow control during pre-infusion. */
    gpio_set_level(PIN_PUMP, duty > 0.0f ? 1 : 0);
}

void hal_pump_off(void)
{
    gpio_set_level(PIN_PUMP, 0);
}

espresso_result_t hal_valve_init(void)
{
    for (hal_valve_id_t v = 0; v < HAL_VALVE_COUNT; v++) {
        const int gpio = valve_gpio(v);
        if (gpio >= 0) {
            gpio_reset_pin(gpio);
            gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
            gpio_set_level(gpio, 0);
        }
    }
    return ESPRESSO_OK;
}

void hal_valve_set(hal_valve_id_t valve, bool open)
{
    const int gpio = valve_gpio(valve);
    if (gpio >= 0) {
        gpio_set_level(gpio, open ? 1 : 0);
    }
}
