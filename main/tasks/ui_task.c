/**
 * @file ui_task.c
 * @brief Reads the two buttons + control switches and renders the SSD1306 OLED.
 *
 * Button gestures are interpreted by core/buttons and fed to the core/ui menu
 * controller; the brew/steam switches are translated to machine events. The
 * input scheme is modular: replacing the two buttons with a rotary encoder only
 * requires a different source feeding the same ::ui_t.
 */
#include "app.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "core/buttons.h"
#include "core/ui.h"

#include "hal/hal_display.h"
#include "hal/hal_input.h"
#include "hal/hal_storage.h"
#include "hal/hal_time.h"

/* Status-screen layout. A compact temperature bar lives in the panel's yellow
 * strip (top 16 px): each boiler's number on the left of its half, its two
 * heater-element squares stuck to the right edge (empty = element off, filled =
 * driven). The machine status fills the blue area below (rows 16-63). */
#define CELL_W     64 /* half the panel — one boiler block            */
#define HDR_H      16 /* temperature-bar height (the yellow strip)    */
#define HDR_TEMP_Y  4 /* temperature text row, in the yellow strip    */
#define SQ_SIZE     5 /* heater-element square, px                     */
#define SQ_LO_Y     2 /* lower element square (top)                    */
#define SQ_HI_Y     9 /* upper element square (bottom)                 */
#define SQ_PAD      2 /* square gap from the block's right edge        */
#define MAIN_Y1    28 /* primary status line (blue area)              */
#define MAIN_Y2    44 /* secondary detail line (blue area)            */

/* Friendly one-word status for the top line. */
static const char *status_text(machine_state_t s)
{
    switch (s) {
    case MACHINE_BOOT:      return "Starting...";
    case MACHINE_HEATING:   return "Heating...";
    case MACHINE_READY:     return "Ready";
    case MACHINE_BREWING:   return "Brewing";
    case MACHINE_STEAMING:  return "Steaming";
    case MACHINE_BACKFLUSH: return "Backflush";
    case MACHINE_SLEEP:     return "Sleep";
    case MACHINE_FAULT:     return "FAULT";
    default:                return "?";
    }
}

/* One boiler block at x-origin @p x0 (0 = brew, 64 = steam): "NN\xB0C" on the
 * left, its two element squares stuck to the block's right edge. */
static void draw_boiler(uint8_t x0, bool sensor_ok, float temp,
                        bool lo_on, bool hi_on)
{
    char buf[6];
    if (sensor_ok) {
        snprintf(buf, sizeof(buf), "%.0f", (double)temp);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    uint8_t x = x0 + 2;
    hal_display_text(x, HDR_TEMP_Y, buf);
    x += (uint8_t)(strlen(buf) * 6);       /* 5px glyph + 1px gap */
    hal_display_rect(x, HDR_TEMP_Y, 3, 3, false); /* degree ring */
    hal_display_text((uint8_t)(x + 4), HDR_TEMP_Y, "C");
    /* Squares hard against the right edge of this boiler's half. */
    const uint8_t sx = (uint8_t)(x0 + CELL_W - SQ_SIZE - SQ_PAD);
    hal_display_rect(sx, SQ_LO_Y, SQ_SIZE, SQ_SIZE, lo_on);
    hal_display_rect(sx, SQ_HI_Y, SQ_SIZE, SQ_SIZE, hi_on);
}

/* Draw @p s horizontally centred on the panel at row @p y. */
static void draw_centered(const char *s, uint8_t y)
{
    const int w = (int)strlen(s) * 6; /* ~6 px per glyph */
    int x = ((int)hal_display_width() - w) / 2;
    if (x < 0) {
        x = 0;
    }
    hal_display_text((uint8_t)x, y, s);
}

/* Apply edited settings to the live controllers and persist them. */
static void commit_settings(app_state_t *app, ui_t *ui)
{
    boiler_set_setpoint(&app->brew_boiler, app->settings.brew_setpoint);
    boiler_set_setpoint(&app->steam_boiler, app->settings.steam_setpoint);
    app_apply_brew_profile(app);
    safety_init(&app->safety, &app->settings.safety);
    if (ui_take_dirty(ui)) {
        hal_storage_save("settings", &app->settings, sizeof(app->settings));
    }
}

static void render(app_state_t *app, const ui_t *ui)
{
    /* Snapshot shared state under the lock, then draw without holding it. */
    hal_temp_reading_t bt, st;
    machine_state_t state;
    uint32_t shot_ms;
    bool cooling;
    uint32_t cooldown_ms;
    bool heater[LOAD_HEATER_COUNT];
    ui_screen_t screen = ui_screen(ui);
    ui_item_t item = ui_item(ui);

    xSemaphoreTake(app->lock, portMAX_DELAY);
    bt = app->brew_temp;
    st = app->steam_temp;
    state = machine_state(&app->machine);
    shot_ms = app->shot_elapsed_ms;
    cooling = app->pump_cooling;
    cooldown_ms = app->pump_cooldown_ms;
    for (int i = 0; i < LOAD_HEATER_COUNT; i++) {
        heater[i] = app->heater_active[i];
    }
    xSemaphoreGive(app->lock);

    hal_display_clear();

    if (screen == UI_STATUS) {
        /* Yellow strip: a framed temperature bar split into two boiler cells. */
        hal_display_rect(0, 0, hal_display_width(), HDR_H, false);
        hal_display_rect(CELL_W - 1, 0, 1, HDR_H, true);
        draw_boiler(0, bt.ok, bt.celsius,
                    heater[LOAD_BREW_LO], heater[LOAD_BREW_HI]);
        draw_boiler(CELL_W, st.ok, st.celsius,
                    heater[LOAD_STEAM_LO], heater[LOAD_STEAM_HI]);

        /* Blue area: the machine's main status (shot timer / pump-cooldown when
         * relevant), centred. */
        char prim[24];
        char sec[16];
        sec[0] = '\0';
        if (state == MACHINE_BREWING) {
            snprintf(prim, sizeof(prim), "Brewing");
            snprintf(sec, sizeof(sec), "%.1f s", (double)shot_ms / 1000.0);
        } else if (cooling) {
            /* Pump is resting on its duty cycle; round up so it never shows 0
             * while still locked. */
            snprintf(prim, sizeof(prim), "Pump Cooling");
            snprintf(sec, sizeof(sec), "%lu s",
                     (unsigned long)((cooldown_ms + 999) / 1000));
        } else {
            snprintf(prim, sizeof(prim), "%s", status_text(state));
        }
        draw_centered(prim, sec[0] != '\0' ? MAIN_Y1 : 34);
        if (sec[0] != '\0') {
            draw_centered(sec, MAIN_Y2);
        }
    } else {
        char value[24];
        ui_item_value(ui, item, value, sizeof(value));
        hal_display_printf(0, 0, "%s", screen == UI_EDIT ? "EDIT" : "CONFIG");
        hal_display_printf(0, 20, "%s", ui_item_label(item));
        hal_display_printf(0, 36, "%s", value);
    }

    hal_display_flush();
}

void ui_task(void *arg)
{
    (void)arg;
    app_state_t *app = &g_app;

    buttons_t buttons;
    const button_config_t bcfg = { .hold_ms = 800, .repeat_ms = 150 };
    buttons_init(&buttons, &bcfg);

    ui_t ui;
    ui_init(&ui, &app->settings);

    TickType_t last = xTaskGetTickCount();
    bool prev_brew_sw = false, prev_steam_sw = false;

    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(UI_PERIOD_MS));
        const esp_ms_t now = hal_time_ms();

        /* Machine control switches -> machine events on edges. */
        const bool bsw = hal_switch_brew();
        const bool ssw = hal_switch_steam();
        if (bsw && !prev_brew_sw)  app_post_event(EV_BREW_LEVER_ON);
        if (!bsw && prev_brew_sw)  app_post_event(EV_BREW_LEVER_OFF);
        if (ssw && !prev_steam_sw) app_post_event(EV_STEAM_ON);
        if (!ssw && prev_steam_sw) app_post_event(EV_STEAM_OFF);
        prev_brew_sw = bsw;
        prev_steam_sw = ssw;

        /* UI buttons -> gestures -> menu. */
        const hal_buttons_t btn = hal_buttons_read();
        const button_event_t be = buttons_update(&buttons, btn.a, btn.b, now);
        if (be != BTN_NONE) {
            xSemaphoreTake(app->lock, portMAX_DELAY);
            const ui_screen_t before = ui_screen(&ui);
            ui_handle(&ui, be);
            const bool left_config = before != UI_STATUS && ui_screen(&ui) == UI_STATUS;
            if (left_config) {
                commit_settings(app, &ui);
            }
            xSemaphoreGive(app->lock);
        }

        render(app, &ui);
    }
}
