/**
 * @file hal_pump.h
 * @brief Vibratory pump control (on/off, or dimmed via phase/PSM control).
 */
#ifndef ESPRESSO_HAL_PUMP_H
#define ESPRESSO_HAL_PUMP_H

#include "hal/hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialise the pump output (off). */
espresso_result_t hal_pump_init(void);

/**
 * @brief Drive the pump at a duty in [0, 1].
 *
 * For a simple build this is on/off (duty > 0 => on). With phase-angle or
 * pulse-skip-modulation hardware the duty maps to reduced flow for
 * pre-infusion or pressure profiling.
 */
void hal_pump_set(float duty);

/** Stop the pump. */
void hal_pump_off(void);

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_HAL_PUMP_H */
