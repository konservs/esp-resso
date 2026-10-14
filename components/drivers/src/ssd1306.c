#include "drivers/ssd1306.h"

#include <string.h>

#include "driver/i2c_master.h"
#include "esp_log.h"

#include "drivers/font5x7.h"
#include "drivers/i2c_bus.h"

static const char *TAG = "ssd1306";

#define SSD1306_PAGES (SSD1306_HEIGHT / 8)
#define FB_SIZE       (SSD1306_WIDTH * SSD1306_PAGES)
#define I2C_TIMEOUT_MS 100

/* Consecutive failed frames before we stop blaming the frame and start
 * suspecting the bus. Three frames is a little under half a second at the UI
 * task's 125 ms cadence — long enough to ride out a one-off NAK, short enough
 * that a jam does not sit there for a noticeable time. */
#define FAIL_LIMIT 3

static i2c_master_dev_handle_t s_dev;
static uint8_t s_addr;
static uint8_t s_fb[FB_SIZE];
static bool s_ok;      /* tracks whether the last panel write succeeded */
static int  s_fails;   /* consecutive failed frames                     */

static esp_err_t cmd(uint8_t c)
{
    const uint8_t buf[2] = { 0x00, c }; /* 0x00 = command stream */
    return i2c_master_transmit(s_dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

/* Standard SSD1306 128x64 init sequence. Re-run on every attach: after a bus
 * jam the panel's addressing mode and column/page pointers are whatever the
 * truncated transfer left behind, which is exactly what shows up as a screen
 * full of artifacts. Rebuilding the bus without this would fix the wire and
 * leave the picture broken. */
static const uint8_t INIT_SEQ[] = {
    0xAE,             /* display off */
    0xD5, 0x80,       /* clock divide */
    0xA8, 0x3F,       /* multiplex = 63 */
    0xD3, 0x00,       /* display offset */
    0x40,             /* start line 0 */
    0x8D, 0x14,       /* charge pump on */
    0x20, 0x00,       /* horizontal addressing mode */
    0xA1,             /* segment remap */
    0xC8,             /* COM scan dec */
    0xDA, 0x12,       /* COM pins */
    0x81, 0xCF,       /* contrast */
    0xD9, 0xF1,       /* pre-charge */
    0xDB, 0x40,       /* VCOM detect */
    0xA4,             /* resume to RAM content */
    0xA6,             /* normal (non-inverted) */
    0xAF,             /* display on */
};

static esp_err_t panel_init(void)
{
    for (size_t i = 0; i < sizeof(INIT_SEQ); i++) {
        const esp_err_t err = cmd(INIT_SEQ[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

/* Push the framebuffer with no recovery policy attached. Both ::ssd1306_flush
 * and the attach hook go through here; only the former reacts to failure, so
 * that recovery cannot recurse back into itself. */
static esp_err_t fb_write(void)
{
    esp_err_t err = cmd(0x21);                     /* column range */
    if (err == ESP_OK) err = cmd(0);
    if (err == ESP_OK) err = cmd(SSD1306_WIDTH - 1);
    if (err == ESP_OK) err = cmd(0x22);            /* page range */
    if (err == ESP_OK) err = cmd(0);
    if (err == ESP_OK) err = cmd(SSD1306_PAGES - 1);
    if (err != ESP_OK) {
        /* The window commands set where the pixels land. If they did not make
         * it there is no point shipping a kilobyte of data to an unknown
         * address — that is how a failing bus paints garbage. */
        return err;
    }

    static uint8_t out[1 + FB_SIZE];
    out[0] = 0x40; /* data stream */
    memcpy(&out[1], s_fb, FB_SIZE);
    return i2c_master_transmit(s_dev, out, sizeof(out), I2C_TIMEOUT_MS);
}

static esp_err_t dev_detach(void)
{
    if (s_dev == NULL) {
        return ESP_OK;
    }
    const esp_err_t err = i2c_master_bus_rm_device(s_dev);
    if (err == ESP_OK) {
        s_dev = NULL;
    }
    return err;
}

static esp_err_t dev_attach(void)
{
    if (s_dev != NULL) {
        return ESP_OK;
    }
    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = s_addr,
        .scl_speed_hz = 400000,
    };
    esp_err_t err = i2c_master_bus_add_device(i2c_bus_handle(), &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        s_dev = NULL;
        return err;
    }

    err = panel_init();
    if (err == ESP_OK) {
        err = fb_write(); /* repaint what was on screen before the jam */
    }
    s_ok = (err == ESP_OK);
    return err;
}

static const i2c_bus_client_t S_CLIENT = {
    .name = "ssd1306",
    .detach = dev_detach,
    .attach = dev_attach,
};

esp_err_t ssd1306_init(int sda_gpio, int scl_gpio, uint8_t i2c_addr)
{
    /* Create (or reuse) the bus shared with the PCF8574 input expander. */
    esp_err_t err = i2c_bus_init(sda_gpio, scl_gpio);
    if (err != ESP_OK) {
        return err;
    }

    s_addr = i2c_addr;
    err = i2c_bus_register_client(&S_CLIENT);
    if (err != ESP_OK) {
        return err;
    }

    ssd1306_clear();
    return dev_attach();
}

void ssd1306_clear(void)
{
    memset(s_fb, 0, sizeof(s_fb));
}

void ssd1306_set_pixel(int x, int y, bool on)
{
    if (x < 0 || x >= SSD1306_WIDTH || y < 0 || y >= SSD1306_HEIGHT) {
        return;
    }
    const int idx = x + (y / 8) * SSD1306_WIDTH;
    const uint8_t bit = (uint8_t)(1u << (y % 8));
    if (on) {
        s_fb[idx] |= bit;
    } else {
        s_fb[idx] &= (uint8_t)~bit;
    }
}

void ssd1306_fill_rect(int x, int y, int w, int h, bool on)
{
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            ssd1306_set_pixel(x + i, y + j, on);
        }
    }
}

void ssd1306_draw_char(int x, int y, char c)
{
    if ((unsigned char)c < FONT5X7_FIRST_CHAR || (unsigned char)c > 0x7F) {
        c = '?';
    }
    const uint8_t *glyph = FONT5X7[(unsigned char)c - FONT5X7_FIRST_CHAR];
    for (int col = 0; col < FONT5X7_WIDTH; col++) {
        for (int row = 0; row < 7; row++) {
            ssd1306_set_pixel(x + col, y + row, (glyph[col] >> row) & 0x01);
        }
    }
}

void ssd1306_draw_text(int x, int y, const char *s)
{
    while (*s != '\0') {
        ssd1306_draw_char(x, y, *s++);
        x += FONT5X7_WIDTH + 1; /* 1px inter-character gap */
    }
}

void ssd1306_flush(void)
{
    /* A NULL handle means a rebuild could not re-attach us. That still has to
     * feed the failure path below, or the one state only another rebuild can
     * clear would be the one state that never asks for one. */
    const esp_err_t err = (s_dev == NULL) ? ESP_ERR_INVALID_STATE : fb_write();
    if (err == ESP_OK) {
        s_ok = true;
        s_fails = 0;
        i2c_bus_report_ok();
        return;
    }

    s_ok = false;
    if (s_fails < FAIL_LIMIT) {
        s_fails++;
    }
    if (s_fails >= FAIL_LIMIT) {
        /* Paced inside i2c_bus_recover, so calling it every frame from here is
         * fine — most calls return straight away without touching the bus. */
        if (i2c_bus_recover()) {
            ESP_LOGW(TAG, "bus recovery run after %d failed frame(s)", s_fails);
            s_fails = 0;
        }
    }
}

bool ssd1306_ok(void)
{
    return s_ok;
}

size_t ssd1306_snapshot(uint8_t *dst, size_t len)
{
    const size_t n = len < FB_SIZE ? len : FB_SIZE;
    memcpy(dst, s_fb, n);
    return n;
}
