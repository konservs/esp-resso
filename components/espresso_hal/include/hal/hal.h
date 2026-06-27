/**
 * @file hal.h
 * @brief Hardware Abstraction Layer — umbrella header and shared identifiers.
 *
 * The HAL is the contract between the portable @c core logic and the physical
 * machine. The ESP32 firmware (@c components/drivers) implements these
 * functions against real peripherals; host builds can provide fakes (see
 * tests/) to exercise code that talks to the HAL.
 *
 * Naming: every function is prefixed @c hal_ and groups live in @c hal_*.h.
 */
#ifndef ESPRESSO_HAL_H
#define ESPRESSO_HAL_H

#include "core/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Boiler / heater channels. */
typedef enum {
    HAL_BOILER_BREW = 0,
    HAL_BOILER_STEAM,
    HAL_BOILER_COUNT
} hal_boiler_id_t;

/** Solenoid valve channels. The E61 group's 3-way valve is mechanical
 *  (actuated by the lever), so the firmware only drives the boiler auto-fill
 *  inlets. */
typedef enum {
    HAL_VALVE_FILL_BREW = 0, /**< Brew boiler auto-fill inlet.  */
    HAL_VALVE_FILL_STEAM,    /**< Steam boiler auto-fill inlet. */
    HAL_VALVE_COUNT
} hal_valve_id_t;

/** Water-level probe channels. */
typedef enum {
    HAL_LEVEL_BREW = 0,
    HAL_LEVEL_STEAM,
    HAL_LEVEL_RESERVOIR,
    HAL_LEVEL_COUNT
} hal_level_id_t;

/** Bring up all peripherals. Call once at boot before any other HAL call.
 *  Named espresso_hal_init (not hal_init) to avoid clashing with the hal_init
 *  symbol exported by the ESP32 Wi-Fi blobs. */
espresso_result_t espresso_hal_init(void);

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_HAL_H */
