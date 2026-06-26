#include "core/boiler.h"

#include <math.h>

void boiler_init(boiler_t *b, temp_c_t setpoint, const pid_config_t *pid_cfg,
                 float ready_band)
{
    b->setpoint = setpoint;
    b->ready_band = ready_band;
    pid_init(&b->pid, pid_cfg);
}

void boiler_set_setpoint(boiler_t *b, temp_c_t setpoint)
{
    b->setpoint = setpoint;
    /* Reset integral action so a large step change does not carry stale wind-up. */
    pid_reset(&b->pid);
}

bool boiler_at_setpoint(const boiler_t *b, temp_c_t measured)
{
    return fabsf(measured - b->setpoint) <= b->ready_band;
}

boiler_output_t boiler_update(boiler_t *b, temp_c_t measured, float dt_s)
{
    boiler_output_t out;
    out.heater_duty = pid_update(&b->pid, b->setpoint, measured, dt_s);
    out.at_setpoint = boiler_at_setpoint(b, measured);
    return out;
}
