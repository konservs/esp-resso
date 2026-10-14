/**
 * @file i2c_bus.c
 * @brief Implementation of the single shared I2C master bus (see i2c_bus.h).
 */
#include "drivers/i2c_bus.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "i2c_bus";

/* Recovery pacing. The first couple of attempts are soft resets, which fix the
 * common case (a slave still holding SDA from an interrupted byte) in about a
 * millisecond. Past that the fault is not a stuck slave and we rebuild the bus
 * outright. The interval doubles while failures keep coming so a bus that is
 * simply unplugged settles into one quiet attempt every few seconds. */
#define RECOVER_MIN_INTERVAL_MS 1000
#define RECOVER_MAX_INTERVAL_MS 8000
#define SOFT_ATTEMPTS           2

static i2c_master_bus_handle_t s_bus;   /* NULL until the first init.        */
static int s_sda = -1, s_scl = -1;      /* Pins the bus was created on.      */
static SemaphoreHandle_t s_lock;        /* Guards recovery vs. registration. */

static const i2c_bus_client_t *s_clients[I2C_BUS_MAX_CLIENTS];
static size_t s_client_count;

static int     s_attempts;              /* recoveries since the last good xfer */
static int     s_interval_ms = RECOVER_MIN_INTERVAL_MS;
static int64_t s_last_recover_us;

/* Probe every 7-bit address and log which devices ACK. Cheap bring-up aid:
 * expect the SSD1306 (0x3C) and the PCF8574 (0x20). Nothing here => a wiring
 * or pull-up problem; the expected address missing => that one chip. */
static void i2c_bus_scan(void)
{
    ESP_LOGI(TAG, "scanning bus on SDA=%d SCL=%d ...", s_sda, s_scl);
    int found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (i2c_master_probe(s_bus, addr, 50) == ESP_OK) {
            ESP_LOGI(TAG, "  device at 0x%02X", addr);
            found++;
        }
    }
    ESP_LOGI(TAG, "scan complete: %d device(s)", found);
}

/* Create the peripheral-side bus on the already-recorded pins. */
static esp_err_t bus_create(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = -1, /* auto-select a free port */
        .sda_io_num = s_sda,
        .scl_io_num = s_scl,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        s_bus = NULL;
    }
    return err;
}

/* Soft step: reset the peripheral state machine and pulse SCL up to nine times
 * so a slave still driving SDA finishes its byte and sees a STOP. */
static bool recover_soft(void)
{
    const esp_err_t err = i2c_master_bus_reset(s_bus);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "soft reset failed: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGW(TAG, "bus soft reset");
    return true;
}

/* Hard step: drop every device, delete the bus, build it again, and let each
 * driver re-attach and re-initialise its chip. Deleting the bus is also what
 * releases the GPIO reservation, so the driver stops warning that pins 21/22
 * are "not usable" on every subsequent pin config. */
static bool recover_hard(void)
{
    if (s_bus != NULL) {
        for (size_t i = 0; i < s_client_count; i++) {
            const esp_err_t err = s_clients[i]->detach();
            if (err != ESP_OK) {
                /* A device left attached makes the delete below fail, so give
                 * up now and keep the bus we have rather than half-dismantling
                 * it. */
                ESP_LOGE(TAG, "%s detach failed: %s", s_clients[i]->name,
                         esp_err_to_name(err));
                return false;
            }
        }

        const esp_err_t err = i2c_del_master_bus(s_bus);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "bus delete failed: %s", esp_err_to_name(err));
            /* The handle is still live; let the clients back on so the next
             * frame has somewhere to talk to. */
            for (size_t i = 0; i < s_client_count; i++) {
                (void)s_clients[i]->attach();
            }
            return false;
        }
        s_bus = NULL;
    }
    /* s_bus is NULL from here: either we just deleted it, or an earlier rebuild
     * failed after the delete and left us with nothing. Both want a fresh bus,
     * so a failed rebuild is retried rather than being the end of the road. */

    esp_err_t err = bus_create();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bus rebuild failed: %s", esp_err_to_name(err));
        return false;
    }

    bool all_ok = true;
    for (size_t i = 0; i < s_client_count; i++) {
        err = s_clients[i]->attach();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "%s re-attach failed: %s", s_clients[i]->name,
                     esp_err_to_name(err));
            all_ok = false;
        }
    }
    ESP_LOGW(TAG, "bus rebuilt (%d client(s), %s)", (int)s_client_count,
             all_ok ? "all back" : "some still down");
    return all_ok;
}

esp_err_t i2c_bus_init(int sda_gpio, int scl_gpio)
{
    if (s_bus != NULL) {
        /* Already up — only valid if the caller wants the same pins. */
        return (sda_gpio == s_sda && scl_gpio == s_scl) ? ESP_OK
                                                        : ESP_ERR_INVALID_STATE;
    }

    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    s_sda = sda_gpio;
    s_scl = scl_gpio;
    const esp_err_t err = bus_create();
    if (err != ESP_OK) {
        s_sda = s_scl = -1;
        return err;
    }
    i2c_bus_scan();
    return ESP_OK;
}

i2c_master_bus_handle_t i2c_bus_handle(void)
{
    return s_bus;
}

esp_err_t i2c_bus_register_client(const i2c_bus_client_t *client)
{
    if (client == NULL || client->attach == NULL || client->detach == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE; /* bus not up yet */
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    bool known = false;
    for (size_t i = 0; i < s_client_count; i++) {
        if (s_clients[i] == client) {
            known = true;
            break;
        }
    }
    if (!known) {
        if (s_client_count < I2C_BUS_MAX_CLIENTS) {
            s_clients[s_client_count++] = client;
        } else {
            err = ESP_ERR_NO_MEM;
        }
    }
    xSemaphoreGive(s_lock);
    return err;
}

void i2c_bus_report_ok(void)
{
    if (s_lock == NULL) {
        return;
    }
    /* Uncontended on the happy path; recovery is the only other holder. */
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_attempts = 0;
    s_interval_ms = RECOVER_MIN_INTERVAL_MS;
    xSemaphoreGive(s_lock);
}

bool i2c_bus_recover(void)
{
    if (s_lock == NULL || s_sda < 0) {
        return false; /* never initialised — nothing to recover */
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);

    const int64_t now = esp_timer_get_time();
    if (s_last_recover_us != 0 &&
        (now - s_last_recover_us) < (int64_t)s_interval_ms * 1000) {
        xSemaphoreGive(s_lock); /* too soon — the caller just keeps failing */
        return false;
    }
    s_last_recover_us = now;
    s_attempts++;

    const bool soft = (s_bus != NULL) && (s_attempts <= SOFT_ATTEMPTS);
    const bool ok = soft ? recover_soft() : recover_hard();

    /* Back off while it keeps failing so a dead bus does not flood the log. */
    if (s_interval_ms < RECOVER_MAX_INTERVAL_MS) {
        s_interval_ms *= 2;
        if (s_interval_ms > RECOVER_MAX_INTERVAL_MS) {
            s_interval_ms = RECOVER_MAX_INTERVAL_MS;
        }
    }

    xSemaphoreGive(s_lock);
    return ok;
}
