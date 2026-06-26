#include "core/buttons.h"

void buttons_init(buttons_t *b, const button_config_t *cfg)
{
    b->cfg = *cfg;
    b->state = BST_IDLE;
    b->t_start = 0;
    b->t_last_repeat = 0;
    b->hold_fired = false;
}

static void begin(buttons_t *b, button_state_t st, esp_ms_t now)
{
    b->state = st;
    b->t_start = now;
    b->t_last_repeat = now;
    b->hold_fired = false;
}

/* Decide what a single-button release emits given how long it was held. */
static button_event_t single_release(const buttons_t *b, esp_ms_t now,
                                     button_event_t tap, button_event_t hold)
{
    if (b->hold_fired) {
        return BTN_NONE; /* hold already reported while pressed */
    }
    return espresso_elapsed_ms(b->t_start, now) >= b->cfg.hold_ms ? hold : tap;
}

/* Handle auto-repeat for a held single button. */
static button_event_t single_hold(buttons_t *b, esp_ms_t now, button_event_t hold)
{
    const uint32_t held = espresso_elapsed_ms(b->t_start, now);
    if (!b->hold_fired) {
        if (held >= b->cfg.hold_ms) {
            b->hold_fired = true;
            b->t_last_repeat = now;
            return hold;
        }
        return BTN_NONE;
    }
    if (b->cfg.repeat_ms > 0 &&
        espresso_elapsed_ms(b->t_last_repeat, now) >= b->cfg.repeat_ms) {
        b->t_last_repeat = now;
        return hold;
    }
    return BTN_NONE;
}

button_event_t buttons_update(buttons_t *b, bool a, bool b_pressed, esp_ms_t now)
{
    switch (b->state) {
    case BST_IDLE:
        if (a && b_pressed) {
            begin(b, BST_BOTH, now);
        } else if (a) {
            begin(b, BST_A, now);
        } else if (b_pressed) {
            begin(b, BST_B, now);
        }
        return BTN_NONE;

    case BST_A:
        if (a && b_pressed) {           /* second button joined -> chord */
            begin(b, BST_BOTH, now);
            return BTN_NONE;
        }
        if (!a) {
            b->state = BST_IDLE;
            return single_release(b, now, BTN_A_TAP, BTN_A_HOLD);
        }
        return single_hold(b, now, BTN_A_HOLD);

    case BST_B:
        if (a && b_pressed) {
            begin(b, BST_BOTH, now);
            return BTN_NONE;
        }
        if (!b_pressed) {
            b->state = BST_IDLE;
            return single_release(b, now, BTN_B_TAP, BTN_B_HOLD);
        }
        return single_hold(b, now, BTN_B_HOLD);

    case BST_BOTH: {
        const bool both_up = !a && !b_pressed;
        const bool one_up = !a || !b_pressed;
        if (both_up || one_up) {
            /* If only one came up, swallow the remaining press in WAIT_RELEASE
             * so it does not register as a stray single tap. */
            b->state = both_up ? BST_IDLE : BST_WAIT_RELEASE;
            return b->hold_fired ? BTN_NONE : BTN_BOTH_TAP;
        }
        if (!b->hold_fired &&
            espresso_elapsed_ms(b->t_start, now) >= b->cfg.hold_ms) {
            b->hold_fired = true;
            return BTN_BOTH_HOLD;
        }
        return BTN_NONE;
    }

    case BST_WAIT_RELEASE:
    default:
        if (!a && !b_pressed) {
            b->state = BST_IDLE;
        }
        return BTN_NONE;
    }
}
