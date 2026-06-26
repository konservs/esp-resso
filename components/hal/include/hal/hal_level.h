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

/** True when water is present at the given probe (above its threshold). */
bool hal_level_present(hal_level_id_t level);

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_HAL_LEVEL_H */
