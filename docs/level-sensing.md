# Water-level sensing (isolated H-bridge AC conductivity)

Boiler level is sensed with a **conductivity probe**: a stainless rod in the
boiler whose tip sits at the "full" line. The **boiler shell** is the second
electrode. When water rises to the tip it bridges probe→water→shell and the
resistance drops from ~open to a few kΩ. That's a single **wet / dry** threshold
per boiler — autofill runs the pump + inlet valve until the probe is covered.

The catch: you must sense this **without net DC through the water**, or you get
electrolysis (H₂/O₂), probe erosion, and scale. See [safety.md](safety.md).

## Approach: opto-isolated H-bridge on a floating 12 V rail

We drive the probe with **true AC** from an **H-bridge** and keep the whole
sensing front-end **galvanically isolated** from the ESP32 (optocouplers +
an isolated 12 V supply). This is essentially how commercial autofill
controllers work, and it's robust: 12 V gives a strong, noise-immune signal, and
isolation keeps the mains-/earth-referenced boiler off the MCU (no ground loops).

### Why two pins make AC — and why isolation is the key

A single GPIO only swings 0↔3.3 V (unipolar). Two pins driven **anti-phase**
through an H-bridge reverse the current each half-cycle, giving symmetric AC with
**zero net DC** — no electrolysis:

```
   IN_A=1, IN_B=0  →  OUT+ = +V, OUT- = 0   → current →
   IN_A=0, IN_B=1  →  OUT+ = 0,  OUT- = +V   → current ←
   IN_A=0, IN_B=0  →  bridge off (idle)        (net DC over a cycle = 0)
```

An H-bridge needs its load to **float** (both ends driven). Our boiler shell is
bonded to **earth**, which would normally pin one end. The fix is to run the
bridge from an **isolated 12 V supply**: the sensing current loop then floats
relative to the ESP32/earth, so the bridge can swing the probe both sides of the
shell. The shell stays earthed for safety; the *measurement loop* is what's
isolated.

## Circuit (dual boiler)

Both boiler shells are bonded to **earth/chassis**, so they are the *same*
electrical node — a common return. The per-boiler sense element therefore sits on
the **hot (drive) side**, in series with each probe, *before* the currents merge
at the shell. One H-bridge excites both probes; each probe has its own AC
optocoupler → its own ESP32 input.

```
   ESP32 (3.3 V)     ║ isolation ║     Isolated / floating 12 V domain
   --------------    ║  barrier  ║     -------------------------------------
   GPIO14 EXC_A ─[opto1]═════════════►  H-bridge IN_A
   GPIO2  EXC_B ─[opto2]═════════════►  H-bridge IN_B
                                            │
                                          HOT ──────┬──────────────────┐
                                                    │                  │
                                        [AC opto·brew]        [AC opto·steam]
                                                    │                  │
                                         [Rlim 4.7k]            [Rlim 4.7k]
                                                    │                  │
                                      brew probe ●               steam probe ●
                                        ) water )                  ) water )
                                      brew shell ●               steam shell ●
                                                    └────────┬─────────┘
                                                       chassis / earth
                                                             │
                                          RET ───────────────┘  (bridge return)

   GPIO35 ◄═[opto·brew Q]          GPIO36 ◄═[opto·steam Q]
            asserted = brew wet              asserted = steam wet
```

- **H-bridge** (driver IC or 4 transistors), anti-phase → symmetric AC, zero DC,
  **shared** by both probes.
- **Rlim (~4.7 kΩ)** per probe sets the current to ~1–2 mA (12 V across a few-kΩ
  water path). Low current + AC = no electrolysis, long probe life.
- **AC-input optocoupler** per probe (anti-parallel LEDs, e.g. PC814/LTV-814) in
  series with that probe's drive line — lights on **either** half-cycle when its
  water conducts; its output is a clean **digital** wet/dry back to the ESP32.
  No ADC needed, and the two boilers are read independently.
- **opto1/opto2** isolate the H-bridge inputs; the 12 V rail is isolated → the
  ESP32 is fully galvanically separated from the boiler. The single earth tie is
  `RET`↔chassis (the shells); keep it the only one.

Component values are starting points — tune `Rlim` for your water hardness and
the firmware threshold (below).

## How the firmware reads it

Implemented in [`hal_esp32_sensors.c`](../components/drivers/src/hal_esp32_sensors.c),
behind the unchanged `hal_level_present()`:

1. On a read, drive the bridge **anti-phase** for a short burst of cycles
   (`EXC_A`/`EXC_B` toggled, **never both high** — that would short the bridge).
2. Sample the probe's (digital, isolated) sense input each half-cycle and count
   how many were "conducting". `wet = hits ≥ LEVEL_WET_MIN_HITS` (calibratable).
3. Set **both** drive pins low (bridge idle) between reads — sensing is
   **pulsed**, so average current and any residual effect are negligible.
4. The control loop additionally **debounces** the result so boiling/splashing
   doesn't chatter the autofill valve.

> Prefer an H-bridge **driver IC with built-in dead-time**, or add a small
> non-overlap delay, so the two phases never conduct simultaneously. The firmware
> already orders its writes (drop one side before raising the other) and idles
> both low.

Autofill itself lives in `control_task` ([control.md](control.md)): it opens the
fill valve while the probe reads dry, gated on the reservoir having water and the
machine not being faulted (dry-fire protection).

## Reservoir

The cold **reservoir** needs no AC sensing — a **float switch** (reed + magnet)
on `GPIO39` is simplest and has zero electrolysis concern. Wire it to GND with an
external pull-up (GPIO39 is input-only and has no internal pull).

## Safety / isolation notes

- Keep the **isolation barrier** intact: do **not** join the ESP32 ground to the
  12 V sensing ground. That separation is what makes the H-bridge valid on a
  grounded shell and what protects the MCU.
- Use an **isolated** 12 V source (isolated DC-DC or separate winding).
- The probe lead is a long antenna in a hot, electrically noisy machine — the
  opto-isolated digital signal is far more robust here than an analog one.

## Pin summary

| Signal | GPIO | Role |
|--------|-----:|------|
| `PIN_LEVEL_EXC_A` | 14 | H-bridge input A (output) |
| `PIN_LEVEL_EXC_B` | 2 | H-bridge input B (output; strapping/LED, idles low) |
| `PIN_LEVEL_BREW` | 35 | Brew probe sense (opto output, digital) |
| `PIN_LEVEL_STEAM` | 36 | Steam probe sense (opto output, digital) |
| `PIN_LEVEL_RESERVOIR` | 39 | Reservoir float switch (input, ext. pull-up) |
