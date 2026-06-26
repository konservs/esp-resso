/**
 * @file ssd1306.h
 * @brief Minimal framebuffer driver for a 128x64 I2C SSD1306 OLED.
 *
 * Maintains an in-RAM framebuffer; ::ssd1306_flush pushes it to the panel.
 * Text uses the 5x7 font in font5x7.h.
 */
#ifndef ESPRESSO_DRIVERS_SSD1306_H
#define ESPRESSO_DRIVERS_SSD1306_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define SSD1306_WIDTH  128
#define SSD1306_HEIGHT 64

/** Initialise the I2C bus and the panel. */
esp_err_t ssd1306_init(int sda_gpio, int scl_gpio, uint8_t i2c_addr);

void ssd1306_clear(void);
void ssd1306_set_pixel(int x, int y, bool on);
void ssd1306_fill_rect(int x, int y, int w, int h, bool on);
void ssd1306_draw_char(int x, int y, char c);
void ssd1306_draw_text(int x, int y, const char *s);
void ssd1306_flush(void);

#endif /* ESPRESSO_DRIVERS_SSD1306_H */
