/**
 * @file state_machine.h
 * @brief Top-level machine state machine.
 *
 * Coordinates the high-level operating modes of the machine. Guards prevent
 * illegal transitions (e.g. you cannot start a shot until both boilers are
 * ready). A safety trip (::EV_FAULT) forces ::MACHINE_FAULT from any state and
 * latches there until ::EV_FAULT_CLEAR.
 */
#ifndef ESPRESSO_CORE_STATE_MACHINE_H
#define ESPRESSO_CORE_STATE_MACHINE_H

#include "core/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MACHINE_BOOT = 0, /**< Power-on self test (zero value).        */
    MACHINE_HEATING,  /**< Warming up to setpoints.                */
    MACHINE_READY,    /**< Both boilers at temperature.            */
    MACHINE_BREWING,  /**< Shot in progress.                       */
    MACHINE_STEAMING, /**< Steam valve open.                       */
    MACHINE_BACKFLUSH,/**< Cleaning cycle.                         */
    MACHINE_SLEEP,    /**< Eco / standby (heaters reduced/off).    */
    MACHINE_FAULT     /**< Safety trip; latched.                   */
} machine_state_t;

typedef enum {
    EV_NONE = 0,
    EV_WAKE,               /**< Boot complete / wake from sleep.        */
    EV_BOTH_BOILERS_READY, /**< Both boilers reached temperature.       */
    EV_NOT_READY,          /**< Dropped out of the ready band.          */
    EV_BREW_LEVER_ON,      /**< E61 brew lever / paddle engaged.        */
    EV_BREW_LEVER_OFF,     /**< Brew lever released.                    */
    EV_STEAM_ON,
    EV_STEAM_OFF,
    EV_BACKFLUSH,
    EV_SLEEP,
    EV_FAULT,              /**< Safety supervisor tripped.              */
    EV_FAULT_CLEAR         /**< Operator acknowledged/cleared a fault.  */
} machine_event_t;

typedef struct {
    machine_state_t state;
} machine_t;

/** Initialise to ::MACHINE_BOOT. */
void machine_init(machine_t *m);

/** Apply an event; returns the (possibly unchanged) resulting state. */
machine_state_t machine_dispatch(machine_t *m, machine_event_t ev);

/** Current state. */
machine_state_t machine_state(const machine_t *m);

/** Human-readable name for logging/UI. */
const char *machine_state_name(machine_state_t s);

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_CORE_STATE_MACHINE_H */
