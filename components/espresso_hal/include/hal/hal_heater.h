/**
 * @file hal_heater.h
 * @brief Boiler heater control via solid-state relays (slow PWM / zero-cross).
 */
#ifndef ESPRESSO_HAL_HEATER_H
#define ESPRESSO_HAL_HEATER_H

#include "hal/hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialise the heater outputs (all off). */
espresso_result_t hal_heater_init(void);

/**
 * @brief Set a boiler heater duty cycle.
 * @param boiler Which boiler.
 * @param duty   0.0 (off) .. 1.0 (full). Realised as a slow PWM window over the
 *               SSR(s) so each on/off aligns with mains zero-crossings. A boiler
 *               with two elements (lower + upper) drives both from this one
 *               duty; the split is a driver-side detail, invisible to callers.
 */
void hal_heater_set_duty(hal_boiler_id_t boiler, float duty);

/** Immediately cut all heaters. Used by the safety supervisor. */
void hal_heater_all_off(void);

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_HAL_HEATER_H */
