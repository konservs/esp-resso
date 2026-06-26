/**
 * @file ui.h
 * @brief Portable UI / configuration-menu controller.
 *
 * Consumes ::button_event_t gestures and drives a tiny three-screen UI:
 *
 *   STATUS  --(both-hold)-->  MENU  --(both-tap on item)-->  EDIT
 *      ^                       |  ^                            |
 *      +----(both-hold)--------+  +------(both-tap)------------+
 *
 *   - MENU: A/B tap or hold scrolls the item list.
 *   - EDIT: A/B tap or hold decrements/increments the selected setting
 *           (holding auto-repeats); both-tap returns to MENU.
 *   - both-hold from anywhere returns to STATUS, persisting changes.
 *
 * Edits are applied to a caller-owned ::settings_t. Rendering is left to the
 * platform (the UI task reads this state and draws it on the OLED), so the
 * controller stays display-agnostic and host-testable.
 */
#ifndef ESPRESSO_CORE_UI_H
#define ESPRESSO_CORE_UI_H

#include <stddef.h>

#include "core/buttons.h"
#include "core/settings.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_STATUS = 0,
    UI_MENU,
    UI_EDIT
} ui_screen_t;

typedef enum {
    UI_ITEM_PROFILE = 0,
    UI_ITEM_BREW_TEMP,
    UI_ITEM_STEAM_TEMP,
    UI_ITEM_PREINFUSE,
    UI_ITEM_TARGET_VOL,
    UI_ITEM_EXIT,
    UI_ITEM_COUNT
} ui_item_t;

typedef struct {
    ui_screen_t screen;
    int         item;     /**< Selected menu item (::ui_item_t).         */
    settings_t *settings; /**< Live settings being edited (not owned).   */
    bool        dirty;    /**< Settings changed since the last save.     */
} ui_t;

/** Initialise the UI in the STATUS screen, editing @p settings in place. */
void ui_init(ui_t *ui, settings_t *settings);

/** Apply one gesture, updating screen/selection/settings as appropriate. */
void ui_handle(ui_t *ui, button_event_t ev);

ui_screen_t ui_screen(const ui_t *ui);
ui_item_t   ui_item(const ui_t *ui);

/** Short label for a menu item (for the display). */
const char *ui_item_label(ui_item_t item);

/** Format an item's current value into @p buf (e.g. "93.0C"). */
void ui_item_value(const ui_t *ui, ui_item_t item, char *buf, size_t len);

/** Return whether settings changed and clear the flag (caller then persists). */
bool ui_take_dirty(ui_t *ui);

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_CORE_UI_H */
