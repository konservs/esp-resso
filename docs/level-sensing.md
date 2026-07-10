# Water-level sensing (isolated bipolar ±12 V, decoded drive, one boiler at a time)

Each boiler's level is sensed with a **conductivity probe**: a stainless **rod**
whose tip sits at the "full" line, with the **boiler body (shell)** as the second
electrode. Water rising to the tip bridges rod→water→body and the resistance
drops from ~open to a few kΩ — a single **wet / dry** threshold per boiler.
Autofill runs the pump + inlet valve until the rod is covered.

## Contacts: four wires, (up to) three electrical nodes

| Contact | Meaning |
|---------|---------|
| **PROBE_BREW** | brew boiler rod |
| **PROBE_STEAM** | steam boiler rod |
| **SENSE_BREW / SENSE_STEAM** | brew / steam boiler body |

The bodies are normally bonded to **earth/chassis**, so SENSE_BREW = SENSE_STEAM = one node.
We **don't rely on that**: each boiler has its own POS/NEG sense pair, so sensing
still works if a body bond is poor or absent.

## Design at a glance

- **Bipolar ±12 V excitation.** The selected rod is driven to **+12VA** or
  **−12VA** relative to the earthed body — true AC, zero net DC, no electrolysis.
- **GNDA = earth = body**, the fixed 0 V return. There's a real −12 V rail, so no
  floating-pivot trick is needed.
- **One boiler at a time**, decoded from 3 control lines.
- **Hardware interlock via a decoder.** A 74HC139 turns {SELECT, ENABLE, REVERSE}
  into the four switch drives and *structurally* asserts at most one — shoot-through
  is impossible and there's no "forbidden" input combination.
- **Two-direction, per-boiler sensing.** Each boiler has a `POS` and a `NEG` opto;
  real conduction fires both (on their half-cycles) in step with the drive.
- **Variable frequency** (a few hundred Hz ↔ a few kHz) to reject fixed-frequency
  pump/heater/mains noise.
- **All logic on the 3.3 V MCU side.** The isolated domain is just the optos —
  the drive optos *are* the ±12VA switches (no MOSFETs), so there's no isolated
  logic supply and no level-shifters. The interlock is still pure hardware
  (combinational, independent of firmware).
- **Fully isolated**: 8 opto channels + an isolated DC-DC; the earthed boiler
  never touches logic ground or the RTD front-ends.

## Supply and grounding

Powered by an **isolated dual ±12 V DC-DC** (e.g. **Murata NMA1212**): input from
the logic +12 V, output **+12VA / GNDA / −12VA**, galvanically isolated.

- **GNDA is bonded to the boiler bodies (earth)** — the single earth tie.
- **+12VA = earth + 12 V, −12VA = earth − 12 V.**
- Never tie logic GND to GNDA; every signal crosses via an opto.

## Control: decode + interlock (MCU side, 3.3 V)

Three GPIOs feed a **74HC139** (dual 2→4 decoder w/ enable) *before* the optos:
`Ē ← ENABLE`, `A0 ← REVERSE`, `A1 ← SELECT`.

| ENABLE | SELECT | REVERSE | Decoder out | Switch on |
|:------:|:------:|:-------:|-------------|-----------|
| off | x | x | none | **idle** |
| on | 0 (brew) | 0 | Y0 | BREW_P (PROBE_BREW → +12VA) |
| on | 0 (brew) | 1 | Y1 | BREW_N (PROBE_BREW → −12VA) |
| on | 1 (steam) | 0 | Y2 | STEAM_P (PROBE_STEAM → +12VA) |
| on | 1 (steam) | 1 | Y3 | STEAM_N (PROBE_STEAM → −12VA) |

A decoder can assert **only one output at a time**, so P and N of a boiler (indeed
any two switches) can never be on together — that's the hardware interlock, and
every input combination is safe. `ENABLE` = on/off, `REVERSE` = polarity,
`SELECT` = boiler. The four active-low outputs drive the four drive-opto LEDs.

> Wire `ENABLE` so the decoder powers up **disabled** (all outputs off) at boot —
> e.g. a pull to the inactive level on the `Ē` line.

## Sense routing: 74HC157 mux (MCU side, 3.3 V)

The four sense optos have their outputs pulled up to 3.3 V and fed to a **74HC157**
2:1 mux; `SELECT` routes the active boiler's pair to the MCU:

| SELECT | `SENSE_POS` ← | `SENSE_NEG` ← |
|:------:|---------------|---------------|
| 0 (brew) | BREW_POS | BREW_NEG |
| 1 (steam) | STEAM_POS | STEAM_NEG |

So the unselected boiler's optos are physically disconnected from the sense pins —
belt-and-suspenders on top of "only one boiler is ever driven."

## Isolation: eight opto channels

| Opto | Dir | Carries |
|------|-----|---------|
| drive ×4 (BREW_P/N, STEAM_P/N) | MCU → sensing | 74HC139 outputs → rod switched to ±12VA (opto is the switch) |
| sense ×4 (BREW_POS/NEG, STEAM_POS/NEG) | sensing → MCU | per-boiler, per-direction conduction |

## Drive + sense circuit (isolated domain)

```
   +12VA ─┬─[BREW_P  opto ▷]─┐              ┌─[STEAM_P opto ▷]─┬─ +12VA
          │                  ● drive1       ● drive2          │
   −12VA ─┴─[BREW_N  opto ▷]─┘   │          │   └[STEAM_N opto ▷]┴─ −12VA
                                 │          │
                           [Rlim1 4.7k] [Rlim2 4.7k]
                                 │          │
                   BREW_POS ►|◄ BREW_NEG   STEAM_POS ►|◄ STEAM_NEG   (anti-parallel
                                 │          │                          sense-opto LED pairs)
                            PROBE_BREW PROBE_STEAM
                             )water(     )water(
                            SENSE_BREW SENSE_STEAM
                                 └─────┬─────┘
                                     GNDA (= earth, single tie)

   Each ▷ is a drive-opto output transistor (collector→emitter), NOT a MOSFET.
   The opto's LED sits on the MCU side, lit by a 74HC139 output; lighting it
   turns the transistor on and ties the rod to that rail. A phototransistor
   conducts one way only — exactly what a single-polarity switch needs — so one
   PC817 type covers both the +12VA (P) and −12VA (N) roles. No gate, no bias.
```

- **The drive opto *is* the switch.** Probe current is only ~1–2 mA — well within
  a PC817's output rating — so the phototransistor ties the rod to the rail
  directly, no MOSFET or gate driver in the isolated domain. Each switch is
  single-polarity and a phototransistor conducts one way only, so the same PC817
  serves both the +12VA (P, collector→rail) and −12VA (N, emitter→rail) roles.
- **V(CEO) headroom.** Off-state a switch can see the full swing
  `+12VA − (−12VA) = ~24 V` across collector-emitter, so confirm the opto covers
  it (PC817-class parts are typically ~35 V: adequate, but modest margin — pick an
  80 V-class part for more headroom).
- **Saturation now matters.** The opto must saturate while *carrying* the probe
  current (not just pulling a high-impedance gate), so keep the LED well driven:
  the 330 Ω series R gives ~5–6 mA, which a ≥ 50 % CTR part turns into ≥ 2.5 mA —
  enough to saturate the 1–2 mA switch. Drop toward ~220 Ω for worst-case-CTR parts.
- **Rlim (~4.7 kΩ)** per boiler sets ~1–2 mA — low current + AC = no electrolysis.
- The **anti-parallel sense pair** sits in each rod line: `POS` lights on the +
  half (rod at +12VA), `NEG` on the − half (rod at −12VA). Because these are
  per-boiler, sensing does not depend on the bodies being a common node.

## Operation (one read)

Behind the unchanged `hal_level_present()`
([`hal_esp32_sensors.c`](../components/drivers/src/hal_esp32_sensors.c)):

1. Set `SELECT` to the boiler (routes both its drive and its sense mux channel).
2. For a burst at frequency *f*, alternate with `ENABLE` gating each flip:
   assert `REVERSE=0` (sample `SENSE_POS`), all-off gap, `REVERSE=1` (sample
   `SENSE_NEG`), all-off gap. Keep + and − dwell **equal** → zero net DC.
3. Each half-cycle is scored **differentially**: it only counts when the driven
   direction's opto conducts **and the other one does not**. Genuine water
   forward-biases just one sense opto per polarity (`SENSE_POS` alone on + halves,
   `SENSE_NEG` alone on − halves); *both* lines asserting at once means a rod
   short, common-mode noise, or a floating/disconnected sense front-end — not
   water — and is rejected rather than read as "full".
4. `wet` then requires **both** `SENSE_POS` (+ halves) **and** `SENSE_NEG`
   (− halves) to log ≥ `LEVEL_WET_MIN_HITS`.
5. Optionally repeat at a second *f* and require agreement (noise rejection).
6. `ENABLE` off between reads (idle). The control loop debounces so
   boiling/splashing doesn't chatter the autofill valve.

Autofill lives in `control_task` ([control.md](control.md)): it opens the fill
valve while the rod reads dry, gated on the reservoir having water and no fault
(dry-fire protection).

## Reservoir

The cold **reservoir** uses a **float switch** (reed + magnet) on `GPIO39`, wired
to GND with an external pull-up (GPIO39 is input-only). No AC sensing needed.

## Safety / isolation notes

- Keep the barrier intact: **do not join logic GND to GNDA.** All eight signals go
  through optos; the supply is an isolated DC-DC.
- **Bodies → GNDA is the single earth tie**; don't add a separate GNDA→chassis
  wire on top.
- The **decoder is the shoot-through interlock** — even a firmware bug can't turn
  on two switches. Still gate `ENABLE` off across `SELECT`/`REVERSE` changes.
- The rod lead is a long antenna in a hot, noisy machine — the opto-isolated
  digital sense plus the two-direction + variable-frequency checks reject the
  machine's electrical noise far better than an analog probe.

## Pin summary

| Signal | GPIO | Dir | Role |
|--------|-----:|-----|------|
| `PIN_LEVEL_SELECT` | 16 | out | boiler select → 74HC139 A1 + 74HC157 sel |
| `PIN_LEVEL_ENABLE` | 17 | out | drive enable → 74HC139 Ē (idle-off at boot) |
| `PIN_LEVEL_REVERSE` | 32 | out | polarity → 74HC139 A0 |
| `PIN_LEVEL_SENSE_POS` | 35 | in | + conduction (74HC157 out; input-only) |
| `PIN_LEVEL_SENSE_NEG` | 36 | in | − conduction (74HC157 out; input-only) |
| `PIN_LEVEL_RESERVOIR` | 39 | in | reservoir float switch (ext. pull-up) |

(This frees the old EXC pins GPIO14 and GPIO2.)

## Components (this subsystem)

| Ref | Part | Value | Role |
|-----|------|------:|------|
| Isolated DC-DC | Murata NMA1212 (or eq.) | 1 W, ±12 V | +12VA / GNDA / −12VA |
| Drive decoder | 74HC139 | 3.3 V | {SELECT,ENABLE,REVERSE} → 4 switch drives + interlock |
| Sense mux | 74HC157 | 3.3 V | SELECT routes active boiler's POS/NEG → 2 pins |
| Drive optos (×4) | PC817 (V(CEO) ≥ 24 V) | — | 74HC139 out → **switch rod to ±12VA directly** (opto *is* the switch) |
| Drive LED series R (×4) | resistor | 330 Ω | decoder output → PC817 LED (→ ~220 Ω for low-CTR parts) |
| Sense optos (×4) | PC817 | — | per-boiler, per-direction (anti-parallel LED pair) |
| Sense pull-ups (×4) | resistor | 47 kΩ → 3.3 V | opto output → 74HC157 inputs (low CTR at 1–2 mA) |
| Rlim (×2) | resistor | 4.7 kΩ | ~1–2 mA probe current |

Values are starting points — tune `Rlim` for your water hardness and the firmware
thresholds. If you later confirm the bodies are always common, the four sense
optos could collapse to two shared ones (drop per-boiler independence).
