#include "core/rtd.h"

#include <math.h>

/* Callendar-Van Dusen coefficients for a standard IEC 60751 platinum RTD. */
#define RTD_CVD_A 3.9083e-3f
#define RTD_CVD_B (-5.775e-7f)

temp_c_t rtd_resistance_to_celsius(float resistance_ohms, float r0_ohms)
{
    if (r0_ohms <= 0.0f) {
        return 0.0f;
    }

    /* Solve R = R0 (1 + A T + B T^2) for T using the quadratic formula.
     *   B T^2 + A T + (1 - R/R0) = 0
     * The physically meaningful root (T >= 0) takes the '+' branch. */
    const float ratio = resistance_ohms / r0_ohms;
    const float disc = RTD_CVD_A * RTD_CVD_A - 4.0f * RTD_CVD_B * (1.0f - ratio);

    if (disc < 0.0f) {
        return 0.0f; /* Out of model range; caller treats as sensor fault. */
    }

    return (temp_c_t)((-RTD_CVD_A + sqrtf(disc)) / (2.0f * RTD_CVD_B));
}
