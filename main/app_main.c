/**
 * @file app_main.c
 * @brief Firmware entry point: bring up hardware, load settings, wire the
 *        portable core to the HAL, and start the FreeRTOS tasks.
 */
#include "app.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "hal/hal.h"
#include "hal/hal_storage.h"

static const char *TAG = "app";

app_state_t g_app;

void app_post_event(machine_event_t ev)
{
    if (g_app.events != NULL) {
        (void)xQueueSend(g_app.events, &ev, 0);
    }
}

void app_get_telemetry(app_telemetry_t *out)
{
    xSemaphoreTake(g_app.lock, portMAX_DELAY);
    out->state = machine_state(&g_app.machine);
    out->safety_trip = safety_trip(&g_app.safety);
    out->brew_temp = g_app.brew_temp.celsius;
    out->steam_temp = g_app.steam_temp.celsius;
    out->brew_setpoint = g_app.settings.brew_setpoint;
    out->steam_setpoint = g_app.settings.steam_setpoint;
    out->brew_sensor_ok = g_app.brew_temp.ok;
    out->steam_sensor_ok = g_app.steam_temp.ok;
    out->brew_duty = g_app.brew_duty;
    out->steam_duty = g_app.steam_duty;
    out->both_ready = g_app.both_ready;
    out->shot_volume_ml = g_app.shot_volume_ml;
    out->shot_elapsed_ms = g_app.shot_elapsed_ms;
    xSemaphoreGive(g_app.lock);
}

/* Load persisted settings, falling back to validated defaults. */
static void load_settings(settings_t *s)
{
    settings_load_defaults(s);

    settings_t stored;
    if (hal_storage_load("settings", &stored, sizeof(stored)) == ESPRESSO_OK &&
        settings_validate(&stored) == ESPRESSO_OK) {
        *s = stored;
        ESP_LOGI(TAG, "loaded settings from NVS");
    } else {
        ESP_LOGW(TAG, "using default settings");
    }
}

void app_apply_brew_profile(app_state_t *app)
{
    brew_profile_t profile;
    brew_profile_build(&profile, app->settings.active_profile, &app->settings.brew);
    brew_init(&app->brew, &profile);
}

static void init_controllers(app_state_t *app)
{
    const settings_t *s = &app->settings;
    boiler_init(&app->brew_boiler, s->brew_setpoint, &s->brew_pid, 1.0f);
    boiler_init(&app->steam_boiler, s->steam_setpoint, &s->steam_pid, 2.0f);
    app_apply_brew_profile(app);
    safety_init(&app->safety, &s->safety);
    machine_init(&app->machine);
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP.Resso starting");

    /* NVS must be ready before Wi-Fi or settings storage. */
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(hal_init() == ESPRESSO_OK ? ESP_OK : ESP_FAIL);

    g_app.lock = xSemaphoreCreateMutex();
    g_app.events = xQueueCreate(16, sizeof(machine_event_t));
    configASSERT(g_app.lock && g_app.events);

    load_settings(&g_app.settings);
    init_controllers(&g_app);

    /* Priorities: safety highest, then control, then UI/net.
     * Control + safety are pinned to core 1 to keep timing away from the
     * Wi-Fi/IDF housekeeping that tends to live on core 0. */
    xTaskCreatePinnedToCore(safety_task,  "safety",  3072, NULL, 6, NULL, 1);
    xTaskCreatePinnedToCore(control_task, "control", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(ui_task,      "ui",      4096, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(net_task,     "net",     4096, NULL, 2, NULL, 0);

    app_post_event(EV_WAKE);
}
