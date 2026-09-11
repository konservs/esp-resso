/**
 * @file hal_level.h
 * @brief Water-level probes (boilers and reservoir).
 */
#ifndef ESPRESSO_HAL_LEVEL_H
#define ESPRESSO_HAL_LEVEL_H

#include "hal/hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialise the level-probe inputs. */
espresso_result_t hal_level_init(void);

/** Result of a probe read. */
typedef enum {
    HAL_LEVEL_UNKNOWN = 0, /**< No trusted reading yet: the boot/seed value and
                            *   what callers fall back to when a published state
                            *   goes stale. hal_level_read() never returns it.  */
    HAL_LEVEL_DRY,     /**< Probe uncovered (no water at the rod).            */
    HAL_LEVEL_WET,     /**< Probe covered (water present).                    */
    HAL_LEVEL_FAULT    /**< Implausible reading — the POS and NEG sense lines
                        *   both conduct at once, which real water cannot do
                        *   (rod/output short, mux fault, or common-mode). The
                        *   caller must not treat this as "dry" and fill.      */
} hal_level_state_t;

/** Read a probe as dry / wet / faulted. The reservoir float switch only ever
 *  returns ::HAL_LEVEL_DRY or ::HAL_LEVEL_WET. */
hal_level_state_t hal_level_read(hal_level_id_t level);

/** Convenience wrapper: true only when the probe reads ::HAL_LEVEL_WET (a fault
 *  is *not* water). */
bool hal_level_present(hal_level_id_t level);

/* ---- TEMPORARY bench-debug: slow level-drive sweep (voltmeter, no scope) ---
 * 1 = the level task runs this sweep instead of sensing: it parades the four rod
 * drive states (BREW +/-, STEAM +/-, then idle), holding each ~2 s so a
 * multimeter can settle on every node, and logs each step + the sense lines.
 * While enabled the level task publishes no real readings, so the probe atomics
 * keep their seeded "full" value and the control loop leaves the fill valves
 * shut. Set back to 0 to restore normal fast sensing; remove before shipping. */
#define LEVEL_DEBUG_SWEEP 0

#if LEVEL_DEBUG_SWEEP
/** Bench aid: infinite ~2 s-per-step level-drive sweep. Never returns; called by
 *  the level task in place of normal sensing. */
void hal_level_debug_sweep(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_HAL_LEVEL_H */
