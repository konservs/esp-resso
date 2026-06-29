# Power supply & rails

This board is **low-voltage DC**. The only mains-voltage things it touches are
*switched* loads — the two boiler heaters and the pump — and those are isolated
behind solid-state relays. Everything the board itself powers runs from a single
**12 V DC** input.

> ⚠️ Mains wiring (heaters, pump, the SSR load side) is covered in
> [safety.md](safety.md). This document is only the DC side.

## Rails at a glance

```
 Mains (230/120 VAC)
   ├── SSR (zero-cross) ──► brew heater      ┐
   ├── SSR (zero-cross) ──► steam heater     │ mains loads — only SWITCHED by
   ├── SSR (zero-cross) ──► pump             │ this board, never powered from it
   └── relay ×2 ─────────► fill solenoids    ┘ (not in the DC budget below)

 12 V PSU  ──►  +12V / GND ─────────┬─────────────────────────────────────────────
 (mains→12V)                        │
                                    ├─► buck 12→5 V ─► +5 V ─► LDO 5→3.3 V ─► +3.3 V
                                    │        (flow meter)           (ESP32, 2×MAX31865,
                                    │                                SSD1306, PCF8574)
                                    │
                                    ├─► ULN2003 ─► SSR inputs ×3 (heaters+pump) + relay coils ×2 (fill)
                                    │
                                    └─► isolated DC-DC 12→12 V ─►  +12VA / GNDA
                                                          ‖ isolation barrier ‖   (floating)
                                                          level-sense H-bridge + probes
```

| Rail (net / ground) | Source | Powers |
|------|--------|--------|
| **+12V** / **GND** (logic) | mains→12 V PSU | buck input, isolated DC-DC, ULN2003 (3× SSR inputs + 2× relay coils) |
| **+5V** / GND | buck from 12 V | LDO input, Hall flow meter |
| **+3.3V** / GND | LDO from 5 V | ESP32, both MAX31865, SSD1306 OLED, PCF8574 |
| **+12VA** / **GNDA** (isolated) | isolated DC-DC | level-sense H-bridge + probes (see [level-sensing.md](level-sensing.md)) |

The names above are the **KiCad net names**. Everything is referenced to **GND**
except the isolated sensing front-end, which floats on **+12VA** / **GNDA** (see
**Isolation** below).

## Why buck *then* LDO (two stages, not one)

The ESP32 and the MAX31865 RTD front-ends want a **clean, quiet 3.3 V**, but
getting there from 12 V in one step is a bad trade either way:

- **A single LDO 12 V → 3.3 V** would burn `(12 − 3.3) × I` as heat. At the
  ESP32's ~0.2 A average that's **~1.7 W**, and **~4 W** on Wi-Fi TX peaks —
  far too hot for a small SOT-23 regulator, and wasteful (~70 % of the energy
  lost as heat).
- **A single buck 12 V → 3.3 V** is efficient but leaves switching ripple
  (hundreds of kHz–MHz) on the rail — right in the band that degrades RF range
  and shows up as noise in the RTD readings.

So we split the job:

1. **Buck 12 V → 5 V** does the big drop *efficiently* (~90 %), where its ripple
   is harmless.
2. **LDO 5 V → 3.3 V** drops the last 1.7 V *linearly*, which has excellent
   ripple rejection — it **cleans up** the buck's switching noise for the
   analog/RF loads. Its dissipation is now only `(5 − 3.3) × I` ≈ **0.34 W** at
   0.2 A — cool to the touch.

Place the LDO and its caps close to the ESP32 and the MAX31865s, and keep the
buck's switching node away from the RTD wiring and the antenna.

## Recommended parts

| Stage | Recommended | Why | Cheaper / alt |
|-------|-------------|-----|----------------|
| **Buck 12→5 V** | **Diodes AP63205** (3.8–32 V in, 5 V/2 A, synchronous, low-EMI) | Wide input margin for a noisy 12 V brick; high efficiency; quiet near RF | **MP1584EN** (28 V/3 A, very common); **TI TPS54331** (robust); or an **LM2596 module** for zero layout effort |
| **LDO 5→3.3 V** | **Diodes AP2112K-3.3** (600 mA, low-noise, 250 mV dropout, SOT-23-5) | The de-facto ESP32 LDO — low noise, handles Wi-Fi peaks with a bulk cap | **AMS1117-3.3** (1 A but 1.1 V dropout, higher Iq — cheap); **MCP1825S-3.3**; **RT9013-33** |
| **Isolated 12→12 V** (sensing) | **1 W isolated DC-DC** (e.g. Mornsun B1212S-1W, Murata NMA1212) | Creates the floating **+12VA** / **GNDA** rail the H-bridge needs; sensing load is ~0.2 W so 1 W is ample | A separate small isolated 12 V winding/PSU |
| **12 V PSU** | **12 V / 1 A enclosed** (e.g. Mean Well IRM-10-12 or LRS-15-12) | Logic + sensing + relay coils; the fill valves themselves are mains, relay-switched | 12 V / 2 A if you ever put a valve back on a 12 V DC coil (see below) |

Input current to a 28–32 V-rated buck matters because a cheap 12 V brick can
overshoot on load dump; the headroom keeps the regulator inside spec.

### Decoupling & layout
- **Buck:** input bulk ≥ 22 µF; output cap + inductor per datasheet. A ferrite
  bead or small LC between the 5 V output and the LDO input further attenuates
  switching ripple reaching the analog/RF side.
- **LDO:** ≥ 10 µF in and out (AP2112 needs ≥ 1 µF out).
- **ESP32 transients:** Wi-Fi TX draws ~500 mA for tens of µs. Add a **bulk
  100–470 µF** on +3.3 V right at the module (plus the usual 10 µF + 0.1 µF) so
  the rail doesn't droop and brown-out the MCU. The WROOM module has its own
  decoupling, but the bulk cap is what rides out the bursts.

### Protection
- **Fuse** + **reverse-polarity** protection (series P-FET or Schottky) on the
  12 V input, and a **TVS** (e.g. SMBJ16A) to clamp transients.
- **One ULN2003 buffers every actuator** off +12 V: GPIO → ULN2003 input, the
  output sinks the load to GND, and **COM → +12 V** provides the relay-coil
  flyback clamp. It drives the **three DC-input SSRs (two heaters + pump) at
  12 V** — robust, where a bare 3.3 V GPIO is marginal for Fotek-style 3–32 V
  inputs — plus the **two fill-relay coils**. No separate +5 V SSR buffers.
  Budget ~12 mA per SSR input and ~35 mA per relay coil on +12 V; the ULN2003's
  ~1 V Vce(sat) leaves ~11 V at each load (fine for both). The ULN2003A's inputs
  work from 3.3 V at these light loads — verify, or pick a logic-level-input
  variant.
- **Fill valves are switched by 12 V relays** (their coils on the ULN2003 above):
  the relay **contacts** switch the mains coil with **zero off-state leakage** —
  cheaper than an SSR and no snubber-leakage buzz on a small coil. An **RC snubber
  or MOV across the contacts** is optional, to cut arcing and extend contact life.
- The **pump SSR** must suit the pump: a vibratory pump is light (and a zero-cross
  SSR lets you add pulse-skip flow modulation later), but a **rotary (induction-
  motor) pump** has several-amp inrush — size that SSR/triac for the motor surge
  and add a snubber. Rotary pumps run on/off only (they can't be dimmed).

## Isolation — do not merge grounds

The level-sensing **+12VA** rail is a *separate ground domain*: its return is
**GNDA**, **not GND**. Per [level-sensing.md](level-sensing.md), the ESP32 talks
to the H-bridge only through optocouplers, and **+12VA** must come from an
**isolated** source. **Do not tie GNDA to GND** — that isolation is what lets the
H-bridge swing an earth-bonded boiler shell and keeps mains/boiler noise off the
MCU. This is why **+12VA** / **GNDA** is its own isolated DC-DC, even though the
*nominal* voltage is also 12 V.

## Power budget (electronics only)

Boiler heaters and the pump are **mains loads switched by SSR/relay** — they are
*not* powered by this board and are excluded. Assumptions: Wi-Fi associated;
auto-fill relay coils are intermittent; heater SSR inputs driven from +5 V.

**+3.3 V rail**

| Load | Typ | Peak |
|------|----:|-----:|
| ESP32-WROOM-32U (Wi-Fi associated) | 160 mA | 500 mA (TX burst) |
| 2× MAX31865 | 5 mA | 6 mA |
| SSD1306 OLED | 15 mA | 25 mA |
| PCF8574 + I²C pull-ups | 2 mA | 3 mA |
| Sense-opto pull-ups + misc | 5 mA | 8 mA |
| **Subtotal** | **~185 mA** | **~540 mA** |

**+5 V rail** (direct loads, plus it feeds the LDO)

| Load | Typ | Peak |
|------|----:|-----:|
| Hall flow meter | 12 mA | 15 mA |
| LDO input (≈ 3.3 V load, linear) | ~185 mA | ~540 mA |
| **Subtotal** | **~197 mA** | **~555 mA** |

(The SSR control inputs moved to +12 V, driven by the ULN2003 — see below.)

**+12V rail** (logic, ref GND)

| Load | Typ | Peak |
|------|----:|-----:|
| Buck input (feeding +5 V) | ~90 mA | ~255 mA |
| Isolated DC-DC input (sensing) | ~10 mA | ~15 mA |
| SSR control inputs ×3 (heaters + pump, via ULN2003) | ~24 mA | ~36 mA |
| Fill-valve relay coils ×2 (12 V, via ULN2003) | 0 (idle) | ~70 mA (both energized) |
| **Subtotal, no fill** | **~124 mA (≈1.5 W)** | **~306 mA (≈3.7 W)** |
| **Subtotal, both relays on** | — | **~376 mA (≈4.5 W)** |

**+12VA rail (isolated sensing)** — pulsed: ~5 mA during a read burst, ~0 between
(< 0.2 W). A 1 W isolated DC-DC is comfortable.

### Bottom line
- **Continuous electronics draw ≈ 1.5 W** (≈ 125 mA at 12 V), dominated by the
  ESP32; brief Wi-Fi peaks to ~3.5 W.
- All mains loads are *switched*, not powered: the **two heater SSRs and the pump
  SSR** are driven at 12 V through the **ULN2003** (~12 mA each), and the **two
  fill-relay coils** (~35 mA each) hang off the same chip — only these small
  currents load +12 V.
- A **12 V / 1 A (12 W)** supply covers it with comfortable margin. Bump to 2 A
  only if you later put a valve back on a 12 V DC coil.

### A note on the fill valves
This build uses **line-voltage (230/120 VAC) solenoids switched by 12 V relays** — the same scheme as many prosumer espresso machines like the Profitec Pro 700 (OEM Olab mains coils, part US1005), so the stock valves are compatible, and a relay is how commercial autofill controllers switch them. The same **ULN2003** that buffers the heater and pump SSR inputs also drives these relay coils from GPIO (its **COM pin to +12 V** provides the flyback clamp); the relay **contacts** switch the mains coil. A relay beats an SSR here because the valves switch only briefly and occasionally: it is cheaper and has **zero off-state leakage** (no snubber-leakage buzz on a small coil). An RC snubber or MOV **across the contacts** is optional, to reduce arcing and extend contact life. Only the relay coils load +12 V (~35 mA each), so a **12 V / 1 A** supply is still ample.

Two alternatives: a **packaged SSR** per valve (silent and wear-free, but pricier and its snubber leaks a few mA), or a **12 V DC solenoid** driven by a low-side MOSFET (e.g. AO3400) + flyback diode (stays low-voltage but isn't drop-in for prosumer valves and adds ~4 W per valve, pushing the PSU to 2 A). We chose mains valve + 12 V relay for drop-in compatibility and lowest cost.
