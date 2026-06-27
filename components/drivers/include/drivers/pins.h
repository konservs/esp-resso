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

/* --- I2C bus (SSD1306 OLED) ----------------------------------------------- */
#define PIN_I2C_SDA       21
#define PIN_I2C_SCL       22
#define SSD1306_I2C_ADDR  0x3C

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

/* --- UI buttons (internal pull-ups, active-low) --------------------------- */
#define PIN_BTN_A         32
#define PIN_BTN_B         33

/* --- Machine control switches (E61 lever + steam knob, active-low) -------- */
#define PIN_SWITCH_BREW   16
#define PIN_SWITCH_STEAM  17

#endif /* ESPRESSO_DRIVERS_PINS_H */
