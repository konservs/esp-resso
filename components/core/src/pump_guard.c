#include "core/pump_guard.h"

/* Disabled when no ON budget is configured, or the rest time is degenerate
 * (which would make the drain rate meaningless / divide by zero). */
static bool cfg_enabled(const pump_guard_config_t *cfg)
{
    return cfg->on_max_ms > 0 && cfg->off_min_ms > 0;
}

static bool guard_enabled(const pump_guard_t *g)
{
    return cfg_enabled(&g->cfg);
}

void pump_guard_init(pump_guard_t *g, const pump_guard_config_t *cfg)
{
    g->cfg = *cfg;
    g->last_ms = 0;
    g->started = false;
    /* Start locked when enabled: we cannot know how hard the pump was worked
     * before this power-up, so assume some prior load and require
     * PUMP_STARTUP_REST_MS of rest before the first shot. Idle time — including
     * the boilers' warm-up — drains it, so a cold start rarely actually waits,
     * while a warm restart right after heavy use still holds off.
     *
     * Seed the bucket with the load that drains in exactly that time at the
     * configured rating, so the startup rest stays the same wall-clock wait
     * whatever the rating is. Capped at a full bucket, which is all the model
     * can represent (and all the pump can be carrying). */
    if (cfg_enabled(cfg)) {
        const uint64_t seed =
            (uint64_t)PUMP_STARTUP_REST_MS * cfg->on_max_ms / cfg->off_min_ms;
        g->heat_ms = (seed < cfg->on_max_ms) ? (uint32_t)seed : cfg->on_max_ms;
        /* A seed that rounds away to nothing means no startup rest at all —
         * don't latch cooling on an already-empty bucket. */
        g->cooling = (g->heat_ms > 0);
    } else {
        g->heat_ms = 0;
        g->cooling = false;
    }
}

void pump_guard_update(pump_guard_t *g, bool pump_on, esp_ms_t now)
{
    /* Seed the timestamp on the first call so the initial delta is 0. */
    if (!g->started) {
        g->started = true;
        g->last_ms = now;
        return;
    }

    const uint32_t dt = espresso_elapsed_ms(g->last_ms, now);
    g->last_ms = now;

    if (!guard_enabled(g) || dt == 0) {
        return;
    }

    if (pump_on) {
        /* Fill at 1 ms/ms, saturating at the bucket capacity. */
        const uint32_t headroom = g->cfg.on_max_ms - g->heat_ms;
        g->heat_ms += (dt < headroom) ? dt : headroom;
    } else {
        /* Drain at on_max_ms/off_min_ms per ms so a full bucket empties in
         * exactly off_min_ms. 64-bit intermediate avoids overflow. */
        const uint32_t drain =
            (uint32_t)((uint64_t)dt * g->cfg.on_max_ms / g->cfg.off_min_ms);
        g->heat_ms = (drain < g->heat_ms) ? (g->heat_ms - drain) : 0u;
    }

    /* Hysteresis: trip when the bucket fills, clear only once it has fully
     * drained — i.e. after a complete off_min_ms rest. */
    if (g->heat_ms >= g->cfg.on_max_ms) {
        g->cooling = true;
    } else if (g->heat_ms == 0) {
        g->cooling = false;
    }
}

bool pump_guard_can_brew(const pump_guard_t *g)
{
    return !g->cooling;
}

bool pump_guard_cooling(const pump_guard_t *g)
{
    return g->cooling;
}

uint32_t pump_guard_cooldown_ms(const pump_guard_t *g)
{
    if (!g->cooling || !guard_enabled(g)) {
        return 0;
    }
    /* Time to drain heat_ms to 0 at the drain rate, rounded up so a non-zero
     * remainder never displays as 0. */
    const uint64_t num = (uint64_t)g->heat_ms * g->cfg.off_min_ms;
    return (uint32_t)((num + g->cfg.on_max_ms - 1) / g->cfg.on_max_ms);
}
