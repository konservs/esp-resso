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

/**
 * @brief Sample every input once, into a snapshot the accessors then read from.
 *
 * Call this once per control cycle, before the accessors below. They report the
 * snapshot rather than going to the hardware themselves, so all the inputs a
 * cycle acts on are consistent with each other and the cost of reading them
 * does not scale with how many there are — which matters when they share one
 * I2C expander at the far end of a cable, where each transfer is both slow and
 * a chance to fail. Without a poll the accessors still work, returning whatever
 * the last snapshot holds.
 */
void hal_input_poll(void);

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
