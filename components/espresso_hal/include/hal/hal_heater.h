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

/** The individually-switched heating elements (lower + upper per boiler).
 *  Addressed independently so the load guard can run a boiler on one element
 *  or both (see core/load_guard.h). */
typedef enum {
    HAL_HEATER_BREW_LO = 0,
    HAL_HEATER_BREW_HI,
    HAL_HEATER_STEAM_LO,
    HAL_HEATER_STEAM_HI,
    HAL_HEATER_COUNT
} hal_heater_id_t;

/** Initialise the heater outputs (all off). */
espresso_result_t hal_heater_init(void);

/**
 * @brief Set one heating element's duty cycle.
 * @param element Which element.
 * @param duty    0.0 (off) .. 1.0 (full). Realised as a slow PWM window over the
 *                element's SSR so each on/off aligns with mains zero-crossings.
 */
void hal_heater_set_duty(hal_heater_id_t element, float duty);

/** Immediately cut all heaters. Used by the safety supervisor. */
void hal_heater_all_off(void);

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_HAL_HEATER_H */
