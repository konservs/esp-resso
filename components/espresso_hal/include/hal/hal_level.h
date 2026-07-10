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
    HAL_LEVEL_DRY = 0, /**< Probe uncovered (no water at the rod).            */
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

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_HAL_LEVEL_H */
