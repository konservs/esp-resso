/**
 * @file hal_temp.h
 * @brief Boiler temperature sensing (PT100 RTD via MAX31865 on the ESP32).
 */
#ifndef ESPRESSO_HAL_TEMP_H
#define ESPRESSO_HAL_TEMP_H

#include "hal/hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A single temperature reading plus a validity flag. */
typedef struct {
    temp_c_t celsius; /**< Converted temperature; meaningless if !ok.        */
    bool     ok;      /**< False on sensor open/short or out-of-range fault. */
    uint8_t  fault;   /**< MAX31865 fault byte when !ok (0xFF = no SPI comms);
                           0 when ok. See max31865.h. */
} hal_temp_reading_t;

/** Initialise the temperature front-end(s). */
espresso_result_t hal_temp_init(void);

/** Read one boiler's temperature sensor. */
hal_temp_reading_t hal_temp_read(hal_boiler_id_t boiler);

/* ---- bench debug: simulated RTDs, heaters hard-disabled ---------
 * 1 = the MAX31865 front-ends are never touched (not even initialised) and
 * ::hal_temp_read returns a fixed, always-ok reading per boiler
 * (::TEMP_SIM_BREW_C / ::TEMP_SIM_STEAM_C). Use it to get past a
 * SENSOR_FAULT trip on a bench board whose probes are not wired yet, so the
 * state machine, level sensing, UI and dashboard can be exercised.
 *
 * The firmware then has NO idea how hot the boilers actually are, so while
 * this is set the heaters are refused twice over: the control loop zeroes both
 * boiler duties, and ::hal_heater_set_duty ignores whatever it is handed, so no
 * SSR can be driven even if some other path asks. The safety supervisor is
 * *not* a backstop here — it is fed the simulated temperatures too.
 *
 * Set back to 0 before the machine is anywhere near mains. */
#define TEMP_SIM 1

/** Simulated brew-boiler temperature while ::TEMP_SIM is set (deg C). */
#define TEMP_SIM_BREW_C  93.0f
/** Simulated steam-boiler temperature while ::TEMP_SIM is set (deg C). */
#define TEMP_SIM_STEAM_C 119.0f

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_HAL_TEMP_H */
