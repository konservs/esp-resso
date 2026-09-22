#include "core/safety.h"

void safety_init(safety_t *s, const safety_config_t *cfg)
{
    s->cfg = *cfg;
    s->trip = SAFETY_OK;
    s->heat_since = 0;
    s->timing_heat = false;
    s->fill_since = 0;
    s->timing_fill = false;
}

bool safety_tripped(const safety_t *s)
{
    return s->trip != SAFETY_OK;
}

safety_trip_t safety_trip(const safety_t *s)
{
    return s->trip;
}

void safety_clear(safety_t *s)
{
    s->trip = SAFETY_OK;
    s->timing_heat = false;
    s->timing_fill = false;
}

const char *safety_trip_name(safety_trip_t t)
{
    switch (t) {
    case SAFETY_OK:                return "OK";
    case SAFETY_TRIP_OVERTEMP:     return "OVERTEMP";
    case SAFETY_TRIP_SENSOR_FAULT: return "SENSOR_FAULT";
    case SAFETY_TRIP_HEAT_TIMEOUT: return "HEAT_TIMEOUT";
    case SAFETY_TRIP_FILL_TIMEOUT: return "FILL_TIMEOUT";
    default:                       return "?";
    }
}

/* Latch the first trip encountered; once tripped we never auto-recover. */
static void latch(safety_t *s, safety_trip_t t)
{
    if (s->trip == SAFETY_OK) {
        s->trip = t;
    }
}

safety_trip_t safety_update(safety_t *s, const safety_inputs_t *in, esp_ms_t now)
{
    if (s->trip != SAFETY_OK) {
        return s->trip; /* latched */
    }

    /* 0. No measurement yet. The supervisor runs faster and at a higher
     *    priority than the control loop, so its first ticks land before the
     *    loop has sampled anything: the inputs are still the zero-initialised
     *    defaults, and reading them as a faulted probe would latch a bogus
     *    SENSOR_FAULT on every power-up. Nothing is energised until control
     *    runs, so waiting costs no safety. */
    if (!in->sensors_valid) {
        s->timing_heat = false;
        s->timing_fill = false;
        return SAFETY_OK;
    }

    /* 1. Sensor integrity. A faulted sensor means we cannot trust the
     *    temperature, so we must cut the heaters. */
    if (!in->brew_sensor_ok || !in->steam_sensor_ok) {
        latch(s, SAFETY_TRIP_SENSOR_FAULT);
        return s->trip;
    }

    /* 2. Absolute over-temperature on either boiler. */
    if (in->brew_temp > s->cfg.max_temp || in->steam_temp > s->cfg.max_temp) {
        latch(s, SAFETY_TRIP_OVERTEMP);
        return s->trip;
    }

    /* 3. Heater runaway / dry-boiler detection: a heater driven continuously
     *    for longer than heat_timeout_ms without the temperature making
     *    progress indicates a dry boiler, a dead element, or a welded relay. */
    if (in->any_heater_on && !in->making_progress) {
        if (!s->timing_heat) {
            s->timing_heat = true;
            s->heat_since = now;
        } else if (espresso_elapsed_ms(s->heat_since, now) >= s->cfg.heat_timeout_ms) {
            latch(s, SAFETY_TRIP_HEAT_TIMEOUT);
            return s->trip;
        }
    } else {
        /* Heater off or temperature progressing: reset the timer. */
        s->timing_heat = false;
    }

    /* 4. Fill runaway: a boiler asking for water for longer than
     *    fill_timeout_ms without ever reading full. The fill command drops as
     *    soon as the probe reads covered, so an unbroken run this long means
     *    the water is not arriving — a leak, an empty or stuck reservoir float,
     *    a failed pump or valve, or a probe that never reads wet. Left running,
     *    that pumps water at a boiler indefinitely.
     *
     *    The timer measures *continuous* filling and resets whenever the fill
     *    stops, matching the heat timer above. A fill interrupted by the pump's
     *    duty-cycle guard therefore restarts the clock rather than accumulating
     *    across the rest. */
    if (s->cfg.fill_timeout_ms > 0 && in->any_fill_on) {
        if (!s->timing_fill) {
            s->timing_fill = true;
            s->fill_since = now;
        } else if (espresso_elapsed_ms(s->fill_since, now) >= s->cfg.fill_timeout_ms) {
            latch(s, SAFETY_TRIP_FILL_TIMEOUT);
            return s->trip;
        }
    } else {
        s->timing_fill = false;
    }

    return SAFETY_OK;
}
