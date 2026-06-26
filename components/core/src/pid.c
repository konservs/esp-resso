#include "core/pid.h"

void pid_init(pid_ctrl_t *pid, const pid_config_t *cfg)
{
    pid->cfg = *cfg;
    pid_reset(pid);
}

void pid_reset(pid_ctrl_t *pid)
{
    pid->integrator = 0.0f;
    pid->prev_measurement = 0.0f;
    pid->has_prev = false;
}

void pid_set_gains(pid_ctrl_t *pid, float kp, float ki, float kd)
{
    pid->cfg.kp = kp;
    pid->cfg.ki = ki;
    pid->cfg.kd = kd;
}

float pid_update(pid_ctrl_t *pid, float setpoint, float measurement, float dt_s)
{
    const pid_config_t *c = &pid->cfg;
    const float error = setpoint - measurement;

    /* Proportional term. */
    const float p = c->kp * error;

    /* Integral term with clamping anti-windup. */
    pid->integrator += c->ki * error * dt_s;
    pid->integrator = espresso_clampf(pid->integrator, c->integ_min, c->integ_max);

    /* Derivative on measurement (not on error) avoids a spike when the
     * setpoint changes. The sign is negated because d(error)/dt = -d(meas)/dt
     * for a constant setpoint. */
    float d = 0.0f;
    if (pid->has_prev && dt_s > 0.0f) {
        const float dmeas = (measurement - pid->prev_measurement) / dt_s;
        d = -c->kd * dmeas;
    }
    pid->prev_measurement = measurement;
    pid->has_prev = true;

    const float out = p + pid->integrator + d;
    return espresso_clampf(out, c->out_min, c->out_max);
}
