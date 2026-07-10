/**
 * @file settings.h
 * @brief Persisted machine configuration with sane defaults and validation.
 *
 * The control task loads these at boot (from NVS via the HAL storage layer,
 * falling back to defaults) and they are edited through the UI. Keeping the
 * whole configuration in one validated struct makes it easy to persist and to
 * unit-test the validation rules.
 */
#ifndef ESPRESSO_CORE_SETTINGS_H
#define ESPRESSO_CORE_SETTINGS_H

#include "core/brew.h"
#include "core/load_guard.h"
#include "core/pid.h"
#include "core/pump_guard.h"
#include "core/safety.h"
#include "core/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    temp_c_t            brew_setpoint;  /**< Brew boiler target (deg C).  */
    temp_c_t            steam_setpoint; /**< Steam boiler target (deg C). */
    pid_config_t        brew_pid;
    pid_config_t        steam_pid;
    brew_profile_type_t active_profile; /**< Selected brew profile.       */
    brew_params_t       brew;           /**< Params the profiles build from. */
    pump_guard_config_t pump;           /**< Vibratory-pump duty-cycle rating. */
    uint8_t             max_active_heaters; /**< Mains load cap: heater elements
                                             *   on at once (see load_guard.h).  */
    safety_config_t     safety;
} settings_t;

/** Populate @p s with the factory defaults for a dual-boiler E61 machine. */
void settings_load_defaults(settings_t *s);

/**
 * @brief Validate a settings struct.
 * @return ::ESPRESSO_OK if consistent, otherwise ::ESPRESSO_ERR_RANGE.
 *
 * Checks include: setpoints within sane bounds, the safety cutoff above the
 * highest setpoint, non-negative gains, and a positive control margin.
 */
espresso_result_t settings_validate(const settings_t *s);

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_CORE_SETTINGS_H */
