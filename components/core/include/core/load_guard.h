/**
 * @file load_guard.h
 * @brief Mains load guard: limit how many heater elements run at once.
 *
 * A dual-boiler machine has four 400 W heating elements (a lower + upper element
 * per boiler). Running all four draws 1600 W, which — with the pump and
 * electronics on top — exceeds what a 120 V / 15 A receptacle can carry
 * continuously (~1440 W). This guard caps the number of elements energised at
 * any instant to @c max_active (3 by default: 3 x 400 W = 1200 W, leaving
 * headroom for the pump and logic).
 *
 * Allocation is by priority, matching how the machine is used:
 *   - The **brew boiler wins**. While it is still warming up it takes both of
 *     its elements (fastest heat-up); once it reaches its ready band it holds
 *     temperature on the **lower element only** (a PID trim needs little power),
 *     freeing a slot.
 *   - The **steam boiler** takes whatever elements remain, up to its two.
 *
 * So a cold start runs brew-LO + brew-HI + steam-LO (steam-HI held off), and
 * once brew is up to temperature it flips to brew-LO + steam-LO + steam-HI —
 * always three elements, never four. Set @c max_active to 4 (the element count)
 * to lift the cap entirely, e.g. on a 230 V circuit that can carry all four.
 *
 * The guard is a pure function of its inputs (no state, no I/O), so it is
 * trivially host-testable.
 */
#ifndef ESPRESSO_CORE_LOAD_GUARD_H
#define ESPRESSO_CORE_LOAD_GUARD_H

#include "core/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The four heater elements, in priority-independent index order. */
typedef enum {
    LOAD_BREW_LO = 0,
    LOAD_BREW_HI,
    LOAD_STEAM_LO,
    LOAD_STEAM_HI,
    LOAD_HEATER_COUNT
} load_heater_t;

/** Default simultaneous-element cap for a 120 V / 15 A supply. */
#define LOAD_DEFAULT_MAX_ACTIVE 3

/** Per-boiler heat demand for one control step. */
typedef struct {
    float brew_duty;        /**< Brew boiler PID duty, 0..1 (0 = no heat).     */
    float steam_duty;       /**< Steam boiler PID duty, 0..1 (0 = no heat).    */
    bool  brew_at_setpoint; /**< Brew within its ready band -> hold on LO only. */
} load_guard_input_t;

/** Resolved duty for each element after the cap; index with ::load_heater_t. */
typedef struct {
    float duty[LOAD_HEATER_COUNT];
} load_guard_output_t;

/**
 * @brief Allocate heater elements under the simultaneous-active cap.
 * @param in         Per-boiler demand this step.
 * @param max_active Max elements allowed on at once (clamped to
 *                   0..::LOAD_HEATER_COUNT). ::LOAD_HEATER_COUNT lifts the cap.
 * @return Duty for each element; a disabled element is 0 (never energised).
 */
load_guard_output_t load_guard_apply(const load_guard_input_t *in,
                                     uint8_t max_active);

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_CORE_LOAD_GUARD_H */
