/**
 * @file net_task.c
 * @brief Wi-Fi station + HTTP dashboard for live statistics.
 *
 * When enabled in menuconfig (ESP.Resso -> "Enable Wi-Fi dashboard"), the
 * ESP32 joins your network and serves:
 *   - "/"               a small self-contained HTML dashboard, and
 *   - "/api/telemetry"  a JSON snapshot the page polls once per second.
 *
 * The dashboard reads ::app_get_telemetry(), so it never touches the control
 * loop's internals directly. This is intentionally a clean extension point:
 * add MQTT publishing, a shot-history log, or OTA here without disturbing
 * control or safety. Disabled builds keep this task as a harmless idle stub.
 */
#include "app.h"
#include "sdkconfig.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "net";

#if CONFIG_ESPRESSO_WIFI_ENABLE

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_wifi.h"

/* Self-contained dashboard page (no external assets; single-quoted attributes
 * so it embeds cleanly as a C string). Polls /api/telemetry every second. */
static const char DASHBOARD_HTML[] =
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ESP.Resso</title><style>"
    "body{font-family:system-ui,sans-serif;background:#15110e;color:#eee;margin:0;padding:1.2rem}"
    "h1{font-size:1.3rem;margin:0 0 1rem}.card{background:#241c16;border-radius:10px;padding:1rem;margin:.6rem 0}"
    ".t{font-size:2.2rem;font-weight:600}.sp{color:#caa;font-size:1rem}"
    ".bar{height:8px;background:#3a2e24;border-radius:4px;overflow:hidden;margin-top:.5rem}"
    ".bar>i{display:block;height:100%;background:#e0892b}.k{color:#9a8}.row{display:flex;justify-content:space-between}"
    "#state{font-weight:600;color:#e0892b}</style></head><body>"
    "<h1>ESP.Resso &middot; <span id='state'>...</span></h1>"
    "<div class='card'><div class='row'><span class='k'>Brew boiler</span><span id='bok'></span></div>"
    "<div class='t'><span id='bt'>--</span>&deg;C <span class='sp'>/ <span id='bsp'>--</span></span></div>"
    "<div class='bar'><i id='bd' style='width:0'></i></div></div>"
    "<div class='card'><div class='row'><span class='k'>Steam boiler</span><span id='sok'></span></div>"
    "<div class='t'><span id='st'>--</span>&deg;C <span class='sp'>/ <span id='ssp'>--</span></span></div>"
    "<div class='bar'><i id='sd' style='width:0'></i></div></div>"
    "<div class='card'><div class='row'><span class='k'>Shot</span><span id='shot'>--</span></div></div>"
    "<script>"
    "function g(id){return document.getElementById(id)}"
    "async function tick(){try{const d=await(await fetch('/api/telemetry')).json();"
    "g('state').textContent=d.safety==='OK'?d.state:('FAULT: '+d.safety);"
    "g('bt').textContent=d.brew.t.toFixed(1);g('bsp').textContent=d.brew.sp.toFixed(1);"
    "g('bd').style.width=(d.brew.duty*100)+'%';g('bok').textContent=d.brew.ok?'':'SENSOR FAULT';"
    "g('st').textContent=d.steam.t.toFixed(1);g('ssp').textContent=d.steam.sp.toFixed(1);"
    "g('sd').style.width=(d.steam.duty*100)+'%';g('sok').textContent=d.steam.ok?'':'SENSOR FAULT';"
    "g('shot').textContent=(d.shot.ms/1000).toFixed(1)+'s / '+d.shot.ml.toFixed(0)+'ml';"
    "}catch(e){g('state').textContent='offline'}}"
    "setInterval(tick,1000);tick();"
    "</script></body></html>";

static httpd_handle_t s_server;

static esp_err_t root_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, DASHBOARD_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t telemetry_get(httpd_req_t *req)
{
    app_telemetry_t t;
    app_get_telemetry(&t);

    char buf[384];
    const int n = snprintf(
        buf, sizeof(buf),
        "{\"state\":\"%s\",\"safety\":\"%s\",\"ready\":%s,"
        "\"brew\":{\"t\":%.1f,\"sp\":%.1f,\"duty\":%.2f,\"ok\":%s},"
        "\"steam\":{\"t\":%.1f,\"sp\":%.1f,\"duty\":%.2f,\"ok\":%s},"
        "\"shot\":{\"ml\":%.1f,\"ms\":%lu}}",
        machine_state_name(t.state), safety_trip_name(t.safety_trip),
        t.both_ready ? "true" : "false",
        (double)t.brew_temp, (double)t.brew_setpoint, (double)t.brew_duty,
        t.brew_sensor_ok ? "true" : "false",
        (double)t.steam_temp, (double)t.steam_setpoint, (double)t.steam_duty,
        t.steam_sensor_ok ? "true" : "false",
        (double)t.shot_volume_ml, (unsigned long)t.shot_elapsed_ms);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}

static void start_dashboard(void)
{
    if (s_server != NULL) {
        return;
    }
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = CONFIG_ESPRESSO_DASHBOARD_PORT;
    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "failed to start HTTP server");
        return;
    }
    const httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get };
    const httpd_uri_t tele = { .uri = "/api/telemetry", .method = HTTP_GET,
                               .handler = telemetry_get };
    httpd_register_uri_handler(s_server, &root);
    httpd_register_uri_handler(s_server, &tele);
    ESP_LOGI(TAG, "dashboard listening on port %d", cfg.server_port);
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "disconnected, reconnecting");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = (const ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got IP " IPSTR, IP2STR(&e->ip_info.ip));
        start_dashboard();
    }
}

static void wifi_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        wifi_event, NULL, NULL));

    wifi_config_t wc = { 0 };
    strncpy((char *)wc.sta.ssid, CONFIG_ESPRESSO_WIFI_SSID, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, CONFIG_ESPRESSO_WIFI_PASSWORD,
            sizeof(wc.sta.password) - 1);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void net_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "starting Wi-Fi dashboard");
    wifi_start();
    for (;;) {
        /* Connection + server are event-driven; nothing to poll here yet.
         * TODO: periodic MQTT publish / shot-history logging. */
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

#else /* !CONFIG_ESPRESSO_WIFI_ENABLE */

void net_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Wi-Fi dashboard disabled (enable it in menuconfig)");
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

#endif
