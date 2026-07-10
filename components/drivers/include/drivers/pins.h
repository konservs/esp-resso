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
 *   - GPIO 6-11 are the WROOM module's internal SPI-flash bus (SCK/SDO/SDI/
 *     SHD/SWP/SCS); never wired -> leave those module pads no-connect. GPIO 12
 *     sets the flash voltage, so keep it low at reset (no-connect + the chip's
 *     internal pull-down, or a 10k pull-down); never drive it high.
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

/* --- Heaters: two elements per boiler (lower + upper), each on its own -----
 * zero-cross SSR, active-high via the ULN2003 buffer. Both elements of a boiler
 * follow the same PID duty (mirrored) today — see hal_esp32_actuators.c. Fit a
 * ~10 kOhm pulldown on every ULN2003 heater input so the elements stay OFF
 * through the boot window; GPIO14 especially idles with a weak INTERNAL PULL-UP
 * at reset, so its external pulldown is mandatory to not energise a heater. */
#define PIN_SSR_BREW_LO   25
#define PIN_SSR_BREW_HI   33
#define PIN_SSR_STEAM_LO  26
#define PIN_SSR_STEAM_HI  14

/* --- Pump (SSR) and auto-fill valves (relays), active-high via ULN2003 ---- */
/* The E61 group's 3-way valve is mechanical (lever-actuated) — no GPIO. */
#define PIN_PUMP          27
#define PIN_VALVE_FILL_BREW   13
#define PIN_VALVE_FILL_STEAM   4

/* --- Flow meter (pulse input) --------------------------------------------- */
#define PIN_FLOW_PULSE    34  /* input-only; external pull-up required */

/* --- Water-level sensing (isolated bipolar ±12 V; see docs/level-sensing.md) */
/* Three control lines feed a 74HC139 decoder (MCU side) whose four outputs     */
/* each drive one opto whose transistor switches a rod to ±12 V. The decoder    */
/* asserts only ONE output, so exactly one boiler rod is energised and P/N can  */
/* never conflict (hardware shoot-through interlock). A 74HC157 (SELECT) routes  */
/* the active boiler's POS/NEG conduction optos to two MCU inputs. The cold     */
/* reservoir uses a simple float switch.                                        */
/*   SELECT : 0 = brew, 1 = steam (also selects the 74HC157 sense mux)          */
/*   ENABLE : 74HC139 active-low enable (LOW = drive on, HIGH = idle)           */
/*   REVERSE: 0 = rod +12 V (sense POS), 1 = rod -12 V (sense NEG)              */
#define PIN_LEVEL_SELECT     16  /* boiler select -> 139 A1 + 157 sel          */
#define PIN_LEVEL_ENABLE     17  /* drive enable  -> 139 EN (active-low)       */
#define PIN_LEVEL_REVERSE    32  /* polarity      -> 139 A0                    */
#define PIN_LEVEL_SENSE_POS  35  /* + conduction (157 out; input-only)        */
#define PIN_LEVEL_SENSE_NEG  36  /* - conduction (157 out; input-only)        */
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

/* --- Freed / spare native GPIOs ------------------------------------------- */
/* GPIO 33 and the freed GPIO 14 now drive the two upper heater-element SSRs;
 * 16/17/32 carry the level control lines. That leaves GPIO 2 (the other freed
 * level-drive pin) as the only spare — and it is a strapping pin, so reserve it
 * for a non-critical output at most. (JTAG is not wired; see docs/hardware.md.) */

#endif /* ESPRESSO_DRIVERS_PINS_H */
