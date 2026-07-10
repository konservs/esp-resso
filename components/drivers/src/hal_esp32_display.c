/**
 * @file hal_esp32_display.c
 * @brief Binds the hal_display interface to the SSD1306 OLED driver.
 */
#include "hal/hal_display.h"

#include <stdarg.h>
#include <stdio.h>

#include "drivers/pins.h"
#include "drivers/ssd1306.h"

espresso_result_t hal_display_init(void)
{
    return ssd1306_init(PIN_I2C_SDA, PIN_I2C_SCL, SSD1306_I2C_ADDR) == ESP_OK
               ? ESPRESSO_OK
               : ESPRESSO_ERR_STATE;
}

bool hal_display_ok(void) { return ssd1306_ok(); }

uint8_t hal_display_width(void)  { return SSD1306_WIDTH; }
uint8_t hal_display_height(void) { return SSD1306_HEIGHT; }

void hal_display_clear(void)
{
    ssd1306_clear();
}

void hal_display_text(uint8_t x, uint8_t y, const char *str)
{
    ssd1306_draw_text(x, y, str);
}

void hal_display_printf(uint8_t x, uint8_t y, const char *fmt, ...)
{
    char buf[32];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ssd1306_draw_text(x, y, buf);
}

void hal_display_progress(uint8_t x, uint8_t y, uint8_t w, uint8_t h, float frac)
{
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    /* Border, then fill. */
    ssd1306_fill_rect(x, y, w, h, true);
    ssd1306_fill_rect(x + 1, y + 1, w - 2, h - 2, false);
    ssd1306_fill_rect(x + 1, y + 1, (int)((w - 2) * frac), h - 2, true);
}

void hal_display_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool filled)
{
    if (w == 0 || h == 0) {
        return;
    }
    if (filled) {
        ssd1306_fill_rect(x, y, w, h, true);
        return;
    }
    /* 1px outline. */
    ssd1306_fill_rect(x, y, w, 1, true);             /* top    */
    ssd1306_fill_rect(x, y + h - 1, w, 1, true);     /* bottom */
    ssd1306_fill_rect(x, y, 1, h, true);             /* left   */
    ssd1306_fill_rect(x + w - 1, y, 1, h, true);     /* right  */
}

void hal_display_flush(void)
{
    ssd1306_flush();
}

size_t hal_display_snapshot(uint8_t *buf, size_t len)
{
    return ssd1306_snapshot(buf, len);
}
