/**
 * @file control_task.c
 * @brief Periodic control loop: temperature PIDs for both boilers and the
 *        brew/shot sequencer. Reads sensors and machine events, computes
 *        desired outputs from the portable core, and drives the actuators.
 */
#include "app.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "core/filter.h"

#include "hal/hal_flow.h"
#include "hal/hal_heater.h"
#include "hal/hal_level.h"
#include "hal/hal_pump.h"
#include "hal/hal_temp.h"
#include "hal/hal_time.h"
#include "hal/hal_valve.h"

static const char *TAG = "control";

static void drain_events(app_state_t *app)
{
    machine_event_t ev;
    while (xQueueReceive(app->events, &ev, 0) == pdTRUE) {
        machine_dispatch(&app->machine, ev);
    }
}

/* Classify a boiler probe for the diagnostics view, mirroring the fill logic. */
static level_status_t level_of(bool full, bool filling, bool reservoir_ok)
{
    if (full)          return LVL_FULL;
    if (filling)       return LVL_FILLING;
    if (!reservoir_ok) return LVL_ERROR; /* low but no water to fill from */
    return LVL_LOW;                       /* low, fill held off (e.g. fault) */
}

void control_task(void *arg)
{
    (void)arg;
    app_state_t *app = &g_app;
    const TickType_t period = pdMS_TO_TICKS(CONTROL_PERIOD_MS);
    const float dt_s = (float)CONTROL_PERIOD_MS / 1000.0f;

    TickType_t last = xTaskGetTickCount();
    bool prev_ready = false;
    machine_state_t prev_state = MACHINE_BOOT;

    /* Debounce the level probes so the fill valves do not chatter. */
    debounce_t db_brew, db_steam;
    debounce_init(&db_brew, 3, true);
    debounce_init(&db_steam, 3, true);

    for (;;) {
        vTaskDelayUntil(&last, period);

        /* Sample hardware outside the lock (SPI / pulse counter / GPIO). */
        const hal_temp_reading_t bt = hal_temp_read(HAL_BOILER_BREW);
        const hal_temp_reading_t st = hal_temp_read(HAL_BOILER_STEAM);
        float flow_ml = hal_flow_ml();
        const bool brew_full = debounce_update(&db_brew, hal_level_present(HAL_LEVEL_BREW));
        const bool steam_full = debounce_update(&db_steam, hal_level_present(HAL_LEVEL_STEAM));
        const bool reservoir_ok = hal_level_present(HAL_LEVEL_RESERVOIR);
        const esp_ms_t now = hal_time_ms();

        float brew_duty = 0.0f, steam_duty = 0.0f;
        bool both_ready = false;
        bool pump_on = false;
        float pump_duty = 0.0f;
        load_guard_output_t heat = { { 0.0f, 0.0f, 0.0f, 0.0f } };

        xSemaphoreTake(app->lock, portMAX_DELAY);
        /* Gate the lever/backflush transitions on the pump's duty-cycle guard
         * before draining this cycle's events, so a shot cannot start while the
         * pump is resting. */
        machine_set_pump_ready(&app->machine, pump_guard_can_brew(&app->pump_guard));
        drain_events(app);
        const machine_state_t state = machine_state(&app->machine);

        if (state != MACHINE_FAULT) {
            const boiler_output_t bo = boiler_update(&app->brew_boiler, bt.celsius, dt_s);
            const boiler_output_t so = boiler_update(&app->steam_boiler, st.celsius, dt_s);
            brew_duty = bt.ok ? bo.heater_duty : 0.0f;
            steam_duty = st.ok ? so.heater_duty : 0.0f;
            both_ready = bt.ok && st.ok && bo.at_setpoint && so.at_setpoint;

            /* Cap simultaneous heater elements to the mains budget: brew boiler
             * has priority (both elements while warming, lower-only once ready),
             * steam gets what remains. See core/load_guard.h. */
            const load_guard_input_t li = {
                .brew_duty = brew_duty,
                .steam_duty = steam_duty,
                .brew_at_setpoint = bt.ok && bo.at_setpoint,
            };
            heat = load_guard_apply(&li, app->settings.max_active_heaters);
        }

        /* Brew sequencing on entering/while/leaving BREWING. */
        if (state == MACHINE_BREWING) {
            if (prev_state != MACHINE_BREWING) {
                hal_flow_reset();
                brew_start(&app->brew, now);
                flow_ml = 0.0f;
            }
            const brew_output_t out = brew_update(&app->brew, now, flow_ml);
            pump_on = out.pump_on;
            pump_duty = out.pump_duty;
            app->shot_volume_ml = flow_ml;
            app->shot_elapsed_ms = out.elapsed_ms;
        } else if (prev_state == MACHINE_BREWING) {
            brew_stop(&app->brew);
        }

        /* Advance the pump's duty-cycle model with this cycle's command. It
         * only fills while brewing (the sole state that drives the pump) and
         * drains in every other state, so idle time counts as rest. */
        pump_guard_update(&app->pump_guard, pump_on, now);
        app->pump_cooling = pump_guard_cooling(&app->pump_guard);
        app->pump_cooldown_ms = pump_guard_cooldown_ms(&app->pump_guard);

        app->brew_temp = bt;
        app->steam_temp = st;
        app->brew_duty = brew_duty;
        app->steam_duty = steam_duty;
        for (int i = 0; i < LOAD_HEATER_COUNT; i++) {
            app->heater_active[i] = heat.duty[i] > 0.0f;
        }
        app->both_ready = both_ready;

        /* Auto-fill decision (reused to drive the valves below) + diagnostics. */
        const bool can_fill = (state != MACHINE_FAULT) && reservoir_ok;
        const bool brew_filling = can_fill && !brew_full;
        const bool steam_filling = can_fill && !steam_full;
        app->brew_level = level_of(brew_full, brew_filling, reservoir_ok);
        app->steam_level = level_of(steam_full, steam_filling, reservoir_ok);
        app->reservoir_present = reservoir_ok;

        prev_state = state;
        xSemaphoreGive(app->lock);

        /* Drive actuators. Each element runs the duty the load guard allotted
         * it (0 for any it held off). */
        hal_heater_set_duty(HAL_HEATER_BREW_LO,  heat.duty[LOAD_BREW_LO]);
        hal_heater_set_duty(HAL_HEATER_BREW_HI,  heat.duty[LOAD_BREW_HI]);
        hal_heater_set_duty(HAL_HEATER_STEAM_LO, heat.duty[LOAD_STEAM_LO]);
        hal_heater_set_duty(HAL_HEATER_STEAM_HI, heat.duty[LOAD_STEAM_HI]);
        if (pump_on) {
            hal_pump_set(pump_duty);
        } else {
            hal_pump_off();
        }

        /* Boiler auto-fill: top up a boiler whose probe is uncovered, but only
         * when the reservoir has water and the machine is not faulted. */
        hal_valve_set(HAL_VALVE_FILL_BREW, brew_filling);
        hal_valve_set(HAL_VALVE_FILL_STEAM, steam_filling);

        /* Report ready-state edges to the machine. */
        if (both_ready && !prev_ready) {
            app_post_event(EV_BOTH_BOILERS_READY);
        } else if (!both_ready && prev_ready) {
            app_post_event(EV_NOT_READY);
        }
        prev_ready = both_ready;

        if (!bt.ok || !st.ok) {
            ESP_LOGW(TAG, "sensor fault brew_ok=%d steam_ok=%d", bt.ok, st.ok);
        }
    }
}
