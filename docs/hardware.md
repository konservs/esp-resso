# Hardware

> ⚠️ This machine runs on **mains voltage** and holds **pressurised boilers near
> steam temperature**. Read [safety.md](safety.md) before wiring anything. The
> firmware is a *secondary* safety layer, never the only one.

## Overview

A dual-boiler E61 machine:

- **Brew boiler** (~93 °C) — espresso extraction.
- **Steam boiler** (~125 °C) — steam wand / hot water.
- **E61 group** — the lever mechanically opens the 3-way valve and trips a
  microswitch; the firmware does **not** drive a group valve.

## Bill of materials (core electronics)

| Qty | Part | Notes |
|----:|------|-------|
| 1 | ESP32 dev module (WROOM-32) | Dual-core, classic ESP32 |
| 2 | MAX31865 RTD amplifier | One per boiler; 430 Ω reference for PT100 |
| 2 | PT100 RTD probe | M4/M5 threaded or clamp, boiler temperature |
| 2 | Solid-state relay (zero-cross) | Switch each boiler heating element |
| 1 | Vibratory pump + relay/SSR | Pump for extraction |
| 2 | Solenoid valve + driver | Brew / steam boiler auto-fill inlets |
| 1 | Flow meter (Hall, e.g. digmesa/gicar) | Volumetric dosing |
| 2 | Conductivity level probe | Brew + steam boilers (isolated AC-sensed) |
| 1 | Float switch | Reservoir low-water (reed/magnet) |
| 1 | Isolated 12 V supply | Floating rail for the sensing H-bridge |
| 1 | H-bridge (driver IC or 4 transistors) | Anti-phase AC excitation |
| 4 | Optocoupler | 2× control (ESP32→bridge), 2× AC-input sense (per probe) |
| — | Current-limit resistors | ~4.7 kΩ per probe (≈1–2 mA); see level-sensing.md |
| 1 | SSD1306 OLED, 128×64, I2C | Status display, address 0x3C |
| 2 | Momentary push button | UI (A = −/left, B = +/right) |
| 2 | Microswitch | E61 brew lever, steam knob |
| — | Thermal fuses, over-pressure valve | **Hardware** safety — see safety.md |

## GPIO map

Defined in [`components/drivers/include/drivers/pins.h`](../components/drivers/include/drivers/pins.h).
Change wiring there.

| Function | GPIO | Notes |
|----------|-----:|-------|
| SPI SCLK | 18 | Shared by both MAX31865 |
| SPI MOSI | 23 | |
| SPI MISO | 19 | |
| RTD brew CS | 5 | |
| RTD steam CS | 15 | Strapping pin, but CS idles high → boot-safe |
| I2C SDA | 21 | SSD1306 |
| I2C SCL | 22 | SSD1306 |
| SSR brew | 25 | Active-high |
| SSR steam | 26 | Active-high |
| Pump | 27 | Active-high |
| Fill valve (brew) | 13 | Active-high |
| Fill valve (steam) | 4 | Active-high |
| Flow meter | 34 | **Input-only**, needs external pull-up |
| Level H-bridge in A | 14 | Opto-isolated H-bridge drive (anti-phase) |
| Level H-bridge in B | 2 | Anti-phase pair; strapping/LED pin (idles low) |
| Level sense — brew | 35 | **Input-only**, optocoupler output (digital) |
| Level sense — steam | 36 | **Input-only**, optocoupler output (digital) |
| Level — reservoir | 39 | **Input-only**, float switch, external pull-up |
| Button A (−) | 32 | Internal pull-up, active-low |
| Button B (+) | 33 | Internal pull-up, active-low |
| Brew lever switch | 16 | Internal pull-up, active-low |
| Steam knob switch | 17 | Internal pull-up, active-low |

### ESP32 pin caveats baked into the choices

- GPIO **34/35/36/39** are input-only and have **no internal pull resistors** —
  add external pull-ups/downs for the flow meter and level probes.
- GPIO **0/2/12** are strapping pins and are avoided as driven outputs. GPIO 15
  is used only as a chip-select (idle high), which is safe at boot.

## Sensing details

- **Temperature:** PT100 over MAX31865 (SPI, mode 1). Resistance→temperature
  conversion is the portable `core/rtd.c` (Callendar–Van Dusen), unit-tested on
  the host. Sensor open/short is reported via the MAX31865 fault bit and becomes
  a safety trip.
- **Flow:** Hall flow meter on a pulse counter (`pulse_cnt`). Calibrate
  `FLOW_PULSES_PER_ML` in `hal_esp32_sensors.c` against a measured pour.
- **Level:** boiler probes use **opto-isolated H-bridge AC sensing** — two pins
  (GPIO14/GPIO2) drive an H-bridge on a floating 12 V rail that applies symmetric
  AC across probe↔shell (zero net DC, so no electrolysis); conduction lights a
  per-probe AC optocoupler read as a digital input. Galvanic isolation keeps
  boiler/mains noise off the ESP32. The control loop debounces the result and
  drives auto-fill. The reservoir uses a float switch. Full circuit and
  rationale: **[level-sensing.md](level-sensing.md)**.

## Not in v1 (future)

- **Pressure transducer / pressure profiling** — deliberately omitted for now;
  the brew controller stops on volume + time. The brew stage model already has a
  `BREW_PUMP_PRESSURE` mode reserved for when a sensor is added.
