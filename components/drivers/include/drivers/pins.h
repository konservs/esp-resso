/**
 * @file pins.h
 * @brief Central ESP32 GPIO assignment for ESP.Resso.
 *
 * One place to change wiring. Keep this in sync with docs/hardware.md.
 *
 * ESP32 GPIO notes baked into the choices below:
 *   - GPIO 34/35/36/39 are input-only and have NO internal pull resistors
 *     (used here for the flow meter and level probes -> add external pulls).
 *   - GPIO 0/2/12/15 are strapping pins; avoided for driven outputs. GPIO 15
 *     is used only as an SPI chip-select (idle-high), which is boot-safe.
 */
#ifndef ESPRESSO_DRIVERS_PINS_H
#define ESPRESSO_DRIVERS_PINS_H

/* --- SPI bus (shared by both MAX31865 RTD front-ends) --------------------- */
#define PIN_SPI_SCLK      18
#define PIN_SPI_MOSI      23
#define PIN_SPI_MISO      19
#define PIN_RTD_BREW_CS    5
#define PIN_RTD_STEAM_CS  15

/* --- I2C bus (shared: SSD1306 OLED + PCF8574 input expander) -------------- */
#define PIN_I2C_SDA       21
#define PIN_I2C_SCL       22
#define SSD1306_I2C_ADDR  0x3C
#define PCF8574_I2C_ADDR  0x20  /* A2..A0 = GND; 0x38 for a PCF8574A part */

/* --- Heaters (solid-state relays, active-high) ---------------------------- */
#define PIN_SSR_BREW      25
#define PIN_SSR_STEAM     26

/* --- Pump and auto-fill solenoid valves (active-high relays/MOSFETs) ------ */
/* The E61 group's 3-way valve is mechanical (lever-actuated) — no GPIO. */
#define PIN_PUMP          27
#define PIN_VALVE_FILL_BREW   13
#define PIN_VALVE_FILL_STEAM   4

/* --- Flow meter (pulse input) --------------------------------------------- */
#define PIN_FLOW_PULSE    34  /* input-only; external pull-up required */

/* --- Water-level sensing (isolated H-bridge AC; see docs/level-sensing.md) - */
/* Two pins drive an opto-isolated H-bridge on a FLOATING 12 V rail that applies */
/* symmetric AC across each probe and the boiler shell (zero net DC -> no       */
/* electrolysis). Conduction (wet) lights a per-probe AC optocoupler read as a  */
/* digital input. The cold reservoir uses a simple float switch.               */
#define PIN_LEVEL_EXC_A      14  /* H-bridge input A (anti-phase)             */
#define PIN_LEVEL_EXC_B       2  /* H-bridge input B; strapping/LED pin, but  */
                                 /*   the opto sinks to GND so it idles low   */
#define PIN_LEVEL_BREW       35  /* brew sense (opto output, digital)         */
#define PIN_LEVEL_STEAM      36  /* steam sense (opto output, digital)        */
#define PIN_LEVEL_RESERVOIR  39  /* float switch (input-only, ext. pull-up)   */

/* --- UI buttons + machine switches: PCF8574 expander bits (active-low) ----- */
/* Moved off native GPIOs onto the I2C expander to free JTAG pins. The expander
 * pin idles high (weak pull-up); the contact wires to GND. P4..P7 are spare for
 * future buttons — just add an EXP_* bit and read it via hal_input. */
#define EXP_BTN_A          0  /* P0: button A (- / left)        */
#define EXP_BTN_B          1  /* P1: button B (+ / right)       */
#define EXP_SWITCH_BREW    2  /* P2: E61 brew lever microswitch */
#define EXP_SWITCH_STEAM   3  /* P3: steam knob microswitch     */
/* P4..P7: free for future buttons.                                            */

/* --- Freed native GPIOs (were buttons/switches) --------------------------- */
/* GPIO 13, 14, 15 are JTAG TCK/TMS/TDO and 12 is TDI. Relocating the brew fill
 * valve (13), level H-bridge A (14) and steam RTD CS (15) onto the now-free
 * 16/17/32/33 lets all four JTAG lines be wired. That reshuffle is a separate
 * step; for now 16/17/32/33 are simply unassigned. */

#endif /* ESPRESSO_DRIVERS_PINS_H */
