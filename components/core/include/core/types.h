/**
 * @file types.h
 * @brief Shared scalar types and tiny helpers used across the portable core.
 *
 * The "core" library contains only hardware- and RTOS-independent logic, so it
 * may be compiled both for the ESP32 firmware and natively on a host for unit
 * tests. Nothing in here may include FreeRTOS, ESP-IDF or platform headers.
 */
#ifndef ESPRESSO_CORE_TYPES_H
#define ESPRESSO_CORE_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Milliseconds since an arbitrary monotonic epoch (typically boot). */
typedef uint32_t esp_ms_t;

/** Temperature in degrees Celsius. Single precision matches the ESP32 FPU. */
typedef float temp_c_t;

/** Generic result codes returned by core APIs. */
typedef enum {
    ESPRESSO_OK = 0,
    ESPRESSO_ERR_INVALID_ARG,
    ESPRESSO_ERR_RANGE,
    ESPRESSO_ERR_STATE
} espresso_result_t;

/** Clamp @p v into the inclusive range [@p lo, @p hi]. */
static inline float espresso_clampf(float v, float lo, float hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

/** Unsigned millisecond delta that is robust to monotonic counter wrap-around. */
static inline esp_ms_t espresso_elapsed_ms(esp_ms_t start, esp_ms_t now)
{
    return (esp_ms_t)(now - start);
}

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_CORE_TYPES_H */
