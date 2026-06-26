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
} hal_temp_reading_t;

/** Initialise the temperature front-end(s). */
espresso_result_t hal_temp_init(void);

/** Read one boiler's temperature sensor. */
hal_temp_reading_t hal_temp_read(hal_boiler_id_t boiler);

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_HAL_TEMP_H */
