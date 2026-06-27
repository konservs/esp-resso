# User interface

The on-machine UI is deliberately **modular**: the display and the input device
are abstractions, and all interaction logic is portable, host-tested `core` code
([`core/buttons.c`](../components/core/src/buttons.c),
[`core/ui.c`](../components/core/src/ui.c)).

## Display

A 128×64 I2C **SSD1306 OLED** (address 0x3C) shows:

- **Status screen:** machine state, brew & steam temperatures vs setpoints, and
  the live shot timer while brewing.
- **Config screens:** the selected menu item and its value.

`hal_display` is a small, panel-agnostic text/graphics API
([`hal/hal_display.h`](../components/espresso_hal/include/hal/hal_display.h)) over an
in-RAM framebuffer; the SSD1306 driver
([`drivers/ssd1306.c`](../components/drivers/src/ssd1306.c)) implements it. Swap
in another small display by providing a different `hal_display` backend.

## Input: two buttons

The default control surface is **two buttons** — **A (− / left)** and
**B (+ / right)** — wired active-low. The HAL only reports their raw state
(`hal_buttons_read`); the portable `core/buttons` module interprets gestures so
the *meaning* of the buttons is testable and the hardware is swappable (a rotary
encoder can feed the same gestures).

Gestures (see [`core/buttons.h`](../components/core/include/core/buttons.h)):

| Gesture | Meaning |
|---------|---------|
| Tap A / Tap B | previous / next item, or − / + a value |
| Hold A / Hold B | auto-repeat − / + (fast value changes) |
| Both tapped | select / confirm |
| **Both held** | **enter / exit configuration** |

Timing (`hold_ms`, `repeat_ms`) is configured where the buttons are created in
`ui_task`. Near-simultaneous presses are coalesced into a chord, so a quick
"both" never registers a stray single tap.

## Menu

`core/ui` is a three-screen controller:

```
STATUS --(both-hold)--> MENU --(both-tap on item)--> EDIT
   ^                      |  ^                          |
   +-----(both-hold)------+  +--------(both-tap)--------+
```

- **MENU:** A/B scroll the item list (Profile, Brew temp, Steam temp,
  Pre-infuse, Shot volume, Exit).
- **EDIT:** A/B (tap or hold-to-repeat) decrement/increment the value, clamped to
  sane ranges; both-tap returns to the menu.
- **Both-hold** anywhere returns to STATUS and **persists** changes (the new
  settings are applied to the controllers and saved to NVS).

Edits mutate a `settings_t` in place; `ui_take_dirty()` tells the task when to
save. All of this is exercised in [`tests/test_ui.c`](../tests/test_ui.c) and
[`tests/test_buttons.c`](../tests/test_buttons.c).
