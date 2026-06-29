#include "drivers/ssd1306.h"

#include <string.h>

#include "driver/i2c_master.h"

#include "drivers/font5x7.h"
#include "drivers/i2c_bus.h"

#define SSD1306_PAGES (SSD1306_HEIGHT / 8)
#define FB_SIZE       (SSD1306_WIDTH * SSD1306_PAGES)
#define I2C_TIMEOUT_MS 100

static i2c_master_dev_handle_t s_dev;
static uint8_t s_fb[FB_SIZE];
static bool s_ok; /* tracks whether the last panel write succeeded */

static esp_err_t cmd(uint8_t c)
{
    const uint8_t buf[2] = { 0x00, c }; /* 0x00 = command stream */
    return i2c_master_transmit(s_dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

esp_err_t ssd1306_init(int sda_gpio, int scl_gpio, uint8_t i2c_addr)
{
    /* Create (or reuse) the bus shared with the PCF8574 input expander. */
    esp_err_t err = i2c_bus_init(sda_gpio, scl_gpio);
    if (err != ESP_OK) {
        return err;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = i2c_addr,
        .scl_speed_hz = 400000,
    };
    err = i2c_master_bus_add_device(i2c_bus_handle(), &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        return err;
    }

    /* Standard SSD1306 128x64 init sequence. */
    static const uint8_t init_seq[] = {
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
    for (size_t i = 0; i < sizeof(init_seq); i++) {
        err = cmd(init_seq[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    ssd1306_clear();
    ssd1306_flush();
    return ESP_OK;
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
    /* Whole-buffer write in horizontal addressing mode. */
    cmd(0x21); cmd(0); cmd(SSD1306_WIDTH - 1);   /* column range */
    cmd(0x22); cmd(0); cmd(SSD1306_PAGES - 1);   /* page range   */

    static uint8_t out[1 + FB_SIZE];
    out[0] = 0x40; /* data stream */
    memcpy(&out[1], s_fb, FB_SIZE);
    s_ok = (i2c_master_transmit(s_dev, out, sizeof(out), I2C_TIMEOUT_MS) == ESP_OK);
}

bool ssd1306_ok(void)
{
    return s_ok;
}
