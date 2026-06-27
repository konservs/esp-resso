/**
 * @file hal_flow.h
 * @brief Flow meter for volumetric dosing (pulse counter on the ESP32).
 */
#ifndef ESPRESSO_HAL_FLOW_H
#define ESPRESSO_HAL_FLOW_H

#include "hal/hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialise the flow-meter pulse counter. */
espresso_result_t hal_flow_init(void);

/** Raw accumulated pulse count since boot (monotonic, wraps with the counter). */
uint32_t hal_flow_pulses(void);

/** Accumulated volume in millilitres (pulses scaled by the meter's K-factor). */
float hal_flow_ml(void);

/** Zero the accumulated volume at the start of a shot. */
void hal_flow_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_HAL_FLOW_H */
