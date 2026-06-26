#include "core/brew.h"

const char *brew_profile_name(brew_profile_type_t type)
{
    switch (type) {
    case BREW_PROFILE_MANUAL: return "Manual";
    case BREW_PROFILE_AUTO:   return "Auto";
    default:                  return "?";
    }
}

void brew_profile_build(brew_profile_t *out, brew_profile_type_t type,
                        const brew_params_t *p)
{
    out->type = type;
    out->target_volume_ml = p->target_volume_ml;
    out->max_shot_ms = p->max_shot_ms;
    out->stage_count = 0;

    if (type == BREW_PROFILE_AUTO) {
        /* Pre-infusion: gentle pump for a fixed time. */
        if (p->preinfuse_ms > 0) {
            brew_stage_t s = { 0 };
            s.mode = BREW_PUMP_DUTY;
            s.pump_duty = p->preinfuse_pump;
            s.end = BREW_END_TIME;
            s.duration_ms = p->preinfuse_ms;
            out->stages[out->stage_count++] = s;
        }
        /* Bloom/hold: pump off for a fixed time. */
        if (p->hold_ms > 0) {
            brew_stage_t s = { 0 };
            s.mode = BREW_PUMP_OFF;
            s.end = BREW_END_TIME;
            s.duration_ms = p->hold_ms;
            out->stages[out->stage_count++] = s;
        }
    }

    /* Extraction: full pump until the lever is released or a cap trips.
     * This is also the single stage of the MANUAL profile. */
    {
        brew_stage_t s = { 0 };
        s.mode = BREW_PUMP_DUTY;
        s.pump_duty = p->extract_pump;
        s.end = BREW_END_MANUAL;
        out->stages[out->stage_count++] = s;
    }
}

void brew_init(brew_t *b, const brew_profile_t *profile)
{
    b->profile = *profile;
    b->stage = 0;
    b->shot_start = 0;
    b->stage_start = 0;
    b->running = false;
}

void brew_start(brew_t *b, esp_ms_t now)
{
    b->stage = 0;
    b->shot_start = now;
    b->stage_start = now;
    b->running = b->profile.stage_count > 0;
}

void brew_stop(brew_t *b)
{
    b->running = false;
}

bool brew_active(const brew_t *b)
{
    return b->running;
}

static brew_output_t stopped_output(const brew_t *b, esp_ms_t now)
{
    brew_output_t out;
    out.pump_on = false;
    out.pump_duty = 0.0f;
    out.stage = b->stage;
    out.mode = BREW_PUMP_OFF;
    out.elapsed_ms = b->running ? espresso_elapsed_ms(b->shot_start, now) : 0u;
    out.done = !b->running;
    return out;
}

/* Has the current stage's end condition been met? */
static bool stage_finished(const brew_t *b, esp_ms_t now, float volume_ml)
{
    const brew_stage_t *s = &b->profile.stages[b->stage];
    switch (s->end) {
    case BREW_END_TIME:
        return espresso_elapsed_ms(b->stage_start, now) >= s->duration_ms;
    case BREW_END_VOLUME:
        return volume_ml >= s->volume_ml;
    case BREW_END_MANUAL:
    default:
        return false; /* only a stop or global cap ends a manual stage */
    }
}

brew_output_t brew_update(brew_t *b, esp_ms_t now, float volume_ml)
{
    const brew_profile_t *prof = &b->profile;

    if (!b->running) {
        return stopped_output(b, now);
    }

    const esp_ms_t shot_elapsed = espresso_elapsed_ms(b->shot_start, now);

    /* Global stop conditions apply across all stages. */
    if (prof->max_shot_ms > 0 && shot_elapsed >= prof->max_shot_ms) {
        b->running = false;
        return stopped_output(b, now);
    }
    if (prof->target_volume_ml > 0.0f && volume_ml >= prof->target_volume_ml) {
        b->running = false;
        return stopped_output(b, now);
    }

    /* Advance past any finished stages. */
    while (b->stage < prof->stage_count && stage_finished(b, now, volume_ml)) {
        b->stage++;
        b->stage_start = now;
    }
    if (b->stage >= prof->stage_count) {
        b->running = false;
        return stopped_output(b, now);
    }

    const brew_stage_t *s = &prof->stages[b->stage];
    brew_output_t out;
    out.stage = b->stage;
    out.mode = s->mode;
    out.elapsed_ms = shot_elapsed;
    out.done = false;

    switch (s->mode) {
    case BREW_PUMP_OFF:
        out.pump_on = false;
        out.pump_duty = 0.0f;
        break;
    case BREW_PUMP_PRESSURE:
        /* TODO: regulate to s->pressure_bar via a pump PID once a pressure
         * sensor is fitted. Until then, run at full pump. */
        out.pump_on = true;
        out.pump_duty = 1.0f;
        break;
    case BREW_PUMP_DUTY:
    default:
        out.pump_duty = s->pump_duty;
        out.pump_on = s->pump_duty > 0.0f;
        break;
    }

    return out;
}
