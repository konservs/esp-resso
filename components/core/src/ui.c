#include "core/ui.h"

#include <stdio.h>

void ui_init(ui_t *ui, settings_t *settings)
{
    ui->screen = UI_STATUS;
    ui->item = UI_ITEM_BREW_TEMP;
    ui->settings = settings;
    ui->dirty = false;
}

ui_screen_t ui_screen(const ui_t *ui) { return ui->screen; }
ui_item_t   ui_item(const ui_t *ui)   { return (ui_item_t)ui->item; }

const char *ui_item_label(ui_item_t item)
{
    switch (item) {
    case UI_ITEM_PROFILE:    return "Profile";
    case UI_ITEM_BREW_TEMP:  return "Brew temp";
    case UI_ITEM_STEAM_TEMP: return "Steam temp";
    case UI_ITEM_PREINFUSE:  return "Pre-infuse";
    case UI_ITEM_TARGET_VOL: return "Shot volume";
    case UI_ITEM_EXIT:       return "Exit";
    default:                 return "?";
    }
}

void ui_item_value(const ui_t *ui, ui_item_t item, char *buf, size_t len)
{
    const settings_t *s = ui->settings;
    switch (item) {
    case UI_ITEM_PROFILE:
        snprintf(buf, len, "%s", brew_profile_name(s->active_profile));
        break;
    case UI_ITEM_BREW_TEMP:
        snprintf(buf, len, "%.1f C", (double)s->brew_setpoint);
        break;
    case UI_ITEM_STEAM_TEMP:
        snprintf(buf, len, "%.1f C", (double)s->steam_setpoint);
        break;
    case UI_ITEM_PREINFUSE:
        snprintf(buf, len, "%lu ms", (unsigned long)s->brew.preinfuse_ms);
        break;
    case UI_ITEM_TARGET_VOL:
        snprintf(buf, len, "%.0f ml", (double)s->brew.target_volume_ml);
        break;
    case UI_ITEM_EXIT:
    default:
        snprintf(buf, len, "%s", "");
        break;
    }
}

bool ui_take_dirty(ui_t *ui)
{
    const bool d = ui->dirty;
    ui->dirty = false;
    return d;
}

/* Adjust the currently selected setting by @p dir (+1 / -1) * its step. */
static void adjust(ui_t *ui, int dir)
{
    settings_t *s = ui->settings;
    switch ((ui_item_t)ui->item) {
    case UI_ITEM_PROFILE: {
        int v = (int)s->active_profile + dir;
        /* Wrap within the available profiles. */
        v = (v % BREW_PROFILE_COUNT + BREW_PROFILE_COUNT) % BREW_PROFILE_COUNT;
        s->active_profile = (brew_profile_type_t)v;
        break;
    }
    case UI_ITEM_BREW_TEMP:
        s->brew_setpoint = espresso_clampf(s->brew_setpoint + dir * 0.5f, 80.0f, 110.0f);
        break;
    case UI_ITEM_STEAM_TEMP:
        s->steam_setpoint = espresso_clampf(s->steam_setpoint + dir * 0.5f, 110.0f, 145.0f);
        break;
    case UI_ITEM_PREINFUSE: {
        long v = (long)s->brew.preinfuse_ms + dir * 250;
        if (v < 0) v = 0;
        if (v > 10000) v = 10000;
        s->brew.preinfuse_ms = (uint32_t)v;
        break;
    }
    case UI_ITEM_TARGET_VOL:
        s->brew.target_volume_ml = espresso_clampf(s->brew.target_volume_ml + dir * 2.0f, 0.0f, 100.0f);
        break;
    case UI_ITEM_EXIT:
    default:
        return; /* nothing to adjust */
    }
    ui->dirty = true;
}

static void menu_scroll(ui_t *ui, int delta)
{
    ui->item = (ui->item + UI_ITEM_COUNT + delta) % UI_ITEM_COUNT;
}

void ui_handle(ui_t *ui, button_event_t ev)
{
    switch (ui->screen) {
    case UI_STATUS:
        if (ev == BTN_BOTH_HOLD) {
            ui->screen = UI_MENU;
            ui->item = UI_ITEM_BREW_TEMP;
        }
        break;

    case UI_MENU:
        switch (ev) {
        case BTN_A_TAP:
        case BTN_A_HOLD:
            menu_scroll(ui, -1);
            break;
        case BTN_B_TAP:
        case BTN_B_HOLD:
            menu_scroll(ui, +1);
            break;
        case BTN_BOTH_TAP:
            ui->screen = (ui->item == UI_ITEM_EXIT) ? UI_STATUS : UI_EDIT;
            break;
        case BTN_BOTH_HOLD:
            ui->screen = UI_STATUS;
            break;
        default:
            break;
        }
        break;

    case UI_EDIT:
        switch (ev) {
        case BTN_A_TAP:
        case BTN_A_HOLD:
            adjust(ui, -1);
            break;
        case BTN_B_TAP:
        case BTN_B_HOLD:
            adjust(ui, +1);
            break;
        case BTN_BOTH_TAP:
            ui->screen = UI_MENU;
            break;
        case BTN_BOTH_HOLD:
            ui->screen = UI_STATUS;
            break;
        default:
            break;
        }
        break;

    default:
        break;
    }
}
