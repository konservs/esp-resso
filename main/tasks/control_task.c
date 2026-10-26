/**
 * @file control_task.c
 * @brief Periodic control loop: temperature PIDs for both boilers and the
 *        brew/shot sequencer. Reads sensors and machine events, computes
 *        desired outputs from the portable core, and drives the actuators.
 */
#include "app.h"

#include "core/probe_guard.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hal/hal_flow.h"
#include "hal/hal_heater.h"
#include "hal/hal_level.h"
#include "hal/hal_pump.h"
#include "hal/hal_temp.h"
#include "hal/hal_time.h"
#include "hal/hal_valve.h"

static const char *TAG = "control";

/* Pump duty while auto-filling a boiler. The vibratory pump is on/off at this
 * HAL (any positive duty energises it); named so it has somewhere to go if
 * pulse-skip modulation lands. */
#define FILL_PUMP_DUTY 1.0f

/* True while the brew sequencer owns the pump. Nothing else may command it in
 * these states — an auto-fill topping up a boiler mid-shot would steal pressure
 * from the extraction. The bench override applies the same rule for the same
 * reason (see core/debug_pulse.h). */
static bool pump_owned_by_brew(machine_state_t s)
{
    return s == MACHINE_BREWING || s == MACHINE_BACKFLUSH;
}

static void drain_events(app_state_t *app)
{
    machine_event_t ev;
    while (xQueueReceive(app->events, &ev, 0) == pdTRUE) {
        machine_dispatch(&app->machine, ev);
    }
}

/* Report what the probe says, and only that: full / empty / faulted, plus the
 * two "there is no measurement" cases. Nothing about the reservoir, the machine
 * state or the fill decision belongs here — those are shown separately, and
 * mixing them in is what once made an empty reservoir look like a dead sensor.
 * The fill decision further down is computed from the same probe states, not
 * from this. */
static level_status_t level_of(bool published, bool fresh, hal_level_state_t probe)
{
    if (!published) {
        return LVL_INIT;  /* level task has not published anything yet */
    }
    if (!fresh) {
        return LVL_STALE; /* it published once, then stopped: stalled or dead */
    }
    switch (probe) {
    case HAL_LEVEL_WET:   return LVL_FULL;
    case HAL_LEVEL_DRY:   return LVL_EMPTY;
    case HAL_LEVEL_FAULT: return LVL_ERROR;
    default:              return LVL_INIT; /* UNKNOWN: still settling */
    }
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

    /* Flow-rate estimate: differentiate the accumulated volume and low-pass it
     * (the 5 pulses/ml meter is coarse at the 100 ms loop rate). */
    float prev_flow_ml = 0.0f;
    float flow_rate = 0.0f;
    const float FLOW_RATE_ALPHA = 0.25f;

    /* Last logged probe health, so the report below fires on change only. Seeded
     * to "ok" so the first bad reading is what gets logged, not the boot state. */
    bool prev_bt_ok = true, prev_st_ok = true;
    uint8_t prev_bt_fault = 0, prev_st_fault = 0;

    /* Ride through a short run of transient probe faults (lead pick-up, SSR /
     * pump switching) by holding the last good temperature; only a sustained
     * fault is reported upward, where the safety supervisor latches on it.
     * See core/probe_guard.h. */
    probe_guard_t brew_guard, steam_guard;
    probe_guard_init(&brew_guard, PROBE_GUARD_DEFAULT_TRIP);
    probe_guard_init(&steam_guard, PROBE_GUARD_DEFAULT_TRIP);

    /* Periodic probe readout for bench bring-up: the health report above fires
     * on change only, so a working front-end would otherwise never show its
     * number in the log (the display and dashboard are the normal consumers). */
    const TickType_t TEMP_LOG_PERIOD = pdMS_TO_TICKS(5000);
    TickType_t next_temp_log = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&last, period);

        /* Sample hardware outside the lock (SPI / pulse counter / GPIO). */
        const hal_temp_reading_t bt_raw = hal_temp_read(HAL_BOILER_BREW);
        const hal_temp_reading_t st_raw = hal_temp_read(HAL_BOILER_STEAM);
        float flow_ml = hal_flow_ml();

        /* Debounced view: everything below acts on bt/st, never on the raw
         * samples. A held sample keeps ok=true with the previous temperature
         * and fault=0 (the transient's code is logged separately). */
        const probe_guard_out_t bg =
            probe_guard_update(&brew_guard, bt_raw.ok, bt_raw.celsius, bt_raw.fault);
        const probe_guard_out_t sg =
            probe_guard_update(&steam_guard, st_raw.ok, st_raw.celsius, st_raw.fault);
        const hal_temp_reading_t bt = {
            .celsius = bg.celsius, .ok = bg.ok, .fault = bg.ok ? 0 : bg.fault
        };
        const hal_temp_reading_t st = {
            .celsius = sg.celsius, .ok = sg.ok, .fault = sg.ok ? 0 : sg.fault
        };
        /* One line per transient run, on its first masked sample: a probe that
         * glitches often is worth knowing about before it ever trips. */
        if (bg.held && brew_guard.bad_run == 1) {
            ESP_LOGI(TAG, "brew probe transient fault=0x%02X, holding %.1fC",
                     bg.fault, (double)bg.celsius);
        }
        if (sg.held && steam_guard.bad_run == 1) {
            ESP_LOGI(TAG, "steam probe transient fault=0x%02X, holding %.1fC",
                     sg.fault, (double)sg.celsius);
        }

        /* Boiler levels come from the level task (debounced, its own cadence) via
         * lock-free atomics — the control loop never blocks on a probe read. Read
         * the publish timestamp before the states: with seq-cst atomics a "fresh"
         * timestamp then implies the states are from that same publish. The
         * reservoir float switch is a plain GPIO with no drive-line coupling, so
         * it is read here directly. */
        const esp_ms_t level_ts = atomic_load(&app->level_update_ms);
        const hal_level_state_t brew_lvl = atomic_load(&app->brew_probe_state);
        const hal_level_state_t steam_lvl = atomic_load(&app->steam_probe_state);
        const bool reservoir_ok = hal_level_present(HAL_LEVEL_RESERVOIR);
        const esp_ms_t now = hal_time_ms();

        /* Distrust a level with no fresh reading: not published yet (UNKNOWN at
         * boot) or older than LEVEL_STALE_MS (level task stalled/died). An
         * untrusted probe forces that boiler's heaters off and its fill shut. */
        const bool level_published = (level_ts != 0);
        const bool level_fresh =
            level_published && espresso_elapsed_ms(level_ts, now) < LEVEL_STALE_MS;
        const bool brew_trusted = level_fresh && brew_lvl != HAL_LEVEL_UNKNOWN;
        const bool steam_trusted = level_fresh && steam_lvl != HAL_LEVEL_UNKNOWN;
        const bool brew_full = brew_trusted && brew_lvl == HAL_LEVEL_WET;
        const bool steam_full = steam_trusted && steam_lvl == HAL_LEVEL_WET;
        const bool brew_fault = brew_trusted && brew_lvl == HAL_LEVEL_FAULT;
        const bool steam_fault = steam_trusted && steam_lvl == HAL_LEVEL_FAULT;
        const bool brew_dry = brew_trusted && brew_lvl == HAL_LEVEL_DRY;
        const bool steam_dry = steam_trusted && steam_lvl == HAL_LEVEL_DRY;

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

            /* Dry-fire interlock: never energise a boiler's heaters unless its
             * water level is confirmed present. An uncovered probe, a faulted
             * one, or one with no settled reading yet all force that boiler
             * fully off until auto-fill covers the probe again. This is
             * the proactive guard; the safety supervisor's heat-timeout is only
             * the slow backstop. */
            if (!brew_full || brew_fault) {
                brew_duty = 0.0f;
            }
            if (!steam_full || steam_fault) {
                steam_duty = 0.0f;
            }

#if TEMP_SIM
            /* Simulated probes (see TEMP_SIM in hal/hal_temp.h): the readings
             * driving these PIDs — and the safety supervisor — are invented, so
             * ask for no heat at all. Zeroing here as well as at the HAL keeps
             * the telemetry and the supervisor's heat timer agreeing with what
             * the SSRs are really doing; otherwise it would sit timing a heat
             * that never happens and trip HEAT_TIMEOUT. */
            brew_duty = 0.0f;
            steam_duty = 0.0f;
#endif

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

        /* Live flow rate: differentiate the accumulated volume and low-pass it.
         * A shot resets the counter, so clamp the negative step to zero. */
        float dv = flow_ml - prev_flow_ml;
        prev_flow_ml = flow_ml;
        if (dv < 0.0f) {
            dv = 0.0f;
        }
        flow_rate += FLOW_RATE_ALPHA * (dv / dt_s - flow_rate);
        app->flow_rate_ml_s = flow_rate;

        /* Auto-fill decision (reused to drive the valves below).
         *
         * The reservoir is unpressurised, so the pump is what pushes water
         * through a fill solenoid: the valve and the pump are commanded
         * together, and an open valve on a stopped pump moves nothing. A
         * faulted probe still holds the valve shut — we can't trust the
         * reading, and filling on a bad "dry" reading risks overfilling.
         *
         * A fill is therefore pump work like any other, and is gated on the
         * same duty-cycle guard that gates a shot: none starts while the pump
         * is resting, and one already running stops the moment the guard trips.
         * It is decided here, above pump_guard_update(), so that the pump time
         * a fill spends is charged to the budget it is spending. */
        const bool can_fill = (state != MACHINE_FAULT) &&
                              !pump_owned_by_brew(state) && reservoir_ok &&
                              pump_guard_can_brew(&app->pump_guard);
        bool brew_filling = can_fill && brew_dry;
        bool steam_filling = can_fill && steam_dry;
        if (brew_filling || steam_filling) {
            pump_on = true;
            pump_duty = FILL_PUMP_DUTY;
        }

        /* Advance the pump's duty-cycle model with this cycle's command: a shot
         * or a fill fills the bucket, every other state drains it, so idle time
         * counts as rest. */
        pump_guard_update(&app->pump_guard, pump_on, now);
        app->pump_cooling = pump_guard_cooling(&app->pump_guard);
        app->pump_cooldown_ms = pump_guard_cooldown_ms(&app->pump_guard);

        app->brew_temp = bt;
        app->steam_temp = st;
        app->temp_update_ms = now;
        app->brew_duty = brew_duty;
        app->steam_duty = steam_duty;
        for (int i = 0; i < LOAD_HEATER_COUNT; i++) {
            app->heater_active[i] = heat.duty[i] > 0.0f;
        }
        app->both_ready = both_ready;

        /* The level view is the probe reading itself, so it stays truthful
         * whatever the fill logic above decided — and a bench pulse below
         * cannot make an empty boiler read as anything but empty. Whether a
         * fill is actually running is published separately, in valve_*_open. */
        app->brew_level = level_of(level_published, level_fresh, brew_lvl);
        app->steam_level = level_of(level_published, level_fresh, steam_lvl);
        app->reservoir_present = reservoir_ok;

        /* Admin bench override. Applied last so it wins over the normal
         * decisions above, but only over the pump and the fill valves — the
         * heaters are deliberately not exposed (see core/debug_pulse.h).
         *
         * The window is expired here rather than by whoever started it, so a
         * disconnected dashboard cannot leave an output energised. Re-check the
         * state every cycle too: a lever pulled mid-pulse moves us to BREWING,
         * and the brew sequencer must get the pump back immediately.
         *
         * This runs after pump_guard_update() above on purpose, so a couple of
         * seconds of bench testing does not count against the Ulka's brewing
         * duty-cycle budget. */
        debug_out_t dbg = debug_pulse_active(&app->debug_pulse, now);
        if (dbg != DEBUG_OUT_NONE && !debug_pulse_permitted(state)) {
            debug_pulse_stop(&app->debug_pulse);
            dbg = DEBUG_OUT_NONE;
        }
        switch (dbg) {
        case DEBUG_OUT_PUMP:       pump_on = true; pump_duty = 1.0f; break;
        case DEBUG_OUT_FILL_BREW:  brew_filling = true;  break;
        case DEBUG_OUT_FILL_STEAM: steam_filling = true; break;
        default: break;
        }
        app->debug_out = dbg;
        app->debug_ms = debug_pulse_remaining_ms(&app->debug_pulse, now);

        app->valve_brew_open = brew_filling;
        app->valve_steam_open = steam_filling;

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

        /* Boiler auto-fill: the pump above is already running for these (or for
         * a shot); the solenoid picks which boiler it feeds. */
        hal_valve_set(HAL_VALVE_FILL_BREW, brew_filling);
        hal_valve_set(HAL_VALVE_FILL_STEAM, steam_filling);

        /* Report ready-state edges to the machine. */
        if (both_ready && !prev_ready) {
            app_post_event(EV_BOTH_BOILERS_READY);
        } else if (!both_ready && prev_ready) {
            app_post_event(EV_NOT_READY);
        }
        prev_ready = both_ready;

        /* Report probe health on change only: a latched fault otherwise repeats
         * every cycle and buries everything else in the log. The fault byte is
         * the MAX31865's, or the driver's stand-in for a reading that is not a
         * temperature at all (0x40 near-zero resistance, 0x80 absurdly high,
         * 0xFE config did not read back as written, 0xFF the SPI transfer
         * itself failed). */
        if (bt.ok != prev_bt_ok || st.ok != prev_st_ok ||
            bt.fault != prev_bt_fault || st.fault != prev_st_fault) {
            if (!bt.ok || !st.ok) {
                ESP_LOGW(TAG, "sensor fault brew_ok=%d fault=0x%02X steam_ok=%d fault=0x%02X",
                         bt.ok, bt.fault, st.ok, st.fault);
            } else {
                ESP_LOGI(TAG, "both temperature probes reading OK");
            }
            prev_bt_ok = bt.ok;
            prev_st_ok = st.ok;
            prev_bt_fault = bt.fault;
            prev_st_fault = st.fault;
        }
        if ((int32_t)(xTaskGetTickCount() - next_temp_log) >= 0) {
            next_temp_log += TEMP_LOG_PERIOD;
            ESP_LOGI(TAG, "temp brew %.1fC ok=%d fault=0x%02X | steam %.1fC ok=%d fault=0x%02X",
                     (double)bt.celsius, bt.ok, bt.fault,
                     (double)st.celsius, st.ok, st.fault);
        }
    }
}
