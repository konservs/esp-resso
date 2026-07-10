#include "core/load_guard.h"

load_guard_output_t load_guard_apply(const load_guard_input_t *in,
                                     uint8_t max_active)
{
    load_guard_output_t out = { { 0.0f, 0.0f, 0.0f, 0.0f } };

    uint8_t cap = max_active > LOAD_HEATER_COUNT ? LOAD_HEATER_COUNT : max_active;

    /* How many elements each boiler wants this step. Brew warms on both
     * elements but only needs its lower element to hold temperature; steam
     * always uses both when it calls for heat. */
    const uint8_t brew_want =
        (in->brew_duty > 0.0f) ? (in->brew_at_setpoint ? 1u : 2u) : 0u;
    const uint8_t steam_want = (in->steam_duty > 0.0f) ? 2u : 0u;

    /* Brew has priority; steam takes what remains under the cap. */
    const uint8_t brew_grant = brew_want < cap ? brew_want : cap;
    const uint8_t remaining = (uint8_t)(cap - brew_grant);
    const uint8_t steam_grant = steam_want < remaining ? steam_want : remaining;

    /* Fill lower elements before upper ones. */
    if (brew_grant >= 1) out.duty[LOAD_BREW_LO] = in->brew_duty;
    if (brew_grant >= 2) out.duty[LOAD_BREW_HI] = in->brew_duty;
    if (steam_grant >= 1) out.duty[LOAD_STEAM_LO] = in->steam_duty;
    if (steam_grant >= 2) out.duty[LOAD_STEAM_HI] = in->steam_duty;

    return out;
}
