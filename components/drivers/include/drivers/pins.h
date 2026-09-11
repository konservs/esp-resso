/**
 * @file pins.h
 * @brief Central ESP32 GPIO assignment for ESP.Resso.
 *
 * One place to change wiring. Keep this in sync with docs/hardware.md.
 *
 * ESP32 GPIO notes baked into the choices below:
 *   - GPIO 34/35/36/39 are input-only and have NO internal pull resistors, so a
 *     passive source on one of them needs an external pull: GPIO34 (flow meter,
 *     R12) and GPIO39 (reservoir float switch, R15) are both switch-to-GND
 *     sources and would otherwise float. GPIO35/36 need nothing — they are driven
 *     by the 74HC157's push-pull CMOS outputs (see the level block).
 *   - GPIO 0/2/5/12/15 are strapping pins; avoided for driven outputs. GPIO 5
 *     and 15 are used only as SPI chip-selects (idle-high), which is boot-safe.
 *   - GPIO 6-11 are the WROOM module's internal SPI-flash bus (SCK/SDO/SDI/
 *     SHD/SWP/SCS); never wired -> leave those module pads no-connect. GPIO 12
 *     sets the flash voltage, so keep it low at reset (no-connect + the chip's
 *     internal pull-down, or a 10k pull-down); never drive it high.
 *   - RESET PULL STATE decides what every ULN2003-buffered load does during the
 *     boot window (ROM bootloader -> app_main -> espresso_hal_init(), which
 *     itself runs storage + temp init before the actuators). GPIO 14/15 idle
 *     PULLED UP, GPIO 2/4/12 idle PULLED DOWN, and 13/16/17/25/26/27/32/33
 *     float. So every active-high output needs an external ~10k pulldown on its
 *     ULN2003 input -> see the heater and valve blocks below.
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
 * zero-cross SSR, active-high via the ULN2003 buffer. Each element is switched
 * independently: the PID sets one duty per boiler and the load guard maps it to
 * the elements — the LOWER element is primary, the UPPER is a boost used only
 * while warming up (or when the mains load budget allows). Co-enabled elements
 * share the same duty. See core/load_guard.c and hal_esp32_actuators.c.
 *
 * All four heater pins FLOAT at reset, which leaves a ULN2003 input undefined,
 * so each carries a ~10 kOhm pulldown to GND to hold its element OFF through the
 * boot window: R10 (brew LO), R22 (brew HI), R9 (steam LO), R17 (steam HI).
 * Steam HI moved off GPIO14 onto GPIO32 precisely to get away from GPIO14's
 * internal pull-up at reset; GPIO14 now carries PIN_LEVEL_REVERSE, where a high
 * at reset is harmless (the decoder is gated off — see the level block). */
#define PIN_SSR_BREW_LO   26
#define PIN_SSR_BREW_HI   33
#define PIN_SSR_STEAM_LO  25
#define PIN_SSR_STEAM_HI  32

/* --- Pump (SSR) and auto-fill valves (relays), active-high via ULN2003 ---- */
/* The E61 group's 3-way valve is mechanical (lever-actuated) — no GPIO.
 *
 * Boot safety: a fill valve stuck open through the boot window would admit line
 * pressure into a boiler on every power-up (and repeatedly on a boot loop), so
 * both valves sit on pins that cannot idle high. GPIO4 (steam) idles PULLED DOWN
 * inside the chip; GPIO13 (brew) floats and is held down by R23. The pump is on
 * GPIO27, which floats, held down by R16. hal_valve_init() only drives these low
 * after the bootloader, app_main, and the storage/temp init ahead of it in
 * espresso_hal_init(), so the resistors — not the firmware — are what keep the
 * machine safe across a reset.
 *
 * PCB FIX OUTSTANDING: R24, the ST_FILL pulldown, has its GND leg dangling — the
 * schematic wire stops 1.27 mm short of the pin, so the netlist shows the pad
 * unconnected. GPIO4's internal pull-down covers the boot window meanwhile, so
 * this is belt-and-braces rather than urgent, but close the gap on the next
 * revision. */
#define PIN_PUMP              27
#define PIN_VALVE_FILL_BREW   13
#define PIN_VALVE_FILL_STEAM  4

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
/* ENABLE floats at reset, so R11 (4.7k) pulls it UP to keep the decoder disabled */
/* — and every rod de-energised — until hal_level_init() runs. Do not remove it:  */
/* it is also what makes REVERSE boot-safe on GPIO14, whose internal pull-up would */
/* otherwise pick a polarity at reset. SELECT/REVERSE are plain logic inputs to the */
/* 139/157, not switch drivers, so neither needs a pulldown of its own.            */
#define PIN_LEVEL_SELECT     16  /* boiler select -> 139 A1 + 157 sel          */
#define PIN_LEVEL_ENABLE     17  /* drive enable  -> 139 EN (active-low, R11 PU)*/
#define PIN_LEVEL_REVERSE    14  /* polarity      -> 139 A0 (idles high; gated) */
/* The 47k sense pull-ups (R19/R18 brew, R21/R20 steam) sit on the opto collectors */
/* FEEDING the '157 — not on these pins. The mux output is push-pull CMOS, so      */
/* GPIO35/36 are actively driven both ways and need no external pull of their own. */
#define PIN_LEVEL_SENSE_POS  35  /* + conduction (157 out; input-only)        */
#define PIN_LEVEL_SENSE_NEG  36  /* - conduction (157 out; input-only)        */
#define PIN_LEVEL_RESERVOIR  39  /* float switch (input-only; R15 ext. pull-up)*/

/* --- UI buttons + machine switches: PCF8574 expander bits (active-low) ----- */
/* Moved off native GPIOs onto the I2C expander to free JTAG pins. The expander
 * pin idles high (weak pull-up); the contact wires to GND. P4..P7 are spare for
 * future buttons — just add an EXP_* bit and read it via hal_input. */
#define EXP_BTN_A          7  /* P7: button A (- / left)        */
#define EXP_BTN_B          6  /* P6: button B (+ / right)       */
#define EXP_SWITCH_BREW    5  /* P5: E61 brew lever microswitch */
#define EXP_SWITCH_STEAM   4  /* P4: steam knob microswitch     */
/* P0..P3: free for future buttons.                                            */

/* --- Freed / spare native GPIOs ------------------------------------------- */
/* Upper heater elements sit on GPIO 33 (brew) and 32 (steam); the level control
 * lines are 16/17/14 (SELECT/ENABLE/REVERSE); the steam fill valve has GPIO 4.
 * That leaves GPIO 2 as the only spare native pin — and it is a strapping pin, so
 * reserve it for a non-critical output at most (it idles pulled DOWN at reset, so
 * it would suit another active-high load if one is ever needed). Module pads for
 * GPIO 6-11 and 12 are no-connect by design. (JTAG is not wired; see
 * docs/hardware.md.) */

#endif /* ESPRESSO_DRIVERS_PINS_H */
