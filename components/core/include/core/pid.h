/**
 * @file pid.h
 * @brief Discrete PID controller used to regulate each boiler.
 *
 * Features:
 *  - Output clamping to an arbitrary range (e.g. 0..1 heater duty).
 *  - Integrator clamping for anti-windup.
 *  - Derivative-on-measurement to avoid "derivative kick" on setpoint changes.
 *
 * The controller is a pure value object: it performs no I/O and holds no
 * references to hardware, so it is trivially unit-testable on a host.
 */
#ifndef ESPRESSO_CORE_PID_H
#define ESPRESSO_CORE_PID_H

#include "core/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Static tuning parameters for a PID controller. */
typedef struct {
    float kp;        /**< Proportional gain.                         */
    float ki;        /**< Integral gain (per second).                */
    float kd;        /**< Derivative gain (seconds).                 */
    float out_min;   /**< Lower output clamp.                        */
    float out_max;   /**< Upper output clamp.                        */
    float integ_min; /**< Lower integrator clamp (anti-windup).      */
    float integ_max; /**< Upper integrator clamp (anti-windup).      */
} pid_config_t;

/** Runtime state of a PID controller.
 *  Named pid_ctrl_t (not pid_t) to avoid clashing with POSIX pid_t. */
typedef struct {
    pid_config_t cfg;
    float integrator;
    float prev_measurement;
    bool  has_prev;
} pid_ctrl_t;

/** Initialise @p pid with @p cfg and clear its runtime state. */
void pid_init(pid_ctrl_t *pid, const pid_config_t *cfg);

/** Clear the integrator and derivative history without changing gains. */
void pid_reset(pid_ctrl_t *pid);

/** Update gains in place (e.g. from a settings menu) without resetting state. */
void pid_set_gains(pid_ctrl_t *pid, float kp, float ki, float kd);

/**
 * @brief Advance the controller by one sample.
 * @param pid          Controller instance.
 * @param setpoint     Desired process value.
 * @param measurement  Current (filtered) process value.
 * @param dt_s         Time since the previous update, in seconds (> 0).
 * @return Clamped controller output in [out_min, out_max].
 */
float pid_update(pid_ctrl_t *pid, float setpoint, float measurement, float dt_s);

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_CORE_PID_H */
