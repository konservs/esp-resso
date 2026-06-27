/**
 * @file hal_esp32_input.c
 * @brief The two UI buttons and the brew/steam control switches.
 *
 * All four inputs now live on a PCF8574 I2C expander (freeing native GPIOs
 * 13/14/15 for JTAG — see docs/hardware.md). Each is wired active-low to GND;
 * the expander's weak pull-ups hold an open contact high. Reads are raw: the UI
 * task polls slowly (~125 ms), which filters mechanical bounce, and
 * core/buttons interprets gestures from there.
 *
 * The whole 8-bit port is read per call; the last good value is cached so a
 * transient I2C NAK reports the previous state rather than a phantom press.
 * Spare expander bits (P4..P7) are free for future buttons.
 */
#include "hal/hal_input.h"

#include "drivers/pcf8574.h"
#include "drivers/pins.h"

/* Idle (nothing pressed) is all-high, since inputs are active-low. */
static uint8_t s_port = 0xFF;

espresso_result_t hal_input_init(void)
{
    return pcf8574_init(PCF8574_I2C_ADDR) == ESP_OK ? ESPRESSO_OK
                                                    : ESPRESSO_ERR_STATE;
}

/* Refresh the cached port snapshot; keep the last value on an I2C error. */
static uint8_t read_port(void)
{
    pcf8574_read(&s_port);
    return s_port;
}

/* Active-low: a pressed/engaged contact pulls its expander pin to 0. */
static bool pressed(uint8_t port, int bit)
{
    return ((port >> bit) & 0x01) == 0;
}

hal_buttons_t hal_buttons_read(void)
{
    const uint8_t port = read_port();
    hal_buttons_t b;
    b.a = pressed(port, EXP_BTN_A);
    b.b = pressed(port, EXP_BTN_B);
    return b;
}

bool hal_switch_brew(void)
{
    return pressed(read_port(), EXP_SWITCH_BREW);
}

bool hal_switch_steam(void)
{
    return pressed(read_port(), EXP_SWITCH_STEAM);
}
