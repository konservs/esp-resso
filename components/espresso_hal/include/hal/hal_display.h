/**
 * @file hal_display.h
 * @brief Monochrome status display abstraction.
 *
 * The reference build uses a 128x64 I2C SSD1306 OLED (address 0x3C) to show the
 * boiler temperatures, the machine state and the live shot timer. The interface
 * is a tiny text/graphics API over an in-RAM framebuffer that is pushed to the
 * panel with ::hal_display_flush(). It is deliberately panel-agnostic so the
 * SSD1306 can be swapped for another small display without touching the UI.
 */
#ifndef ESPRESSO_HAL_DISPLAY_H
#define ESPRESSO_HAL_DISPLAY_H

#include "hal/hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialise the panel (I2C bus + SSD1306 init sequence). */
espresso_result_t hal_display_init(void);

/** Panel geometry in pixels. */
uint8_t hal_display_width(void);
uint8_t hal_display_height(void);

/** Clear the framebuffer (does not flush). */
void hal_display_clear(void);

/** Draw a NUL-terminated string at pixel (@p x, @p y) into the framebuffer. */
void hal_display_text(uint8_t x, uint8_t y, const char *str);

/** printf-style convenience wrapper around ::hal_display_text. */
void hal_display_printf(uint8_t x, uint8_t y, const char *fmt, ...);

/** Draw a horizontal progress bar (0..1) — e.g. heat-up or shot progress. */
void hal_display_progress(uint8_t x, uint8_t y, uint8_t w, uint8_t h, float frac);

/** Push the framebuffer to the panel over I2C. */
void hal_display_flush(void);

#ifdef __cplusplus
}
#endif

#endif /* ESPRESSO_HAL_DISPLAY_H */
