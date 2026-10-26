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

/** Consistent samples required, both to debounce and to call a probe settled. */
#define LEVEL_DEBOUNCE_N 3

/* One probe's filtering state. The two debouncers are mutually exclusive (a raw
 * read is exactly one of dry/wet/fault, so a stable "wet" and a stable "fault"
 * can never hold at once), so precedence between them is only a formality;
 * fault wins to fail safe. */
typedef struct {
    debounce_t        full;     /**< Stable "probe covered".                */
    debounce_t        fault;    /**< Stable "sense path faulted".           */
    hal_level_state_t last_raw; /**< Previous raw read, for the settle run. */
    uint8_t           run;      /**< Identical raw reads in a row so far.   */
    bool              settled;  /**< Latches once the first run completes.  */
} probe_filter_t;

static void probe_filter_init(probe_filter_t *p)
{
    /* The "full" debouncer starts true so that an unread probe can never read
     * as dry and open a fill valve blind; the "fault" one starts false.
     *
     * That seed is only safe for the *valve*, though. Published as if it were a
     * reading it would also satisfy the heaters' dry-fire interlock, which is
     * the opposite of safe — so it is not published. Until @c settled these are
     * seeds, not a measurement, and the probe reads HAL_LEVEL_UNKNOWN, which
     * both consumers fail closed on. See docs/safety.md. */
    debounce_init(&p->full, LEVEL_DEBOUNCE_N, true);
    debounce_init(&p->fault, LEVEL_DEBOUNCE_N, false);
    p->last_raw = HAL_LEVEL_UNKNOWN;
    p->run = 0;
    p->settled = false;
}

/* Feed one raw read; returns the state to publish.
 *
 * Until the probe has produced LEVEL_DEBOUNCE_N identical reads in a row this
 * returns ::HAL_LEVEL_UNKNOWN — "no reading yet" — rather than the debouncers'
 * seeded values, which is what used to make an empty boiler announce itself as
 * full for the first few hundred ms after boot. The gate latches open on that
 * first run and never closes again, so a genuine wet->dry transition later on
 * debounces normally instead of flickering back through "no reading".
 *
 * A probe so noisy that it never manages three identical reads in a row never
 * settles at all, and stays at "no reading" indefinitely. That is the intended
 * answer for a probe that cannot be believed: it is visible on the screen, and
 * the control loop holds that boiler untrusted meanwhile — heaters off, fill
 * shut. */
static hal_level_state_t probe_update(probe_filter_t *p, hal_level_state_t raw)
{
    const bool full = debounce_update(&p->full, raw == HAL_LEVEL_WET);
    const bool fault = debounce_update(&p->fault, raw == HAL_LEVEL_FAULT);

    if (!p->settled) {
        if (raw == p->last_raw) {
            if (++p->run >= LEVEL_DEBOUNCE_N) {
                p->settled = true;
            }
        } else {
            p->last_raw = raw;
            p->run = 1;
        }
        if (!p->settled) {
            return HAL_LEVEL_UNKNOWN;
        }
    }

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
     * sensing. Never returns; the probe atomics keep the ::HAL_LEVEL_UNKNOWN
     * they were seeded with, so the control loop treats both boilers as
     * untrusted and keeps the heaters off and the fill valves shut. */
    hal_level_debug_sweep();
#else
    /* Debounce the probes so the fill valves do not chatter, and hold both
     * probes at "no reading yet" until they have actually settled. */
    probe_filter_t brew;
    probe_filter_init(&brew);
#if STEAM_ENABLED
    probe_filter_t steam;
    probe_filter_init(&steam);
#endif

    TickType_t last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(LEVEL_PERIOD_MS));

        const hal_level_state_t brew_raw = hal_level_read(HAL_LEVEL_BREW);
        atomic_store(&app->brew_probe_state, probe_update(&brew, brew_raw));
#if STEAM_ENABLED
        const hal_level_state_t steam_raw = hal_level_read(HAL_LEVEL_STEAM);
        atomic_store(&app->steam_probe_state, probe_update(&steam, steam_raw));
#endif
        /* Steam not fitted: its probe is never driven and stays at the seeded
         * HAL_LEVEL_UNKNOWN, which the control loop treats as "no fill, no heat". */
        /* Stamp the publish time last: with seq-cst atomics, a reader that sees a
         * fresh timestamp is guaranteed to see the states from this iteration. */
        atomic_store(&app->level_update_ms, hal_time_ms());
    }
#endif
}
