/**
 * @file app.h
 * @brief Shared application context and task entry points.
 *
 * The firmware is organised as a small set of FreeRTOS tasks that share one
 * @ref app_state_t guarded by a mutex, and communicate machine transitions
 * through an event queue. The tasks own all RTOS concerns; the decision logic
 * they call lives in the portable @c core library.
 */
#ifndef ESPRESSO_APP_H
#define ESPRESSO_APP_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "core/boiler.h"
#include "core/brew.h"
#include "core/pump_guard.h"
#include "core/safety.h"
#include "core/settings.h"
#include "core/state_machine.h"
#include "hal/hal_temp.h"

/** Water-level status for a boiler, for the diagnostics view. */
typedef enum {
    LVL_FULL = 0,  /**< Probe covered.                               */
    LVL_FILLING,   /**< Uncovered and the fill valve is open.        */
    LVL_LOW,       /**< Uncovered, fill held off (e.g. machine fault). */
    LVL_ERROR      /**< Uncovered but the reservoir is empty.        */
} level_status_t;

/** Control-loop period. Boiler PIDs and the brew controller run at this rate. */
#define CONTROL_PERIOD_MS 100
/** Safety supervisor period (runs faster, at higher priority). */
#define SAFETY_PERIOD_MS  50
/** Display refresh period. */
#define UI_PERIOD_MS      125

/** Shared machine state. Hold @ref app_state_t::lock when touching it. */
typedef struct {
    settings_t settings;
    machine_t  machine;
    boiler_t   brew_boiler;
    boiler_t   steam_boiler;
    brew_t     brew;
    pump_guard_t pump_guard;
    safety_t   safety;

    /* Live telemetry published by the control task (for UI / safety / net). */
    hal_temp_reading_t brew_temp;
    hal_temp_reading_t steam_temp;
    float    brew_duty;
    float    steam_duty;
    bool     heater_active[LOAD_HEATER_COUNT]; /**< Per-element drive state after
                                                *   the load guard (::load_heater_t
                                                *   order); for the UI indicators. */
    float    shot_volume_ml;
    uint32_t shot_elapsed_ms;
    float    flow_rate_ml_s;   /**< Live flow rate (smoothed), ml/s.          */
    bool     both_ready;
    bool     pump_cooling;     /**< Pump duty-cycle guard is resting.         */
    uint32_t pump_cooldown_ms; /**< Rest remaining before a shot can start.   */

    /* Water-level diagnostics published by the control task. */
    level_status_t brew_level;
    level_status_t steam_level;
    bool     reservoir_present;
    bool     valve_brew_open;  /**< Brew auto-fill solenoid commanded open.   */
    bool     valve_steam_open; /**< Steam auto-fill solenoid commanded open.  */

    SemaphoreHandle_t lock;   /**< Guards this struct.               */
    QueueHandle_t     events; /**< machine_event_t produced by tasks. */
} app_state_t;

/** The single global application context. */
extern app_state_t g_app;

/** Immutable snapshot of live state, for the UI and the Wi-Fi dashboard. */
typedef struct {
    machine_state_t state;
    safety_trip_t   safety_trip;
    temp_c_t brew_temp;
    temp_c_t steam_temp;
    temp_c_t brew_setpoint;
    temp_c_t steam_setpoint;
    bool     brew_sensor_ok;
    bool     steam_sensor_ok;
    uint8_t  brew_temp_fault;  /**< MAX31865 fault byte when !brew_sensor_ok.  */
    uint8_t  steam_temp_fault; /**< MAX31865 fault byte when !steam_sensor_ok. */
    float    brew_duty;
    float    steam_duty;
    bool     both_ready;
    float    shot_volume_ml;
    uint32_t shot_elapsed_ms;
    float    flow_rate_ml_s;   /**< Live flow rate (smoothed), ml/s. */

    /* Pump duty-cycle guard: resting (can't brew) + remaining cooldown. */
    bool     pump_cooling;
    uint32_t pump_cooldown_ms;

    /* Component self-check for the diagnostics view. */
    bool           display_ok;
    bool           buttons_ok;
    level_status_t brew_level;
    level_status_t steam_level;
    bool           reservoir_ok;
    bool           valve_brew_open;  /**< Brew auto-fill solenoid open.  */
    bool           valve_steam_open; /**< Steam auto-fill solenoid open. */
} app_telemetry_t;

/** Post a machine event from any task/ISR-safe context. */
void app_post_event(machine_event_t ev);

/** Copy a consistent snapshot of live state (takes the lock internally). */
void app_get_telemetry(app_telemetry_t *out);

/** Rebuild the brew controller from the current settings (profile + params).
 *  Caller must hold @ref app_state_t::lock. */
void app_apply_brew_profile(app_state_t *app);

/** Clear saved Wi-Fi credentials and reboot into the SoftAP setup portal.
 *  Safe to call from any task; a no-op when Wi-Fi is disabled at build time. */
void net_request_provisioning(void);

/* Task entry points (created in app_main). */
void control_task(void *arg);
void safety_task(void *arg);
void ui_task(void *arg);
void net_task(void *arg);

#endif /* ESPRESSO_APP_H */
