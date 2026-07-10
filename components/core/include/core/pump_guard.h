/**
 * @file pump_guard.h
 * @brief Vibratory-pump duty-cycle tracker (thermal protection).
 *
 * Vibratory pumps (e.g. the Ulka EFX5) are rated for an intermittent duty
 * cycle, not continuous running: the EFX5 is specified for **2 min ON / 1 min
 * OFF**. Run it much beyond that and the solenoid coil overheats and fails
 * early. A single espresso shot is well within the rating, but back-to-back
 * shots or a long extraction/backflush can add up, so the firmware tracks the
 * pump's accumulated run time and refuses to *start* a new brew while the pump
 * needs to rest (the machine shows a "Pump Cooling" warning instead).
 *
 * The model is a leaky-bucket thermal accumulator, expressed directly in the
 * two rated numbers:
 *
 *   - Running the pump fills @c heat_ms at 1 ms per ms, capped at @c on_max_ms.
 *   - Resting drains it at @c on_max_ms / @c off_min_ms per ms — chosen so that
 *     draining a full bucket (a full @c on_max_ms of running) takes exactly
 *     @c off_min_ms, matching the datasheet's ON/OFF ratio.
 *
 * The guard trips into a cooling lock-out when the bucket fills, and stays
 * locked until it has fully drained (a full rest). It also **starts locked** at
 * init: the firmware cannot know how hard the pump was run before this power-up,
 * so it assumes a full bucket and asks the user to wait for a cooldown before
 * even the first shot. In practice idle time (including the boilers' warm-up)
 * drains it, so a cold start rarely waits, while a warm restart straight after
 * heavy use correctly holds off. The accumulation is a deliberate
 * simplification: any pump-on time counts as full load regardless of the
 * commanded duty, which is the conservative (pump-protecting) choice.
 *
 * Like the rest of @c core, this does no I/O and takes @c now as a parameter,
 * so it is fully host-testable.
 */
#ifndef ESPRESSO_CORE_PUMP_GUARD_H
#define ESPRESSO_CORE_PUMP_GUARD_H

#include "core/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Pump duty-cycle rating. Set @c on_max_ms to 0 to disable the guard. */
typedef struct {
    uint32_t on_max_ms;  /**< Max accumulated pump-ON before a forced rest.   */
    uint32_t off_min_ms; /**< Rest needed to fully recover from a full ON.    */
} pump_guard_config_t;

typedef struct {
    pump_guard_config_t cfg;
    uint32_t heat_ms;  /**< Accumulated thermal load, 0..on_max_ms.           */
    bool     cooling;  /**< True while locked out for a cooldown (hysteresis).*/
    esp_ms_t last_ms;  /**< Timestamp of the previous ::pump_guard_update.    */
    bool     started;  /**< False until the first update (seeds last_ms).     */
} pump_guard_t;

/** Initialise the guard from a rating. Starts locked (cooling) when enabled,
 *  so the first shot waits for one full rest; see the file comment. */
void pump_guard_init(pump_guard_t *g, const pump_guard_config_t *cfg);

/**
 * @brief Advance the thermal model by the time since the last call.
 * @param pump_on Whether the pump has been commanded on over this interval.
 * @param now     Monotonic time in ms.
 *
 * Call every control cycle with the actual pump command, in every machine
 * state, so the bucket keeps draining while the machine is idle.
 */
void pump_guard_update(pump_guard_t *g, bool pump_on, esp_ms_t now);

/** True if a new brew may start now (i.e. the pump is not resting). */
bool pump_guard_can_brew(const pump_guard_t *g);

/** True while the pump is locked out for a cooldown. */
bool pump_guard_cooling(const pump_guard_t *g);

/**
 * @brief Estimated time until ::pump_guard_can_brew becomes true again.
 * @return Milliseconds of rest remaining, or 0 if the pump is already
 *         available. Intended for a UI countdown.
 */
uint32_t pump_guard_cooldown_ms(const pump_guard_t *g);

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_CORE_PUMP_GUARD_H */
