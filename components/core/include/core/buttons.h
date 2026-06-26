/**
 * @file buttons.h
 * @brief Two-button gesture interpreter (the default, modular input scheme).
 *
 * The simplest control surface is two buttons — "A" (left / minus) and "B"
 * (right / plus). This module turns the raw, debounced button states into
 * higher-level UI gestures:
 *
 *   - short press of A or B            -> ::BTN_A_TAP / ::BTN_B_TAP
 *   - press-and-hold of A or B         -> ::BTN_A_HOLD / ::BTN_B_HOLD
 *                                         (fires at the hold threshold, then
 *                                          auto-repeats every @c repeat_ms)
 *   - both pressed, short              -> ::BTN_BOTH_TAP   (select / confirm)
 *   - both pressed-and-held            -> ::BTN_BOTH_HOLD  (enter/exit config)
 *
 * Time is injected (@p now in ms) so the logic is fully deterministic and
 * unit-testable. A different input device (e.g. a rotary encoder) can drive the
 * same downstream ::ui_t by emitting the same gesture vocabulary.
 */
#ifndef ESPRESSO_CORE_BUTTONS_H
#define ESPRESSO_CORE_BUTTONS_H

#include "core/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BTN_NONE = 0,
    BTN_A_TAP,
    BTN_B_TAP,
    BTN_A_HOLD,
    BTN_B_HOLD,
    BTN_BOTH_TAP,
    BTN_BOTH_HOLD
} button_event_t;

typedef struct {
    uint32_t hold_ms;   /**< Press duration that counts as a hold.            */
    uint32_t repeat_ms; /**< Auto-repeat period for single-button holds (0=off). */
} button_config_t;

typedef enum {
    BST_IDLE = 0,
    BST_A,
    BST_B,
    BST_BOTH,
    BST_WAIT_RELEASE
} button_state_t;

typedef struct {
    button_config_t cfg;
    button_state_t  state;
    esp_ms_t        t_start;       /**< Start of the current gesture.   */
    esp_ms_t        t_last_repeat; /**< Last auto-repeat emission.       */
    bool            hold_fired;    /**< Hold already reported.           */
} buttons_t;

/** Initialise the interpreter. */
void buttons_init(buttons_t *b, const button_config_t *cfg);

/**
 * @brief Feed the current (debounced) button states for time @p now.
 * @param a Pressed state of button A.
 * @param b Pressed state of button B.
 * @return One gesture event, or ::BTN_NONE if nothing happened this tick.
 */
button_event_t buttons_update(buttons_t *b, bool a, bool b_pressed, esp_ms_t now);

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_CORE_BUTTONS_H */
