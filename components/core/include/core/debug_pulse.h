/**
 * @file debug_pulse.h
 * @brief Timed manual override of a single actuator, for bench commissioning.
 *
 * During bring-up you need to confirm that a relay actually clicks and that the
 * right load is on the other side of it. This lets an operator drive one output
 * directly for a couple of seconds, bypassing the control loop.
 *
 * Two properties make that safe enough to expose:
 *
 *   - **The pulse expires here, not at the caller.** ::debug_pulse_active is
 *     what the control loop asks every cycle, and it drops the override once
 *     the window has passed. A client that posts a request and then vanishes,
 *     or a Wi-Fi task that dies mid-pulse, cannot leave an output energised.
 *   - **Only one output at a time.** Starting a pulse replaces any pulse
 *     already running, so no sequence of requests can energise two loads at
 *     once.
 *
 * Deliberately limited to the pump and the fill solenoids. The heating elements
 * are not exposed: a dry element energised by a well-meaning button press is
 * hardware damage, and the dry-fire interlock that prevents it lives in the
 * control loop this module is designed to bypass.
 */
#ifndef ESPRESSO_CORE_DEBUG_PULSE_H
#define ESPRESSO_CORE_DEBUG_PULSE_H

#include "core/state_machine.h"
#include "core/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** How long a debug pulse drives its output. */
#define DEBUG_PULSE_MS 2000u

/** Upper bound accepted by ::debug_pulse_start, whatever the caller asks for. */
#define DEBUG_PULSE_MAX_MS 5000u

/** The outputs a debug pulse may drive. */
typedef enum {
    DEBUG_OUT_NONE = 0,    /**< No override running.            */
    DEBUG_OUT_PUMP,        /**< Vibratory pump.                 */
    DEBUG_OUT_FILL_BREW,   /**< Brew boiler auto-fill solenoid.  */
    DEBUG_OUT_FILL_STEAM,  /**< Steam boiler auto-fill solenoid. */
    DEBUG_OUT_COUNT
} debug_out_t;

typedef struct {
    debug_out_t target;      /**< ::DEBUG_OUT_NONE when idle.        */
    esp_ms_t    start;       /**< When the running pulse began.      */
    uint32_t    duration_ms; /**< How long it runs for.              */
} debug_pulse_t;

/** Initialise to idle. */
void debug_pulse_init(debug_pulse_t *p);

/**
 * @brief Is the machine in a state where a manual override is acceptable?
 *
 * Refused only while a shot or a backflush is running, where the override would
 * fight the brew sequencer for the pump.
 *
 * A latched safety trip deliberately does *not* block a pulse. Blocking on it
 * only made the feature unavailable during exactly the bench session it exists
 * for, since a machine with unfinished probe wiring trips on every boot.
 *
 * Most trips are about temperature — an over-temperature boiler, a faulted
 * probe, a heater making no progress — and none of the outputs here are
 * heaters. ::SAFETY_TRIP_FILL_TIMEOUT is the exception: it is raised *about*
 * these very outputs, and it means water is not reaching a boiler. A pulse is
 * still allowed on it, deliberately — a bounded, operator-initiated run of the
 * pump or a fill valve is how you find out which of them is at fault — but note
 * that if the cause is a leak, a pulse adds a little more water. The pulse
 * expiring here rather than at the caller is what keeps that bounded.
 *
 * That reasoning holds only as long as this module drives no heating element.
 * **If a heater output is ever added to ::debug_out_t, the safety-trip gate has
 * to come back with it.**
 */
bool debug_pulse_permitted(machine_state_t state);

/**
 * @brief Begin driving @p out, replacing any pulse already running.
 * @param duration_ms Clamped to ::DEBUG_PULSE_MAX_MS.
 * @return false (and no change) if @p out is not a drivable output.
 */
bool debug_pulse_start(debug_pulse_t *p, debug_out_t out, esp_ms_t now,
                       uint32_t duration_ms);

/** Cancel any running pulse. */
void debug_pulse_stop(debug_pulse_t *p);

/**
 * @brief The output to override right now, expiring the pulse if its window has
 *        passed.
 *
 * The control loop calls this once per cycle and drives the returned output.
 * Not const: expiry happens here, so that forgetting to call it can only ever
 * fail safe (nothing is driven) rather than latch an output on.
 */
debug_out_t debug_pulse_active(debug_pulse_t *p, esp_ms_t now);

/** Milliseconds left on the running pulse; 0 when idle or expired. */
uint32_t debug_pulse_remaining_ms(const debug_pulse_t *p, esp_ms_t now);

/** Stable short name for an output ("pump", "fill_brew", ...); "none" if idle. */
const char *debug_pulse_out_name(debug_out_t out);

/** Inverse of ::debug_pulse_out_name; ::DEBUG_OUT_NONE if unrecognised. */
debug_out_t debug_pulse_out_from_name(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_CORE_DEBUG_PULSE_H */
