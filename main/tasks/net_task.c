/**
 * @file net_task.c
 * @brief Wi-Fi (station + SoftAP provisioning) and the HTTP dashboard.
 *
 * When enabled in menuconfig (ESP.Resso -> "Enable Wi-Fi dashboard"), the
 * device gets onto your network by one of, in precedence order:
 *
 *   1. a compiled-in SSID/password (ESPRESSO_WIFI_SSID, if set), else
 *   2. credentials saved in NVS by the setup portal, else
 *   3. it hosts a SoftAP "setup portal" so you can enter them from a phone.
 *
 * If the very first connection attempt fails (no portal creds yet, wrong
 * password, network gone), it also falls back to the setup portal. Once it has
 * been online, it only ever reconnects — a flaky router never kicks the machine
 * back into setup mode.
 *
 * As a station it serves:
 *   - "/"               a small self-contained HTML dashboard,
 *   - "/api/telemetry"  a JSON snapshot the page polls once per second, and
 *   - "/api/forget"     clears saved Wi-Fi and reboots into the setup portal.
 *
 * As the setup portal (SoftAP) it serves:
 *   - "/"               a setup page that scans and lists nearby networks,
 *   - "/api/scan"       JSON list of visible APs, and
 *   - "/api/provision"  saves posted credentials to NVS and reboots.
 *
 * The dashboard reads ::app_get_telemetry(), so it never touches the control
 * loop's internals directly — this stays a clean extension point (MQTT, shot
 * history, OTA) that can never disturb control or safety. Disabled builds keep
 * this task as a harmless idle stub.
 */
#include "app.h"
#include "sdkconfig.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "net";

#if CONFIG_ESPRESSO_WIFI_ENABLE

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/task.h"

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "lwip/sockets.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"

#include "hal/hal_storage.h"

#define WIFI_NVS_KEY   "wifi"   /* hal_storage blob key for saved credentials. */
#define AUTH_NVS_KEY   "auth"   /* hal_storage blob key for web credentials.    */
#define STA_MAX_RETRY  10       /* Initial-connect attempts before the portal.  */
#define SCAN_MAX_APS   16       /* Networks reported by /api/scan.               */
#define PORTAL_PORT    80       /* Captive detection only probes port 80.        */
#define CAPTIVE_IP     "192.168.4.1" /* Default SoftAP gateway address.          */

/* favicon.ico, embedded into the binary via EMBED_FILES in main/CMakeLists.txt. */
extern const uint8_t favicon_start[] asm("_binary_favicon_ico_start");
extern const uint8_t favicon_end[]   asm("_binary_favicon_ico_end");

/* Persisted (and posted) credentials. Fixed size so the NVS blob length is
 * stable — hal_storage_load requires an exact size match. */
typedef struct {
    char ssid[33]; /* 32 + NUL */
    char pass[65]; /* 64 + NUL */
} wifi_creds_t;

/* Web-interface access roles: user = read-only, admin = full control. */
typedef enum { ROLE_NONE = 0, ROLE_USER, ROLE_ADMIN } auth_role_t;

/* Stored web credentials: salted SHA-256 of each password (set on the portal). */
typedef struct {
    uint8_t user_hash[32];
    uint8_t admin_hash[32];
    uint8_t set; /* 1 once configured */
} auth_creds_t;

/* ------------------------------------------------------------------------- */
/* Pages (self-contained: no external assets, single-quoted attributes).     */
/* ------------------------------------------------------------------------- */

static const char DASHBOARD_HTML[] =
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ESP.Resso</title><link rel='icon' href='/favicon.ico'><style>"
    "body{font-family:system-ui,sans-serif;background:#15110e;color:#eee;margin:0;padding:1.2rem;max-width:30rem}"
    "h1{font-size:1.3rem;margin:0 0 1rem}"
    ".card{background:#241c16;border-radius:10px;padding:.7rem 1rem;margin:.6rem 0}"
    ".hd{color:#e0892b;font-weight:600;margin-bottom:.3rem}"
    ".row{display:flex;justify-content:space-between;align-items:baseline;padding:.18rem 0}"
    ".k{color:#9a8}.st{font-weight:600}"
    ".ok{color:#7cd97c}.warn{color:#e0b02b}.err{color:#e0552b}"
    "#state{font-weight:600;color:#e0892b}"
    "button{background:#3a2e24;color:#caa;border:0;border-radius:8px;padding:.5rem .8rem;font-size:.85rem}"
    "</style></head><body>"
    "<h1>ESP.Resso &middot; <span id='state'>...</span></h1>"
    "<div class='card'>"
    "<div class='row'><span class='k'>Display</span><span id='disp' class='st'>--</span></div>"
    "<div class='row'><span class='k'>Buttons</span><span id='btns' class='st'>--</span></div>"
    "<div class='row'><span class='k'>Reservoir</span><span id='res' class='st'>--</span></div>"
    "</div>"
    "<div class='card'><div class='hd'>Brew boiler</div>"
    "<div class='row'><span class='k'>Temp sensor</span><span id='bt' class='st'>--</span></div>"
    "<div class='row'><span class='k'>Water level</span><span id='bl' class='st'>--</span></div>"
    "</div>"
    "<div class='card'><div class='hd'>Steam boiler</div>"
    "<div class='row'><span class='k'>Temp sensor</span><span id='stt' class='st'>--</span></div>"
    "<div class='row'><span class='k'>Water level</span><span id='sl' class='st'>--</span></div>"
    "</div>"
    "<div class='card'><div class='row'><span class='k'>Shot</span><span id='shot' class='st'>--</span></div></div>"
    "<div class='card' id='admin' style='display:none'><button onclick='forget()'>Reconfigure Wi-Fi</button></div>"
    "<script>"
    "function g(id){return document.getElementById(id)}"
    "var LVL=['Full','Filling','Low','Error'],LC=['ok','warn','warn','err'];"
    "function setOk(el,b){el.textContent=b?'OK':'FAULT';el.className='st '+(b?'ok':'err')}"
    "function faultText(f){if(f===255)return 'no SPI comms';if(f&192)return 'out of range';"
    "if(f&56)return 'open circuit';if(f&4)return 'voltage fault';return 'fault 0x'+f.toString(16)}"
    "function setTemp(el,o){if(o.ok){el.textContent=o.t.toFixed(1)+'\\u00B0C  OK';el.className='st ok'}"
    "else{var d=faultText(o.fault);el.textContent='FAIL'+(d?' \\u2014 '+d:'');el.className='st err'}}"
    "function setLevel(el,v){el.textContent=LVL[v]||'?';el.className='st '+(LC[v]||'warn')}"
    "async function tick(){try{const d=await(await fetch('/api/telemetry')).json();"
    "g('state').textContent=d.safety==='OK'?d.state:('FAULT: '+d.safety);"
    "setOk(g('disp'),d.display);setOk(g('btns'),d.buttons);setOk(g('res'),d.reservoir);"
    "setTemp(g('bt'),d.brew);setLevel(g('bl'),d.brew.level);"
    "setTemp(g('stt'),d.steam);setLevel(g('sl'),d.steam.level);"
    "g('shot').className='st';g('shot').textContent=(d.shot.ms/1000).toFixed(1)+'s / '+d.shot.ml.toFixed(0)+'ml';"
    "g('admin').style.display=d.role==='admin'?'':'none';"
    "}catch(e){g('state').textContent='offline'}}"
    "async function forget(){if(!confirm('Forget Wi-Fi and reboot into setup mode?'))return;"
    "await fetch('/api/forget',{method:'POST'});"
    "document.body.innerHTML='<h1>Rebooting into setup mode...</h1>"
    "<p>Reconnect to the <b>" CONFIG_ESPRESSO_AP_SSID "</b> network to set up Wi-Fi.</p>'}"
    "setInterval(tick,1000);tick();"
    "</script></body></html>";

static const char SETUP_HTML[] =
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ESP.Resso setup</title><link rel='icon' href='/favicon.ico'><style>"
    "body{font-family:system-ui,sans-serif;background:#15110e;color:#eee;margin:0;padding:1.2rem;max-width:28rem}"
    "h1{font-size:1.3rem}label{display:block;margin:.8rem 0 .2rem;color:#9a8}"
    "input,select,button{width:100%;box-sizing:border-box;padding:.6rem;border-radius:8px;border:1px solid #3a2e24;"
    "background:#241c16;color:#eee;font-size:1rem}"
    "button{background:#e0892b;color:#15110e;font-weight:600;border:0;margin-top:1rem}"
    "#msg{margin-top:1rem;color:#caa}.r{display:flex;gap:.5rem}.r button{width:auto}"
    "</style></head><body>"
    "<h1>ESP.Resso &middot; Wi-Fi setup</h1>"
    "<div class='r'><select id='nets'><option value=''>-- scan for networks --</option></select>"
    "<button type='button' onclick='scan()'>&#x21bb;</button></div>"
    "<label>Network (SSID)</label><input id='ssid' autocapitalize='off' autocorrect='off'>"
    "<label>Wi-Fi password</label><input id='pass' type='password'>"
    "<label>User password (dashboard, view only)</label><input id='upw' type='password'>"
    "<label>Admin password (full control)</label><input id='apw' type='password'>"
    "<button onclick='save()'>Save &amp; connect</button>"
    "<div id='msg'></div>"
    "<script>"
    "function g(id){return document.getElementById(id)}"
    "g('nets').onchange=function(){if(this.value)g('ssid').value=this.value};"
    "async function scan(){g('msg').textContent='Scanning...';try{"
    "const a=await(await fetch('/api/scan')).json();const s=g('nets');"
    "s.length=1;a.forEach(function(n){const o=document.createElement('option');"
    "o.value=n.ssid;o.textContent=n.ssid+(n.lock?' \\uD83D\\uDD12':'')+' ('+n.rssi+')';s.appendChild(o)});"
    "g('msg').textContent=a.length?'':'No networks found.'}"
    "catch(e){g('msg').textContent='Scan failed.'}}"
    "async function save(){const ssid=g('ssid').value;if(!ssid){g('msg').textContent='Enter a network name.';return}"
    "const up=g('upw').value,ap=g('apw').value;"
    "if(!up||!ap){g('msg').textContent='Set both the user and admin passwords.';return}"
    "g('msg').textContent='Saving...';"
    "const body='ssid='+encodeURIComponent(ssid)+'&pass='+encodeURIComponent(g('pass').value)"
    "+'&userpw='+encodeURIComponent(up)+'&adminpw='+encodeURIComponent(ap);"
    "try{const r=await fetch('/api/provision',{method:'POST',"
    "headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body});"
    "g('msg').textContent=r.ok?('Saved. The machine is rebooting to join '+ssid+'.'):'Save failed.';}"
    "catch(e){g('msg').textContent='Saved. The machine is rebooting.';}}"
    "scan();"
    "</script></body></html>";

/* ------------------------------------------------------------------------- */
/* State                                                                     */
/* ------------------------------------------------------------------------- */

typedef enum { NET_CONNECTING, NET_PORTAL } net_state_t;

static httpd_handle_t s_server;
static esp_netif_t   *s_sta_netif;     /* Kept so we can set a static IP.     */
static esp_netif_t   *s_ap_netif;      /* Kept for the captive DNS offer.     */
static net_state_t    s_state;
static int            s_retry;
static bool           s_been_online;   /* Got an IP at least once.            */
static bool           s_hardcoded;     /* SSID came from menuconfig.          */
static bool           s_dns_started;   /* Captive DNS task running.           */
static volatile bool  s_req_dashboard; /* Event -> net_task: start dashboard. */
static volatile bool  s_req_portal;    /* Event -> net_task: enter portal.    */

/* ------------------------------------------------------------------------- */
/* Credential storage                                                        */
/* ------------------------------------------------------------------------- */

static bool creds_load(wifi_creds_t *c)
{
    return hal_storage_load(WIFI_NVS_KEY, c, sizeof(*c)) == ESPRESSO_OK &&
           c->ssid[0] != '\0';
}

static void creds_save(const wifi_creds_t *c)
{
    if (hal_storage_save(WIFI_NVS_KEY, c, sizeof(*c)) != ESPRESSO_OK) {
        ESP_LOGE(TAG, "failed to persist Wi-Fi credentials");
    }
}

static void creds_clear(void)
{
    wifi_creds_t zero = { 0 };
    creds_save(&zero);
}

/* Compiled-in credentials, if a real SSID is configured. The historical
 * placeholder is treated as "unset" so a stale sdkconfig doesn't trap setup. */
static bool creds_from_config(wifi_creds_t *c)
{
    const char *ssid = CONFIG_ESPRESSO_WIFI_SSID;
    if (ssid[0] == '\0' || strcmp(ssid, "your-ssid") == 0) {
        return false;
    }
    memset(c, 0, sizeof(*c));
    snprintf(c->ssid, sizeof(c->ssid), "%s", ssid);
    snprintf(c->pass, sizeof(c->pass), "%s", CONFIG_ESPRESSO_WIFI_PASSWORD);
    return true;
}

static bool auth_load(auth_creds_t *a)
{
    return hal_storage_load(AUTH_NVS_KEY, a, sizeof(*a)) == ESPRESSO_OK && a->set;
}

static void auth_save(const auth_creds_t *a)
{
    if (hal_storage_save(AUTH_NVS_KEY, a, sizeof(*a)) != ESPRESSO_OK) {
        ESP_LOGE(TAG, "failed to persist web credentials");
    }
}

/* Salted SHA-256 of a password. Not bcrypt, but adequate for a LAN appliance:
 * Basic auth sends the password each request, so we hash and compare. */
static void hash_password(const char *pw, uint8_t out[32])
{
    char buf[160];
    snprintf(buf, sizeof(buf), "esp-resso-v1:%s", pw);
    mbedtls_sha256((const unsigned char *)buf, strlen(buf), out, 0);
}

/* ------------------------------------------------------------------------- */
/* Small string helpers                                                      */
/* ------------------------------------------------------------------------- */

/* Decode application/x-www-form-urlencoded text in place ('+' and %XX). */
static void urldecode(char *s)
{
    char *d = s;
    for (; *s; s++) {
        if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            char hex[3] = { s[1], s[2], 0 };
            *d++ = (char)strtol(hex, NULL, 16);
            s += 2;
        } else if (*s == '+') {
            *d++ = ' ';
        } else {
            *d++ = *s;
        }
    }
    *d = '\0';
}

/* Append @p in as a JSON string body (without quotes) into @p out, escaping
 * the characters that would break JSON. Returns bytes written. */
static int json_escape(char *out, size_t cap, const uint8_t *in, size_t len)
{
    size_t o = 0;
    for (size_t i = 0; i < len && in[i] && o + 2 < cap; i++) {
        const uint8_t c = in[i];
        if (c == '"' || c == '\\') {
            out[o++] = '\\';
            out[o++] = (char)c;
        } else if (c >= 0x20) {
            out[o++] = (char)c;
        } /* drop control chars */
    }
    out[o] = '\0';
    return (int)o;
}

/* ------------------------------------------------------------------------- */
/* Authentication (HTTP Basic; user = read-only, admin = control)            */
/* ------------------------------------------------------------------------- */

static esp_err_t deny(httpd_req_t *req)
{
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"ESP.Resso\"");
    return httpd_resp_send(req, "Authentication required", HTTPD_RESP_USE_STRLEN);
}

/* Resolve the caller's role from the Basic Authorization header. If no
 * credentials are configured yet (e.g. the device was provisioned before this
 * feature), access is open so it is never locked out — set passwords via the
 * setup portal to enable enforcement. */
static auth_role_t check_auth(httpd_req_t *req)
{
    auth_creds_t a;
    if (!auth_load(&a)) {
        return ROLE_ADMIN; /* not configured -> open */
    }

    char hdr[160];
    if (httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof(hdr)) != ESP_OK ||
        strncmp(hdr, "Basic ", 6) != 0) {
        return ROLE_NONE;
    }

    unsigned char dec[160];
    size_t dlen = 0;
    if (mbedtls_base64_decode(dec, sizeof(dec) - 1, &dlen,
                              (const unsigned char *)hdr + 6, strlen(hdr + 6)) != 0) {
        return ROLE_NONE;
    }
    dec[dlen] = '\0';
    char *colon = strchr((char *)dec, ':');
    if (colon == NULL) {
        return ROLE_NONE;
    }
    *colon = '\0';
    const char *user = (const char *)dec;
    const char *pass = colon + 1;

    uint8_t h[32];
    hash_password(pass, h);
    if (strcmp(user, "admin") == 0 && memcmp(h, a.admin_hash, sizeof(h)) == 0) {
        return ROLE_ADMIN;
    }
    if (strcmp(user, "user") == 0 && memcmp(h, a.user_hash, sizeof(h)) == 0) {
        return ROLE_USER;
    }
    return ROLE_NONE;
}

/* ------------------------------------------------------------------------- */
/* HTTP handlers — dashboard (station mode)                                  */
/* ------------------------------------------------------------------------- */

static esp_err_t root_get(httpd_req_t *req)
{
    if (check_auth(req) < ROLE_USER) {
        return deny(req);
    }
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, DASHBOARD_HTML, HTTPD_RESP_USE_STRLEN);
}

/* Embedded favicon, served in both station and portal modes. */
static esp_err_t favicon_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "image/x-icon");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
    return httpd_resp_send(req, (const char *)favicon_start,
                           favicon_end - favicon_start);
}

static esp_err_t telemetry_get(httpd_req_t *req)
{
    const auth_role_t role = check_auth(req);
    if (role < ROLE_USER) {
        return deny(req);
    }

    app_telemetry_t t;
    app_get_telemetry(&t);

    char buf[512];
    const int n = snprintf(
        buf, sizeof(buf),
        "{\"state\":\"%s\",\"safety\":\"%s\",\"ready\":%s,\"role\":\"%s\","
        "\"display\":%s,\"buttons\":%s,\"reservoir\":%s,"
        "\"brew\":{\"t\":%.1f,\"sp\":%.1f,\"ok\":%s,\"fault\":%u,\"level\":%d},"
        "\"steam\":{\"t\":%.1f,\"sp\":%.1f,\"ok\":%s,\"fault\":%u,\"level\":%d},"
        "\"shot\":{\"ml\":%.1f,\"ms\":%lu}}",
        machine_state_name(t.state), safety_trip_name(t.safety_trip),
        t.both_ready ? "true" : "false",
        role == ROLE_ADMIN ? "admin" : "user",
        t.display_ok ? "true" : "false", t.buttons_ok ? "true" : "false",
        t.reservoir_ok ? "true" : "false",
        (double)t.brew_temp, (double)t.brew_setpoint,
        t.brew_sensor_ok ? "true" : "false",
        (unsigned)t.brew_temp_fault, (int)t.brew_level,
        (double)t.steam_temp, (double)t.steam_setpoint,
        t.steam_sensor_ok ? "true" : "false",
        (unsigned)t.steam_temp_fault, (int)t.steam_level,
        (double)t.shot_volume_ml, (unsigned long)t.shot_elapsed_ms);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}

static esp_err_t forget_post(httpd_req_t *req)
{
    if (check_auth(req) < ROLE_ADMIN) {
        return deny(req);
    }
    creds_clear();
    httpd_resp_sendstr(req, "ok");
    ESP_LOGW(TAG, "Wi-Fi credentials cleared; rebooting into setup portal");
    vTaskDelay(pdMS_TO_TICKS(1500)); /* let the response flush */
    esp_restart();
    return ESP_OK;
}

/* ------------------------------------------------------------------------- */
/* HTTP handlers — setup portal (SoftAP mode)                                */
/* ------------------------------------------------------------------------- */

static esp_err_t setup_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, SETUP_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t scan_get(httpd_req_t *req)
{
    wifi_scan_config_t sc = { .show_hidden = false };
    if (esp_wifi_scan_start(&sc, true) != ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "[]");
    }
    uint16_t num = SCAN_MAX_APS;
    wifi_ap_record_t *recs = calloc(num, sizeof(*recs));
    if (recs == NULL) {
        return httpd_resp_send_500(req);
    }
    esp_wifi_scan_get_ap_records(&num, recs);

    /* Build a JSON array of the scan results. */
    char *json = malloc(1536);
    if (json == NULL) {
        free(recs);
        return httpd_resp_send_500(req);
    }
    size_t o = 0;
    json[o++] = '[';
    for (uint16_t i = 0; i < num; i++) {
        char esc[33 * 2];
        json_escape(esc, sizeof(esc), recs[i].ssid, sizeof(recs[i].ssid));
        if (esc[0] == '\0') {
            continue; /* skip hidden / unnamed */
        }
        o += snprintf(json + o, 1536 - o, "%s{\"ssid\":\"%s\",\"rssi\":%d,\"lock\":%s}",
                      o > 1 ? "," : "", esc, recs[i].rssi,
                      recs[i].authmode == WIFI_AUTH_OPEN ? "false" : "true");
        if (o > 1400) {
            break;
        }
    }
    json[o++] = ']';
    json[o] = '\0';

    httpd_resp_set_type(req, "application/json");
    const esp_err_t r = httpd_resp_send(req, json, o);
    free(json);
    free(recs);
    return r;
}

static esp_err_t provision_post(httpd_req_t *req)
{
    /* Holds ssid + Wi-Fi password + user/admin passwords, percent-encoded
     * (which can triple each character); decode into roomy temporaries. */
    char body[1024];
    int len = req->content_len < (int)sizeof(body) - 1 ? req->content_len
                                                       : (int)sizeof(body) - 1;
    int got = 0;
    while (got < len) {
        const int r = httpd_req_recv(req, body + got, len - got);
        if (r <= 0) {
            return httpd_resp_send_500(req);
        }
        got += r;
    }
    body[got] = '\0';

    char ssid_enc[128] = { 0 }, pass_enc[256] = { 0 };
    char upw_enc[160] = { 0 }, apw_enc[160] = { 0 };
    httpd_query_key_value(body, "ssid", ssid_enc, sizeof(ssid_enc));
    httpd_query_key_value(body, "pass", pass_enc, sizeof(pass_enc));
    httpd_query_key_value(body, "userpw", upw_enc, sizeof(upw_enc));
    httpd_query_key_value(body, "adminpw", apw_enc, sizeof(apw_enc));
    urldecode(ssid_enc);
    urldecode(pass_enc);
    urldecode(upw_enc);
    urldecode(apw_enc);
    if (ssid_enc[0] == '\0' || upw_enc[0] == '\0' || apw_enc[0] == '\0') {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "ssid and both passwords are required");
    }

    /* Persist hashed web credentials, then the Wi-Fi network. */
    auth_creds_t auth = { 0 };
    hash_password(upw_enc, auth.user_hash);
    hash_password(apw_enc, auth.admin_hash);
    auth.set = 1;
    auth_save(&auth);

    wifi_creds_t c = { 0 };
    snprintf(c.ssid, sizeof(c.ssid), "%s", ssid_enc);
    snprintf(c.pass, sizeof(c.pass), "%s", pass_enc);
    creds_save(&c);
    httpd_resp_sendstr(req, "ok");
    ESP_LOGI(TAG, "provisioned SSID '%s' + web credentials; rebooting", c.ssid);
    vTaskDelay(pdMS_TO_TICKS(1500)); /* let the response flush */
    esp_restart();
    return ESP_OK;
}

/* ------------------------------------------------------------------------- */
/* Captive portal — hijack DNS so any lookup resolves to the SoftAP, and      */
/* redirect unknown URLs to the setup page, so the OS auto-opens it.          */
/* ------------------------------------------------------------------------- */

/* Answer every DNS query with an A record pointing at the SoftAP (192.168.4.1).
 * That is what makes phones/laptops pop the "sign in to network" sheet. */
static void captive_dns_task(void *arg)
{
    (void)arg;
    const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "captive DNS: socket failed");
        vTaskDelete(NULL);
        return;
    }
    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "captive DNS: bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    uint8_t pkt[256];
    for (;;) {
        struct sockaddr_in src;
        socklen_t slen = sizeof(src);
        const int n = recvfrom(sock, pkt, sizeof(pkt), 0, (struct sockaddr *)&src, &slen);
        /* Need a header (12B) + at least one question, and it must be a query. */
        if (n < 13 || (pkt[2] & 0x80)) {
            continue;
        }
        /* Walk the QNAME labels to find the end of the question section. */
        int q = 12;
        while (q < n && pkt[q] != 0) {
            q += pkt[q] + 1;
        }
        q += 1 + 4; /* zero byte + QTYPE + QCLASS */
        if (q + 16 > (int)sizeof(pkt) || q > n) {
            continue; /* malformed or no room for our answer */
        }

        pkt[2] |= 0x84; /* QR=1 (response), AA=1 */
        pkt[3] = 0x00;  /* RA=0, RCODE=0 */
        pkt[6] = 0x00; pkt[7] = 0x01; /* ANCOUNT = 1 */
        pkt[8] = pkt[9] = pkt[10] = pkt[11] = 0x00; /* NS/AR = 0 */

        uint8_t *a = pkt + q;
        a[0] = 0xC0; a[1] = 0x0C;        /* name: pointer to the question name */
        a[2] = 0x00; a[3] = 0x01;        /* TYPE  = A   */
        a[4] = 0x00; a[5] = 0x01;        /* CLASS = IN  */
        a[6] = a[7] = a[8] = a[9] = 0;   /* TTL   = 0   */
        a[10] = 0x00; a[11] = 0x04;      /* RDLENGTH = 4 */
        a[12] = 192; a[13] = 168; a[14] = 4; a[15] = 1; /* 192.168.4.1 */

        sendto(sock, pkt, q + 16, 0, (struct sockaddr *)&src, slen);
    }
}

static void captive_dns_start(void)
{
    if (s_dns_started) {
        return;
    }
    s_dns_started = true;
    xTaskCreate(captive_dns_task, "captive_dns", 3072, NULL, 4, NULL);
}

/* Redirect any unknown URL to the setup page (the OS's connectivity-check probe
 * lands here and triggers the captive-portal UI). */
static esp_err_t portal_redirect(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://" CAPTIVE_IP "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* Make the SoftAP's DHCP server hand out 192.168.4.1 as the DNS server, so
 * clients send their lookups to our captive DNS (the ESP32 AP doesn't offer DNS
 * by default). Called once the AP is up: stop, set the option, restart. */
static void configure_ap_dns(void)
{
    esp_netif_dns_info_t dns = { 0 };
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    esp_netif_str_to_ip4(CAPTIVE_IP, &dns.ip.u_addr.ip4);

    esp_netif_dhcps_stop(s_ap_netif); /* ignore "already stopped" */
    esp_netif_set_dns_info(s_ap_netif, ESP_NETIF_DNS_MAIN, &dns);
    uint8_t offer_dns = 0x02; /* dhcps OFFER_DNS bit */
    esp_netif_dhcps_option(s_ap_netif, ESP_NETIF_OP_SET,
                           ESP_NETIF_DOMAIN_NAME_SERVER, &offer_dns, sizeof(offer_dns));
    esp_netif_dhcps_start(s_ap_netif);
}

/* ------------------------------------------------------------------------- */
/* Server + Wi-Fi bring-up                                                   */
/* ------------------------------------------------------------------------- */

static void server_start(uint16_t port)
{
    if (s_server != NULL) {
        return;
    }
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = port;
    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "failed to start HTTP server");
    }
}

static void register_uri(const char *uri, httpd_method_t method,
                         esp_err_t (*handler)(httpd_req_t *))
{
    const httpd_uri_t u = { .uri = uri, .method = method, .handler = handler };
    httpd_register_uri_handler(s_server, &u);
}

static void start_dashboard(void)
{
    server_start(CONFIG_ESPRESSO_DASHBOARD_PORT);
    register_uri("/", HTTP_GET, root_get);
    register_uri("/favicon.ico", HTTP_GET, favicon_get);
    register_uri("/api/telemetry", HTTP_GET, telemetry_get);
    register_uri("/api/forget", HTTP_POST, forget_post);
    ESP_LOGI(TAG, "dashboard listening on port %d", CONFIG_ESPRESSO_DASHBOARD_PORT);
}

static void start_portal_server(void)
{
    server_start(PORTAL_PORT); /* captive detection only checks port 80 */
    register_uri("/", HTTP_GET, setup_get);
    register_uri("/favicon.ico", HTTP_GET, favicon_get);
    register_uri("/api/scan", HTTP_GET, scan_get);
    register_uri("/api/provision", HTTP_POST, provision_post);
    /* Anything else (the OS connectivity-check URL) -> redirect to the page. */
    httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, portal_redirect);
    configure_ap_dns(); /* advertise ourselves as DNS so the hijack is used */
    captive_dns_start();
}

/* Switch the radio to AP+STA and bring the setup AP up. Valid whether or not
 * Wi-Fi has been started yet (set_mode/set_config work in both cases). STA is
 * kept enabled so the portal's network scan works. */
static void ap_configure(void)
{
    wifi_config_t ap = { 0 };
    const char *ssid = CONFIG_ESPRESSO_AP_SSID;
    const char *pass = CONFIG_ESPRESSO_AP_PASSWORD;
    snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "%s", ssid);
    ap.ap.ssid_len = strlen(ssid);
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    if (pass[0] != '\0') {
        snprintf((char *)ap.ap.password, sizeof(ap.ap.password), "%s", pass);
        ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        ap.ap.authmode = WIFI_AUTH_OPEN;
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_LOGW(TAG, "setup portal up: join Wi-Fi '%s' then open http://192.168.4.1/", ssid);
}

/* Fall back to the setup portal at runtime (Wi-Fi already started). Runs in
 * net_task context, off the event task. */
static void enter_portal(void)
{
    if (s_state == NET_PORTAL) {
        return;
    }
    s_state = NET_PORTAL;
    esp_wifi_disconnect(); /* abandon the station connect */
    ap_configure();
    start_portal_server();
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (s_state != NET_PORTAL) {
            esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_state == NET_PORTAL) {
            return; /* STA idle in portal mode; ignore */
        }
        if (s_been_online || s_hardcoded) {
            esp_wifi_connect(); /* known-good network: just keep reconnecting */
        } else if (++s_retry <= STA_MAX_RETRY) {
            esp_wifi_connect();
        } else {
            ESP_LOGW(TAG, "no connection after %d tries; opening setup portal", s_retry);
            s_req_portal = true; /* net_task brings up the portal */
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = (const ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got IP " IPSTR, IP2STR(&e->ip_info.ip));
        s_retry = 0;
        s_been_online = true;
        s_req_dashboard = true; /* net_task starts the server */
    }
}

static void start_sta(const wifi_creds_t *c)
{
    wifi_config_t wc = { 0 };
    /* These are fixed-size fields, not NUL-terminated C strings (a 32-char SSID
     * uses all 32 bytes), so copy exactly the used length and leave the
     * zero-initialised tail — snprintf would both warn and clip a full-length
     * SSID by one byte. */
    memcpy(wc.sta.ssid, c->ssid, strnlen(c->ssid, sizeof(wc.sta.ssid)));
    memcpy(wc.sta.password, c->pass, strnlen(c->pass, sizeof(wc.sta.password)));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_LOGI(TAG, "connecting to '%s'", c->ssid);
}

/* Pin the station interface to a fixed address instead of using DHCP. */
static void apply_static_ip(esp_netif_t *sta)
{
#if CONFIG_ESPRESSO_WIFI_STATIC_IP
    esp_netif_dhcpc_stop(sta); /* ignore "already stopped" */

    esp_netif_ip_info_t ip = { 0 };
    esp_netif_str_to_ip4(CONFIG_ESPRESSO_WIFI_IP, &ip.ip);
    esp_netif_str_to_ip4(CONFIG_ESPRESSO_WIFI_GATEWAY, &ip.gw);
    esp_netif_str_to_ip4(CONFIG_ESPRESSO_WIFI_NETMASK, &ip.netmask);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(sta, &ip));

    esp_netif_dns_info_t dns = { 0 };
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    esp_netif_str_to_ip4(CONFIG_ESPRESSO_WIFI_DNS, &dns.ip.u_addr.ip4);
    esp_netif_set_dns_info(sta, ESP_NETIF_DNS_MAIN, &dns);

    ESP_LOGI(TAG, "static IP %s configured", CONFIG_ESPRESSO_WIFI_IP);
#else
    (void)sta; /* DHCP (default) */
#endif
}

static void wifi_preinit(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    apply_static_ip(s_sta_netif);

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        wifi_event, NULL, NULL));
}

void net_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "starting Wi-Fi");

    wifi_creds_t creds;
    bool have = creds_from_config(&creds);
    s_hardcoded = have;
    if (!have) {
        have = creds_load(&creds);
    }

    wifi_preinit();

    if (have) {
        s_state = NET_CONNECTING;
        start_sta(&creds); /* set_mode(STA) + config; STA_START -> connect */
    } else {
        s_state = NET_PORTAL; /* set before start so STA_START won't connect */
        ap_configure();       /* set_mode(APSTA) + AP config */
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    if (!have) {
        start_portal_server();
    }

    for (;;) {
        if (s_req_dashboard) {
            s_req_dashboard = false;
            start_dashboard();
        }
        if (s_req_portal) {
            s_req_portal = false;
            enter_portal();
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void net_request_provisioning(void)
{
    creds_clear();
    esp_restart();
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

void net_request_provisioning(void) { /* no-op when Wi-Fi is disabled */ }

#endif
