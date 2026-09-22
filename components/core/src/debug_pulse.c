#include "core/debug_pulse.h"

#include <string.h>

/* Wire-format names. These appear in the HTTP API and in telemetry, so the
 * dashboard and the firmware agree on one spelling per output. */
static const char *const OUT_NAMES[DEBUG_OUT_COUNT] = {
    [DEBUG_OUT_NONE]       = "none",
    [DEBUG_OUT_PUMP]       = "pump",
    [DEBUG_OUT_FILL_BREW]  = "fill_brew",
    [DEBUG_OUT_FILL_STEAM] = "fill_steam",
};

void debug_pulse_init(debug_pulse_t *p)
{
    p->target = DEBUG_OUT_NONE;
    p->start = 0;
    p->duration_ms = 0;
}

bool debug_pulse_permitted(machine_state_t state)
{
    /* BREWING and BACKFLUSH both hand the pump to the brew sequencer; an
     * override would fight it for the same output mid-shot. Every other state,
     * FAULT included, leaves the pump and the fill valves idle, so a pulse has
     * them to itself. See the header for why FAULT is allowed. */
    return state != MACHINE_BREWING && state != MACHINE_BACKFLUSH;
}

bool debug_pulse_start(debug_pulse_t *p, debug_out_t out, esp_ms_t now,
                       uint32_t duration_ms)
{
    if (out <= DEBUG_OUT_NONE || out >= DEBUG_OUT_COUNT) {
        return false;
    }
    if (duration_ms > DEBUG_PULSE_MAX_MS) {
        duration_ms = DEBUG_PULSE_MAX_MS;
    }
    p->target = out;
    p->start = now;
    p->duration_ms = duration_ms;
    return true;
}

void debug_pulse_stop(debug_pulse_t *p)
{
    p->target = DEBUG_OUT_NONE;
    p->duration_ms = 0;
}

debug_out_t debug_pulse_active(debug_pulse_t *p, esp_ms_t now)
{
    if (p->target == DEBUG_OUT_NONE) {
        return DEBUG_OUT_NONE;
    }
    if (espresso_elapsed_ms(p->start, now) >= p->duration_ms) {
        debug_pulse_stop(p);
        return DEBUG_OUT_NONE;
    }
    return p->target;
}

uint32_t debug_pulse_remaining_ms(const debug_pulse_t *p, esp_ms_t now)
{
    if (p->target == DEBUG_OUT_NONE) {
        return 0;
    }
    const esp_ms_t elapsed = espresso_elapsed_ms(p->start, now);
    return (elapsed >= p->duration_ms) ? 0u : (p->duration_ms - elapsed);
}

const char *debug_pulse_out_name(debug_out_t out)
{
    if (out < 0 || out >= DEBUG_OUT_COUNT || OUT_NAMES[out] == NULL) {
        return "none";
    }
    return OUT_NAMES[out];
}

debug_out_t debug_pulse_out_from_name(const char *name)
{
    if (name == NULL) {
        return DEBUG_OUT_NONE;
    }
    for (int i = 0; i < DEBUG_OUT_COUNT; i++) {
        if (OUT_NAMES[i] != NULL && strcmp(OUT_NAMES[i], name) == 0) {
            return (debug_out_t)i;
        }
    }
    return DEBUG_OUT_NONE;
}
