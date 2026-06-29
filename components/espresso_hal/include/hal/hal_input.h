/**
 * @file hal_input.h
 * @brief Raw user inputs: the two UI buttons and the machine control switches.
 *
 * The default control surface is two debounced buttons (A = left/minus,
 * B = right/plus). The HAL only reports their *raw pressed state*; all gesture
 * interpretation (tap / hold / chord) lives in the portable @c core/buttons
 * module, so the input scheme stays modular — swap in a rotary encoder by
 * providing a different source that feeds the same UI controller.
 *
 * The brew lever and steam knob are exposed as separate level signals because
 * they map directly to machine state, not to the UI.
 */
#ifndef ESPRESSO_HAL_INPUT_H
#define ESPRESSO_HAL_INPUT_H

#include "hal/hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Debounced pressed-state of the two UI buttons. */
typedef struct {
    bool a; /**< Button A (left / minus).  */
    bool b; /**< Button B (right / plus).  */
} hal_buttons_t;

/** Initialise button GPIOs and control-switch inputs. */
espresso_result_t hal_input_init(void);

/** True if the input expander is responding (last I2C read succeeded). */
bool hal_input_ok(void);

/** Read the current debounced button states. */
hal_buttons_t hal_buttons_read(void);

/** True while the E61 brew lever / paddle is engaged. */
bool hal_switch_brew(void);

/** True while the steam knob microswitch is engaged. */
bool hal_switch_steam(void);

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_HAL_INPUT_H */
