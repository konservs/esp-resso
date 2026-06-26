/**
 * @file boiler.h
 * @brief Per-boiler temperature controller.
 *
 * A dual-boiler E61 machine has two independent instances of this controller:
 * a brew boiler (~93 C) and a steam boiler (~125 C). Each wraps a PID whose
 * output is a normalised heater duty in [0, 1] driven onto a solid-state relay.
 */
#ifndef ESPRESSO_CORE_BOILER_H
#define ESPRESSO_CORE_BOILER_H

#include "core/pid.h"
#include "core/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    temp_c_t setpoint;   /**< Target temperature (deg C).                 */
    pid_ctrl_t pid;      /**< Temperature PID (output range = duty 0..1). */
    float    ready_band; /**< +/- band counted as "at temperature".      */
} boiler_t;

/** Result of one control step. */
typedef struct {
    float heater_duty;  /**< Commanded SSR duty, 0..1.        */
    bool  at_setpoint;  /**< |error| <= ready_band.           */
} boiler_output_t;

/**
 * @brief Initialise a boiler controller.
 * @param b          Instance.
 * @param setpoint   Initial target temperature.
 * @param pid_cfg    PID tuning; out_min/out_max should bound the duty (0..1).
 * @param ready_band Half-width of the "at temperature" band, in deg C.
 */
void boiler_init(boiler_t *b, temp_c_t setpoint, const pid_config_t *pid_cfg,
                 float ready_band);

/** Change the target temperature and reset the PID integrator. */
void boiler_set_setpoint(boiler_t *b, temp_c_t setpoint);

/**
 * @brief Run one control step.
 * @param b        Instance.
 * @param measured Current (filtered) boiler temperature.
 * @param dt_s     Time since the previous step, in seconds.
 */
boiler_output_t boiler_update(boiler_t *b, temp_c_t measured, float dt_s);

/** True if @p measured is within the ready band of the setpoint. */
bool boiler_at_setpoint(const boiler_t *b, temp_c_t measured);

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_CORE_BOILER_H */
