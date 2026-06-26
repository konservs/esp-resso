/**
 * @file hal_time.h
 * @brief Monotonic time and blocking delay.
 */
#ifndef ESPRESSO_HAL_TIME_H
#define ESPRESSO_HAL_TIME_H

#include "core/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Milliseconds since boot from a monotonic source (esp_timer on ESP32). */
esp_ms_t hal_time_ms(void);

/** Block the calling task for @p ms milliseconds. */
void hal_delay_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_HAL_TIME_H */
