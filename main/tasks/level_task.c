/**
 * @file level_task.c
 * @brief Water-level sensing, decoupled from the control loop.
 *
 * The two boiler conductivity probes are sensed here — off the control loop —
 * because a read drives an isolated bipolar burst (see docs/level-sensing.md)
 * that owns the SELECT/ENABLE/REVERSE lines and busy-waits for a few ms. Running
 * it on its own task means:
 *
 *   - the control loop never blocks on a probe read; it just reads the latest
 *     debounced state from an atomic (::app_state_t::brew_probe_state /
 *     ::steam_probe_state), and
 *   - the sensing cadence (::LEVEL_PERIOD_MS) is tunable independently of the
 *     control period — slow it right down for bench work without disturbing
 *     control timing.
 *
 * This task is the sole owner of the probe drive lines. The reservoir float
 * switch is a plain GPIO with no drive-line coupling, so it stays in the
 * control loop.
 *
 * When ::LEVEL_DEBUG_SWEEP is set, this task instead runs the slow voltmeter
 * sweep (holding each drive state for whole seconds) and never publishes real
 * readings, so the probe atomics stay ::HAL_LEVEL_UNKNOWN — the control loop
 * then treats both boilers as untrusted (heaters off, fill valves shut).
 */
#include "app.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "core/filter.h"
#include "hal/hal_level.h"
#include "hal/hal_time.h"

#if !LEVEL_DEBUG_SWEEP
/* Collapse the two independent debouncers into one published state. They are
 * mutually exclusive (a raw read is exactly one of dry/wet/fault, so a stable
 * "wet" and a stable "fault" can never hold at once), so precedence here is only
 * a formality; fault wins to fail safe. */
static hal_level_state_t level_classify(bool full, bool fault)
{
    if (fault) {
        return HAL_LEVEL_FAULT;
    }
    return full ? HAL_LEVEL_WET : HAL_LEVEL_DRY;
}
#endif

void level_task(void *arg)
{
    (void)arg;
    app_state_t *app = &g_app;

#if LEVEL_DEBUG_SWEEP
    /* Bench mode: parade the drive states slowly for a voltmeter instead of
     * sensing. Never returns; the probe atomics keep their seeded "full" value
     * so the control loop keeps the fill valves shut. */
    hal_level_debug_sweep();
#else
    /* Debounce the probes so the fill valves do not chatter. The "full"
     * debouncers start true (assume full at boot so we never fill blind); the
     * "fault" ones start false. */
    debounce_t db_brew, db_steam, db_brew_fault, db_steam_fault;
    debounce_init(&db_brew, 3, true);
    debounce_init(&db_steam, 3, true);
    debounce_init(&db_brew_fault, 3, false);
    debounce_init(&db_steam_fault, 3, false);

    TickType_t last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(LEVEL_PERIOD_MS));

        const hal_level_state_t brew_raw = hal_level_read(HAL_LEVEL_BREW);
        const hal_level_state_t steam_raw = hal_level_read(HAL_LEVEL_STEAM);

        const bool brew_full = debounce_update(&db_brew, brew_raw == HAL_LEVEL_WET);
        const bool brew_fault = debounce_update(&db_brew_fault, brew_raw == HAL_LEVEL_FAULT);
        const bool steam_full = debounce_update(&db_steam, steam_raw == HAL_LEVEL_WET);
        const bool steam_fault = debounce_update(&db_steam_fault, steam_raw == HAL_LEVEL_FAULT);

        atomic_store(&app->brew_probe_state, level_classify(brew_full, brew_fault));
        atomic_store(&app->steam_probe_state, level_classify(steam_full, steam_fault));
        /* Stamp the publish time last: with seq-cst atomics, a reader that sees a
         * fresh timestamp is guaranteed to see the states from this iteration. */
        atomic_store(&app->level_update_ms, hal_time_ms());
    }
#endif
}
