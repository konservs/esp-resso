/**
 * @file hal_valve.h
 * @brief Solenoid valve control (group 3-way valve, boiler fill inlets).
 */
#ifndef ESPRESSO_HAL_VALVE_H
#define ESPRESSO_HAL_VALVE_H

#include "hal/hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialise all valves (closed/de-energised). */
espresso_result_t hal_valve_init(void);

/** Open (energise) or close a valve. */
void hal_valve_set(hal_valve_id_t valve, bool open);

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_HAL_VALVE_H */
