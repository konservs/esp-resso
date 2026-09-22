/**
 * @file safety_task.c
 * @brief High-priority, independent safety supervisor.
 *
 * Runs faster and at a higher priority than the control loop. If any safety
 * rule trips it cuts all heaters immediately (without waiting for the control
 * task) and posts ::EV_FAULT so the machine latches into ::MACHINE_FAULT.
 *
 * NOTE: this is the firmware's *second* line of defence. The first line is
 * hardware — a thermal fuse on each boiler and a mechanical over-pressure
 * valve — which must protect the machine even if this firmware hangs. See
 * docs/safety.md.
 */
#include "app.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hal/hal_heater.h"
#include "hal/hal_time.h"

static const char *TAG = "safety";

void safety_task(void *arg)
{
    (void)arg;
    app_state_t *app = &g_app;
    const TickType_t period = pdMS_TO_TICKS(SAFETY_PERIOD_MS);

    TickType_t last = xTaskGetTickCount();
    temp_c_t prev_brew = 0.0f, prev_steam = 0.0f;
    bool have_prev = false;
    safety_trip_t prev_trip = SAFETY_OK;

    for (;;) {
        vTaskDelayUntil(&last, period);

        safety_inputs_t in;
        safety_trip_t trip;
        const esp_ms_t now = hal_time_ms();

        xSemaphoreTake(app->lock, portMAX_DELAY);
        /* This task runs twice as often as the control loop and at a higher
         * priority, so at boot it gets here before any temperature has been
         * sampled. Until the loop publishes its first reading the fields below
         * are zero-initialised placeholders, not measurements — say so rather
         * than let the supervisor read ok=false as a faulted probe and latch
         * SENSOR_FAULT on every power-up. Once readings do flow, one that has
         * gone stale means the control loop has stalled or died, and its last
         * temperature is no longer something we can act on: that IS a fault. */
        const esp_ms_t published = app->temp_update_ms;
        const bool sampled = (published != 0);
        const bool stale = sampled && espresso_elapsed_ms(published, now) >= TEMP_STALE_MS;

        in.sensors_valid = sampled;
        in.brew_temp = app->brew_temp.celsius;
        in.steam_temp = app->steam_temp.celsius;
        in.brew_sensor_ok = app->brew_temp.ok && !stale;
        in.steam_sensor_ok = app->steam_temp.ok && !stale;
        in.any_heater_on = (app->brew_duty > 0.0f) || (app->steam_duty > 0.0f);
        /* Progress = temperature climbing, or already within a ready band. */
        in.making_progress =
            !have_prev ||
            (in.brew_temp > prev_brew + 0.05f) ||
            (in.steam_temp > prev_steam + 0.05f) ||
            boiler_at_setpoint(&app->brew_boiler, in.brew_temp) ||
            boiler_at_setpoint(&app->steam_boiler, in.steam_temp);

        trip = safety_update(&app->safety, &in, now);
        prev_brew = in.brew_temp;
        prev_steam = in.steam_temp;
        have_prev = true;
        xSemaphoreGive(app->lock);

        if (trip != SAFETY_OK) {
            /* Keep cutting the heaters every cycle — that must not depend on an
             * edge — but announce the trip once. The trip is latched, so
             * re-posting EV_FAULT and re-logging at every tick only floods the
             * event queue and the console. */
            hal_heater_all_off();
            if (prev_trip == SAFETY_OK) {
                app_post_event(EV_FAULT);
                ESP_LOGE(TAG, "SAFETY TRIP: %s (brew %.1fC ok=%d, steam %.1fC ok=%d)",
                         safety_trip_name(trip), in.brew_temp, in.brew_sensor_ok,
                         in.steam_temp, in.steam_sensor_ok);
            }
        }
        prev_trip = trip;
    }
}
