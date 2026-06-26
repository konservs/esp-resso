#include "core/settings.h"

/* Reasonable starting points for a dual-boiler E61 machine. Gains are
 * conservative and meant to be refined per machine (see docs/control.md). */
#define DEFAULT_BREW_SETPOINT   93.0f
#define DEFAULT_STEAM_SETPOINT  125.0f
#define DEFAULT_MAX_TEMP        165.0f

void settings_load_defaults(settings_t *s)
{
    s->brew_setpoint = DEFAULT_BREW_SETPOINT;
    s->steam_setpoint = DEFAULT_STEAM_SETPOINT;

    /* PID output is a normalised heater duty in [0, 1]. */
    const pid_config_t brew_pid = {
        .kp = 0.06f, .ki = 0.004f, .kd = 0.8f,
        .out_min = 0.0f, .out_max = 1.0f,
        .integ_min = 0.0f, .integ_max = 1.0f
    };
    const pid_config_t steam_pid = {
        .kp = 0.05f, .ki = 0.003f, .kd = 0.5f,
        .out_min = 0.0f, .out_max = 1.0f,
        .integ_min = 0.0f, .integ_max = 1.0f
    };
    s->brew_pid = brew_pid;
    s->steam_pid = steam_pid;

    s->active_profile = BREW_PROFILE_AUTO;
    const brew_params_t brew = {
        .preinfuse_ms = 3000,
        .hold_ms = 2000,
        .preinfuse_pump = 0.35f,
        .extract_pump = 1.0f,
        .target_volume_ml = 36.0f, /* ~double shot; 0 disables volumetric stop */
        .max_shot_ms = 60000
    };
    s->brew = brew;

    const safety_config_t safety = {
        .max_temp = DEFAULT_MAX_TEMP,
        .heat_timeout_ms = 120000 /* 2 min of fruitless heating -> trip */
    };
    s->safety = safety;
}

static bool gains_ok(const pid_config_t *p)
{
    return p->kp >= 0.0f && p->ki >= 0.0f && p->kd >= 0.0f &&
           p->out_max > p->out_min && p->integ_max >= p->integ_min;
}

espresso_result_t settings_validate(const settings_t *s)
{
    if (s->brew_setpoint < 80.0f || s->brew_setpoint > 110.0f) {
        return ESPRESSO_ERR_RANGE;
    }
    if (s->steam_setpoint < 110.0f || s->steam_setpoint > 145.0f) {
        return ESPRESSO_ERR_RANGE;
    }
    /* The absolute cutoff must sit safely above the highest setpoint so normal
     * operation never trips it. */
    const temp_c_t highest = s->steam_setpoint > s->brew_setpoint
                                 ? s->steam_setpoint
                                 : s->brew_setpoint;
    if (s->safety.max_temp <= highest + 5.0f) {
        return ESPRESSO_ERR_RANGE;
    }
    if (!gains_ok(&s->brew_pid) || !gains_ok(&s->steam_pid)) {
        return ESPRESSO_ERR_RANGE;
    }
    if (s->brew.extract_pump <= 0.0f || s->brew.extract_pump > 1.0f) {
        return ESPRESSO_ERR_RANGE;
    }
    if (s->brew.max_shot_ms == 0) {
        return ESPRESSO_ERR_RANGE;
    }
    if ((int)s->active_profile < 0 || s->active_profile >= BREW_PROFILE_COUNT) {
        return ESPRESSO_ERR_RANGE;
    }
    return ESPRESSO_OK;
}
