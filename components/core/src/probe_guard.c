#include "core/probe_guard.h"

void probe_guard_init(probe_guard_t *g, uint8_t trip)
{
    g->trip = (trip == 0) ? PROBE_GUARD_DEFAULT_TRIP : trip;
    g->bad_run = 0;
    g->have_good = false;
    g->last_good_c = 0.0f;
}

probe_guard_out_t probe_guard_update(probe_guard_t *g, bool ok, temp_c_t celsius,
                                     uint8_t fault)
{
    probe_guard_out_t out = { .ok = false, .celsius = 0.0f, .fault = fault, .held = false };

    if (ok) {
        g->bad_run = 0;
        g->have_good = true;
        g->last_good_c = celsius;
        out.ok = true;
        out.celsius = celsius;
        out.fault = 0;
        return out;
    }

    /* Saturate rather than wrap: a probe that stays bad must stay tripped. */
    if (g->bad_run < UINT8_MAX) {
        g->bad_run++;
    }

    if (g->have_good && g->bad_run < g->trip) {
        out.ok = true;
        out.celsius = g->last_good_c;
        out.held = true;
    }
    return out;
}
