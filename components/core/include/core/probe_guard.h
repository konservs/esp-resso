/**
 * @file probe_guard.h
 * @brief Transient-fault debounce for a temperature probe reading.
 *
 * A MAX31865 flags a single conversion as faulted when a lead picks up a
 * transient (handling static, pump/SSR switching, ESD), even though the probe
 * itself is fine and the very next sample reads normally. Reporting that one
 * sample upward latches SENSOR_FAULT in the safety supervisor, which then needs
 * a power cycle to clear. This guard rides through a short run of bad samples
 * by holding the last good temperature, and only reports the probe as failed
 * once the run reaches a trip count. A real open/short still trips, just a few
 * control cycles later; heater control on a reading that old is harmless.
 *
 * A probe that has never produced a good sample is reported bad immediately:
 * there is no last-good value to hold, and a missing probe at power-up must
 * still trip at once.
 */
#ifndef ESPRESSO_CORE_PROBE_GUARD_H
#define ESPRESSO_CORE_PROBE_GUARD_H

#include "core/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Consecutive bad samples before a probe is reported failed. At the 100 ms
 *  control period this is half a second. */
#define PROBE_GUARD_DEFAULT_TRIP 5

typedef struct {
    uint8_t  trip;        /**< Bad-sample run length that reports a failure. */
    uint8_t  bad_run;     /**< Consecutive bad samples seen so far.          */
    bool     have_good;   /**< A good sample has been seen since init.       */
    temp_c_t last_good_c; /**< Held while a short bad run is ridden through. */
} probe_guard_t;

/** What the consumer should act on this cycle. */
typedef struct {
    bool     ok;      /**< Probe usable: a fresh good sample or a held one.   */
    temp_c_t celsius; /**< Fresh or held temperature; meaningless if !ok.    */
    uint8_t  fault;   /**< Raw fault code of this sample (0 when the sample
                       *   was good), reported even while being ridden
                       *   through so it can be logged.                      */
    bool     held;    /**< This sample was bad and last_good_c substituted.  */
} probe_guard_out_t;

/** @param trip Run length that trips; 0 selects ::PROBE_GUARD_DEFAULT_TRIP. */
void probe_guard_init(probe_guard_t *g, uint8_t trip);

/**
 * @brief Feed one raw sample and get the debounced view.
 * @param ok       Raw sample validity from the front-end.
 * @param celsius  Raw temperature (ignored when !ok).
 * @param fault    Raw fault code (0 when ok).
 */
probe_guard_out_t probe_guard_update(probe_guard_t *g, bool ok, temp_c_t celsius,
                                     uint8_t fault);

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_CORE_PROBE_GUARD_H */
