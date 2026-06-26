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

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "core/buttons.h"
#include "core/ui.h"

#include "hal/hal_display.h"
#include "hal/hal_input.h"
#include "hal/hal_storage.h"
#include "hal/hal_time.h"

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
    temp_c_t bsp, ssp;
    machine_state_t state;
    uint32_t shot_ms;
    ui_screen_t screen = ui_screen(ui);
    ui_item_t item = ui_item(ui);

    xSemaphoreTake(app->lock, portMAX_DELAY);
    bt = app->brew_temp;
    st = app->steam_temp;
    bsp = app->settings.brew_setpoint;
    ssp = app->settings.steam_setpoint;
    state = machine_state(&app->machine);
    shot_ms = app->shot_elapsed_ms;
    xSemaphoreGive(app->lock);

    hal_display_clear();

    if (screen == UI_STATUS) {
        hal_display_printf(0, 0, "%s", machine_state_name(state));
        hal_display_printf(0, 16, "Brew  %4.1f/%2.0f", (double)bt.celsius, (double)bsp);
        hal_display_printf(0, 28, "Steam %4.1f/%3.0f", (double)st.celsius, (double)ssp);
        if (state == MACHINE_BREWING) {
            hal_display_printf(0, 44, "Shot %5.1fs", (double)shot_ms / 1000.0);
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
