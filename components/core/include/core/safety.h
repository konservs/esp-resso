/**
 * @file safety.h
 * @brief Independent safety supervisor.
 *
 * This runs separately from the PID control loop and is the firmware's second
 * line of defence (the first line is hardware: a thermal fuse and an
 * over-pressure valve — see docs/safety.md). It latches a trip on:
 *   - over-temperature on either boiler,
 *   - a temperature-sensor fault (open/short reported by the front-end),
 *   - a heater that stays on too long without making progress (dry boiler,
 *     failed element, or a welded SSR contact),
 *   - a boiler that keeps calling for water without ever reading full (a leak,
 *     a dry or stuck reservoir float, a failed pump or valve, or a dead probe).
 *
 * Once tripped it stays tripped until ::safety_clear() is called by an
 * explicit operator action.
 */
#ifndef ESPRESSO_CORE_SAFETY_H
#define ESPRESSO_CORE_SAFETY_H

#include "core/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SAFETY_OK = 0,
    SAFETY_TRIP_OVERTEMP,      /**< Boiler exceeded the absolute maximum.   */
    SAFETY_TRIP_SENSOR_FAULT,  /**< Temperature sensor open/short.          */
    SAFETY_TRIP_HEAT_TIMEOUT,  /**< Heating too long without progress.      */
    SAFETY_TRIP_FILL_TIMEOUT   /**< Filling too long without reaching full. */
} safety_trip_t;

typedef struct {
    temp_c_t max_temp;        /**< Absolute cutoff for either boiler (deg C). */
    uint32_t heat_timeout_ms; /**< Max continuous heating without progress.   */
    uint32_t fill_timeout_ms; /**< Max continuous filling before a trip;
                               *   0 disables the check (e.g. a plumbed
                               *   machine, or bench work).                  */
} safety_config_t;

typedef struct {
    safety_config_t cfg;
    safety_trip_t   trip;
    esp_ms_t        heat_since;  /**< When continuous heating began.          */
    bool            timing_heat; /**< Whether the heat timer is running.      */
    esp_ms_t        fill_since;  /**< When continuous filling began.          */
    bool            timing_fill; /**< Whether the fill timer is running.      */
} safety_t;

/** Per-step inputs sampled by the control/safety task. */
typedef struct {
    bool     sensors_valid;    /**< A real reading has been taken. While false
                                *   (boot, before the control loop's first
                                *   sample) every other field below is a
                                *   placeholder rather than a measurement, so
                                *   the supervisor holds off instead of
                                *   latching a trip on it. */
    temp_c_t brew_temp;
    temp_c_t steam_temp;
    bool     brew_sensor_ok;
    bool     steam_sensor_ok;
    bool     any_heater_on;    /**< True if either SSR is being driven.        */
    bool     making_progress;  /**< Temperature rising or already at setpoint. */
    bool     any_fill_on;      /**< True while either fill valve is commanded
                                *   open. There is no partial-progress signal
                                *   to pair this with — a level probe is
                                *   binary, and the fill command drops the
                                *   moment it reads covered — so continuous
                                *   fill time IS the failure measure.         */
} safety_inputs_t;

/** Initialise the supervisor in the OK state. */
void safety_init(safety_t *s, const safety_config_t *cfg);

/**
 * @brief Evaluate all safety rules for this step.
 * @return The current (latched) trip code; ::SAFETY_OK if healthy.
 */
safety_trip_t safety_update(safety_t *s, const safety_inputs_t *in, esp_ms_t now);

/** True if a trip is currently latched. */
bool safety_tripped(const safety_t *s);

/** Current latched trip code. */
safety_trip_t safety_trip(const safety_t *s);

/** Clear a latched trip (operator reset). */
void safety_clear(safety_t *s);

/** Human-readable trip name. */
const char *safety_trip_name(safety_trip_t t);

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_CORE_SAFETY_H */
