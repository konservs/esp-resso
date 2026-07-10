/**
 * @file rtd.h
 * @brief PT100/PT1000 RTD resistance-to-temperature conversion (pure math).
 *
 * The MAX31865 front-end returns an RTD resistance; converting it to a
 * temperature uses the Callendar-Van Dusen equation. That conversion is pure
 * arithmetic, so it lives in the portable core and is unit-tested on the host,
 * while the SPI transport lives in the ESP32 driver.
 */
#ifndef ESPRESSO_CORE_RTD_H
#define ESPRESSO_CORE_RTD_H

#include "core/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Nominal resistance at 0 degrees C: 100 ohm for PT100, 1000 ohm for PT1000. */
#define RTD_PT100_R0  100.0f
#define RTD_PT1000_R0 1000.0f

/** Plausible resistance band for a working probe, as a fraction of R0. A reading
 *  outside this is not a real temperature: a near-zero value means a dead/absent
 *  front-end (which would otherwise convert to a bogus ~-247 C), and a very high
 *  one an open/short the MAX31865's own detector missed. The band spans roughly
 *  -125..+266 C for a PT100 — far outside anything an espresso machine sees — so
 *  a genuine reading never trips it. The driver treats out-of-band as a fault. */
#define RTD_PLAUSIBLE_MIN_RATIO 0.5f
#define RTD_PLAUSIBLE_MAX_RATIO 2.0f

/**
 * @brief Convert an RTD resistance to a temperature in degrees Celsius.
 *
 * Valid for T >= 0 C (the espresso operating range). Uses the positive branch
 * of the Callendar-Van Dusen equation:
 *     R(T) = R0 * (1 + A*T + B*T^2)
 * solved for T.
 *
 * @param resistance_ohms Measured RTD resistance.
 * @param r0_ohms         Nominal resistance at 0 C (e.g. ::RTD_PT100_R0).
 * @return Temperature in degrees Celsius.
 */
temp_c_t rtd_resistance_to_celsius(float resistance_ohms, float r0_ohms);

/**
 * @brief Is @p resistance_ohms a physically plausible reading for this probe?
 * @param resistance_ohms Measured RTD resistance.
 * @param r0_ohms         Nominal resistance at 0 C (e.g. ::RTD_PT100_R0).
 * @return false for a near-zero / absurdly-high resistance (a dead front-end or
 *         undetected open/short); true for anything in the working band.
 */
bool rtd_resistance_plausible(float resistance_ohms, float r0_ohms);

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_CORE_RTD_H */
