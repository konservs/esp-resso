/**
 * @file filter.h
 * @brief Small signal-conditioning helpers: EMA low-pass, digital debounce,
 *        and dual-threshold hysteresis (used for boiler fill control).
 *
 * Pure value objects, no I/O.
 */
#ifndef ESPRESSO_CORE_FILTER_H
#define ESPRESSO_CORE_FILTER_H

#include "core/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Exponential moving-average low-pass filter.                                */
/* -------------------------------------------------------------------------- */

typedef struct {
    float alpha; /**< Smoothing factor in (0, 1]. Higher = less smoothing. */
    float value; /**< Current filtered value.                              */
    bool  init;  /**< True once the first sample has seeded @c value.      */
} ema_t;

/** Initialise an EMA filter with smoothing factor @p alpha in (0, 1]. */
void  ema_init(ema_t *f, float alpha);
/** Clear the filter so the next sample re-seeds it. */
void  ema_reset(ema_t *f);
/** Feed a raw sample and return the new filtered value. */
float ema_update(ema_t *f, float x);

/* -------------------------------------------------------------------------- */
/* Integrator-style debouncer for noisy digital inputs (buttons, probes).     */
/* -------------------------------------------------------------------------- */

typedef struct {
    bool    state;     /**< Current debounced (stable) state.        */
    uint8_t count;     /**< Consecutive agreeing samples.            */
    uint8_t threshold; /**< Samples required to accept a new state.  */
} debounce_t;

/** Initialise a debouncer requiring @p threshold consistent samples. */
void debounce_init(debounce_t *d, uint8_t threshold, bool initial);
/** Feed a raw reading; returns the current stable state. */
bool debounce_update(debounce_t *d, bool raw);

/* -------------------------------------------------------------------------- */
/* Dual-threshold hysteresis ("turn on below low, off above high").           */
/* Used for boiler auto-fill: start filling when level drops below @c low,    */
/* keep filling until it reaches @c high.                                     */
/* -------------------------------------------------------------------------- */

typedef struct {
    float low;
    float high;
    bool  active;
} hysteresis_t;

/** Initialise a hysteresis band. Requires low <= high. */
void hysteresis_init(hysteresis_t *h, float low, float high, bool initial);
/** Update with measurement @p x; returns whether the output is active. */
bool hysteresis_update(hysteresis_t *h, float x);

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_CORE_FILTER_H */
