# Water-level sensing (isolated bipolar ±12 V, decoded drive, one boiler at a time)

Each boiler's level is sensed with a **conductivity probe**: a stainless **rod**
whose tip sits at the "full" line, with the **boiler body (shell)** as the second
electrode. Water rising to the tip bridges rod→water→body and the resistance
drops from ~open to a few kΩ — a single **wet / dry** threshold per boiler.
Autofill runs the pump + inlet valve until the rod is covered.

## Contacts: four wires, three electrical nodes

| Contact | Net | Meaning |
|---------|-----|---------|
| **PROBE_BREW** | `PROBE_BREW` | brew boiler rod (J3.1) |
| **PROBE_STEAM** | `PROBE_STEAM` | steam boiler rod (J4.1) |
| brew / steam body | `GNDA` | both boiler bodies (J3.2, J4.2) |

The bodies are bonded to **earth/chassis** and both tie directly to `GNDA`, so they
are **one node** by design — that tie is the isolated domain's 0 V reference (see
[Supply and grounding](#supply-and-grounding)).

Per-boiler independence does not depend on that, though: each boiler's `POS`/`NEG`
sense-opto pair sits **in its own rod line**, upstream of the water, so it only ever
carries that boiler's probe current. A poor body bond degrades the boiler it belongs
to and cannot make the other boiler misread.

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

> `ENABLE` carries a **4.7 kΩ pull-up to 3.3 V (R11)** so the decoder powers up
> **disabled** (all outputs off) at boot, before `hal_level_init()` drives the pin.

## Sense routing: 74HC157 mux (MCU side, 3.3 V)

The four sense optos have their outputs pulled up to 3.3 V and fed to a **74HC157**
(U15) 2:1 mux; `SELECT` routes the active boiler's pair to the MCU:

| SELECT | `SENSE_POS` ← U15 Za | `SENSE_NEG` ← U15 Zb |
|:------:|----------------------|----------------------|
| 0 (brew) | `SENSE_BREW_POS` (I0a, U8) | `SENSE_BREW_NEG` (I0b, U14) |
| 1 (steam) | `SENSE_STEAM_POS` (I1a, U17) | `SENSE_STEAM_NEG` (I1b, U9) |

So the unselected boiler's optos are physically disconnected from the sense pins —
belt-and-suspenders on top of "only one boiler is ever driven."

Two wiring details matter on this part:

- **`Ē` (pin 15) is tied to GND**, permanently enabling the mux. The '157 has *no*
  tri-state — a high `Ē` forces both outputs **LOW**, which (conduction being
  active-low) is indistinguishable from "both sense lines conducting" and would
  read as a fault. Do **not** gate `Ē` from `ENABLE`; `SELECT` alone does the
  routing. Use a '257 if you ever need a high-Z output instead.
- **The unused c/d channels have their inputs tied to GND** (pins 10, 11, 13, 14).
  Floating CMOS inputs hold both output FETs partly on, raising I(CC) and coupling
  noise into the used channels through the shared supply. The unused outputs
  (pins 9, 12) are correctly left open.

## Isolation: eight opto channels

| Opto | Dir | Carries |
|------|-----|---------|
| drive ×4 (BREW_P/N, STEAM_P/N) | MCU → sensing | 74HC139 outputs → rod switched to ±12VA (opto is the switch) |
| sense ×4 (BREW_POS/NEG, STEAM_POS/NEG) | sensing → MCU | per-boiler, per-direction conduction |

## Drive + sense circuit (isolated domain)

```
   Brew channel. Steam is identical: U3/U13 drive, R7 = Rlim, U17 = POS, U9 = NEG.

   +12VA ────[ U10  BREW_P  ▷ ]────┐
                                   ├──● drive node    exactly one opto ever on
   −12VA ────[ U11  BREW_N  ▷ ]────┘  │               (74HC139 interlock)
                                      │
                              [ R8   4.7 kΩ ]         Rlim — sets probe current.
                                      │               ONE per boiler, shared by both
                                      ● excitation    polarities, so the + and − half
                                      │   node        cycles see identical impedance
                          ┌───────────┴───────────┐   → zero net DC
                        ──▼── U8   POS          ──▲── U14  NEG
                          │    (+ half)           │    (− half)
                          └───────────┬───────────┘   anti-parallel: each LED clamps
                                      │               the other's reverse to ~1.2 V,
                                      │               so neither is ever overstressed
                          PROBE_BREW  ●── J3.1 ── rod ──┐
                                      │                 │
                                  ) water (             │  boiler
                                      │  few kΩ wet     │
                                      │  open when dry  │
                                GNDA  ●── J3.2 ── body ─┘

   ▼ / ▲ = sense-opto LED; the triangle points the way current flows.
   ▷     = drive-opto output transistor (collector→emitter), NOT a MOSFET.

   Both boiler bodies tie directly to GNDA — the single earth reference.

   The drive opto's LED sits on the MCU side, lit by a 74HC139 output; lighting it
   turns the transistor on and ties the rod to that rail. A phototransistor
   conducts one way only — exactly what a single-polarity switch needs — so one
   PC817 type covers both the +12VA (P) and −12VA (N) roles. No gate, no bias.

   The sense pair sits in the ROD line, between Rlim and the rod — not in the body
   return. That placement is load-bearing: it makes each pair carry the full probe
   current of its own boiler only. Moving it to the body side would put both
   boilers' pairs in parallel across the shared GNDA tie, halving the current
   through each and letting a failure in one channel silently reroute the other.
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
  Use **one** resistor per boiler on the shared drive node, not one per polarity:
  a single resistor guarantees the + and − half-cycles see the same impedance, and
  splitting it puts a tolerance mismatch straight into the water (net DC).
- The **anti-parallel sense pair** sits in each rod line: `POS` lights on the +
  half (rod at +12VA), `NEG` on the − half (rod at −12VA). Orientation matters —
  the LED whose **anode faces Rlim** is `POS` (it conducts when current flows
  Rlim → rod); the one whose anode faces the rod is `NEG`. Getting these swapped
  reads as permanently dry with *no* fault raised, because exactly one line still
  asserts per half-cycle — it just asserts the wrong one.
- **Sense-opto CTR is the tight budget.** PC817 CTR is specified at I(F) = 5 mA and
  degrades sharply below ~1 mA, so at a 1–2 mA probe current expect only a few
  hundred µA of collector current. The pull-up must be weak enough that this still
  pulls the mux input below V(IL): 47 kΩ needs ~53 µA (≈10× margin), while 4.7 kΩ
  needs ~530 µA and will leave the input stranded near mid-rail — which puts the
  '157 in its linear region and shows up as ~1.9 V on its output. If a sense line
  reads mid-rail, measure the drop across Rlim first: `I = V(Rlim) / R` is the
  single most diagnostic number in this subsystem.

## Operation (one read)

Sensing runs in a dedicated **`level_task`**
([`level_task.c`](../main/tasks/level_task.c)), not the control loop: a read
drives the isolated burst below and busy-waits a few ms, so it is kept off the
control/safety core. The task debounces each probe and publishes the result to
`app_state_t` as a lock-free atomic (`brew_probe_state` / `steam_probe_state`);
`control_task` just reads those. Each publish is timestamped, and `control_task`
treats a probe as untrusted — heaters off, fill shut — when it reads
`HAL_LEVEL_UNKNOWN` (the boot seed) or the timestamp is older than
`LEVEL_STALE_MS` (10 s), so a stalled level task fails safe. Because it is its
own task, its sensing cadence
(`LEVEL_PERIOD_MS`) is tunable independently of the control period — see the
`LEVEL_DEBUG_SWEEP` bench mode in [`hal_level.h`](../components/espresso_hal/include/hal/hal_level.h).

Behind `hal_level_read()`
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
6. `ENABLE` off between reads (idle). The level task debounces so
   boiling/splashing doesn't chatter the autofill valve.

Autofill lives in `control_task` ([control.md](control.md)): it reads the level
task's published state and opens the fill valve while the rod reads dry, gated on
the reservoir having water and no fault (dry-fire protection). The reservoir
float switch is a plain GPIO (no drive lines), so `control_task` still reads it
directly.

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
| `PIN_LEVEL_ENABLE` | 17 | out | drive enable → 74HC139 Ē; **R11** 4.7 kΩ pull-up = idle-off at boot |
| `PIN_LEVEL_REVERSE` | 14 | out | polarity → 74HC139 A0 (idles high at reset; gated by R11) |
| `PIN_LEVEL_SENSE_POS` | 35 | in | + conduction (74HC157 out; input-only, no pull needed) |
| `PIN_LEVEL_SENSE_NEG` | 36 | in | − conduction (74HC157 out; input-only, no pull needed) |
| `PIN_LEVEL_RESERVOIR` | 39 | in | reservoir float switch (input-only; **R15** 4.7 kΩ pull-up) |

`SELECT` and `REVERSE` are logic inputs to the '139/'157, not switch drivers, so
neither needs a pulldown — **R11 is the one part that makes this subsystem
boot-safe**, because the decoder enable is active-low and GPIO 17 floats at reset.
REVERSE can therefore live on GPIO 14 despite its internal pull-up: a polarity
selected at reset goes nowhere while the decoder is disabled. GPIO 2 remains the
only spare native pin.

## Components (this subsystem)

| Ref | Part | Value | Role |
|-----|------|------:|------|
| U12 | Murata NMA1212SC (or eq.) | 1 W, ±12 V | isolated +12VA / GNDA / −12VA |
| U16 | 74HC139 | 3.3 V | {SELECT,ENABLE,REVERSE} → 4 switch drives + interlock |
| U15 | 74HC157 | 3.3 V | SELECT routes active boiler's POS/NEG → 2 pins; `Ē` → GND |
| U10 / U11 | PC817 (V(CEO) ≥ 24 V) | — | brew drive P / N — **the opto *is* the switch** |
| U3 / U13 | PC817 (V(CEO) ≥ 24 V) | — | steam drive P / N |
| R3 / R4 / R5 / R6 | resistor | 330 Ω | decoder output → drive-opto LED (→ ~220 Ω for low-CTR parts) |
| U8 / U14 | PC817 | — | brew sense POS / NEG (anti-parallel pair, in the rod line) |
| U17 / U9 | PC817 | — | steam sense POS / NEG |
| R19 / R18 | resistor | 47 kΩ → 3.3 V | brew sense POS / NEG pull-up, on the opto collector (= '157 input) |
| R21 / R20 | resistor | 47 kΩ → 3.3 V | steam sense POS / NEG pull-up, on the opto collector (= '157 input) |
| R8 / R7 | resistor | 4.7 kΩ | Rlim, brew / steam — ~1–2 mA probe current |
| R11 | resistor | 4.7 kΩ → 3.3 V | `ENABLE` pull-up: decoder disabled at boot |

Values are starting points — tune `Rlim` for your water hardness and the firmware
thresholds.

> **Don't collapse the four sense optos to two shared ones.** It looks tempting now
> that both bodies are commoned at `GNDA`, but the only place a shared pair can sit
> is the body return — and there the two boilers' pairs end up in parallel across
> the earth tie. That halves the current through each LED (straight into the CTR
> cliff above) and lets a fault in one channel reroute the other boiler's return
> current silently, so a wet boiler can read dry. Keep one pair per rod line.
