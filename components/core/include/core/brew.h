/**
 * @file brew.h
 * @brief Profile-driven espresso shot controller.
 *
 * On an E61 machine the group lever flips a microswitch that tells the
 * controller to start/stop a shot (mapped to EV_BREW_LEVER_ON/OFF). What
 * happens during the shot is described by a @ref brew_profile_t: an ordered
 * list of stages, each with a pump mode and an end condition. Two profiles are
 * built in:
 *
 *   - ::BREW_PROFILE_MANUAL — one stage at full pump that ends only when the
 *     lever is released (or a global cap trips). You control pre-infusion
 *     yourself with the lever.
 *   - ::BREW_PROFILE_AUTO — timed pre-infusion -> bloom/hold -> extraction.
 *
 * The stage model is intentionally general so further profiles (and pressure
 * profiling, once a pressure sensor is fitted) drop in as new stages without
 * changing the engine. The controller does no I/O.
 *
 * The E61 group's 3-way valve is mechanical (the lever opens the brew path and
 * vents the puck), so this controller commands only the pump and timing.
 */
#ifndef ESPRESSO_CORE_BREW_H
#define ESPRESSO_CORE_BREW_H

#include "core/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Built-in profile identifiers. */
typedef enum {
    BREW_PROFILE_MANUAL = 0,
    BREW_PROFILE_AUTO,
    BREW_PROFILE_COUNT
} brew_profile_type_t;

/** How the pump is driven during a stage. */
typedef enum {
    BREW_PUMP_OFF = 0,  /**< Pump off (bloom/hold, or line-pressure soak). */
    BREW_PUMP_DUTY,     /**< Run the pump at a fixed duty.                 */
    BREW_PUMP_PRESSURE  /**< Future: regulate to a target pressure (needs a
                             pressure sensor; treated as full pump for now). */
} brew_pump_mode_t;

/** What ends a stage. */
typedef enum {
    BREW_END_MANUAL = 0, /**< Ends only on lever release / global cap.   */
    BREW_END_TIME,       /**< Ends after @c duration_ms.                 */
    BREW_END_VOLUME      /**< Ends when shot volume >= @c volume_ml.      */
} brew_stage_end_t;

typedef struct {
    brew_pump_mode_t mode;
    float            pump_duty;    /**< For ::BREW_PUMP_DUTY, 0..1.        */
    float            pressure_bar; /**< For ::BREW_PUMP_PRESSURE (future). */
    brew_stage_end_t end;
    uint32_t         duration_ms;  /**< For ::BREW_END_TIME.               */
    float            volume_ml;    /**< For ::BREW_END_VOLUME.             */
} brew_stage_t;

#define BREW_MAX_STAGES 6

typedef struct {
    brew_profile_type_t type;
    uint8_t             stage_count;
    brew_stage_t        stages[BREW_MAX_STAGES];
    float               target_volume_ml; /**< Global volumetric stop; 0=off. */
    uint32_t            max_shot_ms;       /**< Global hard time cap.          */
} brew_profile_t;

/** Scalar, user-editable parameters used to build the built-in profiles. */
typedef struct {
    uint32_t preinfuse_ms;
    uint32_t hold_ms;
    float    preinfuse_pump;
    float    extract_pump;
    float    target_volume_ml;
    uint32_t max_shot_ms;
} brew_params_t;

/** Build a built-in profile of type @p type from scalar @p params. */
void brew_profile_build(brew_profile_t *out, brew_profile_type_t type,
                        const brew_params_t *params);

/** Human-readable profile name. */
const char *brew_profile_name(brew_profile_type_t type);

typedef struct {
    brew_profile_t profile;
    uint8_t        stage;
    esp_ms_t       shot_start;
    esp_ms_t       stage_start;
    bool           running;
} brew_t;

typedef struct {
    bool             pump_on;
    float            pump_duty;
    uint8_t          stage;
    brew_pump_mode_t mode;
    uint32_t         elapsed_ms;
    bool             done; /**< True once the shot has finished.  */
} brew_output_t;

/** Initialise the controller with a profile (idle until ::brew_start). */
void brew_init(brew_t *b, const brew_profile_t *profile);

/** Begin a shot at monotonic time @p now (volume is measured from here). */
void brew_start(brew_t *b, esp_ms_t now);

/** Stop a shot immediately (e.g. lever released). */
void brew_stop(brew_t *b);

/**
 * @brief Advance the shot.
 * @param now       Monotonic time in ms.
 * @param volume_ml Accumulated dispensed volume since ::brew_start.
 */
brew_output_t brew_update(brew_t *b, esp_ms_t now, float volume_ml);

/** True while a shot is running. */
bool brew_active(const brew_t *b);

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_CORE_BREW_H */
