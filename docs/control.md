# Temperature & brew control

All control logic is in `core/` and unit-tested in `tests/`.

## Dual-boiler PID

Each boiler is regulated by an independent PID ([`core/pid.c`](../components/core/src/pid.c))
wrapped by a boiler controller ([`core/boiler.c`](../components/core/src/boiler.c)).
The PID output is **one normalised heater duty in [0, 1] per boiler**, realised as
a slow-PWM window on that boiler's heater element(s) — see
[Heater elements & SSR drive](#heater-elements--ssr-drive) below.

Features that matter for a boiler:

- **Output clamping** to [0, 1] — a heater cannot do negative work.
- **Integrator clamping (anti-windup)** — prevents the integral term winding up
  while the boiler heats from cold, which would cause a large overshoot.
- **Derivative on measurement** — avoids a derivative "kick" when you change the
  setpoint in the menu.

### Tuning

Defaults live in `settings_load_defaults()`
([`core/settings.c`](../components/core/src/settings.c)) and are intentionally
conservative. To tune for your machine:

1. Start with `ki = kd = 0` and raise `kp` until the temperature oscillates
   slightly around the setpoint, then back off ~30%.
2. Add `ki` to remove steady-state offset (the gap between setpoint and settled
   temperature). Keep `integ_max` modest so recovery from cold doesn't overshoot.
3. Add a little `kd` to damp overshoot after group flushes / shots.

The brew boiler and steam boiler tune separately — the steam boiler is larger
and slower, so it usually wants lower gains.

## Mains load guard

Each boiler has **two 400 W elements** (lower + upper), so all four together draw
**1600 W**. A 120 V / 15 A receptacle only carries **~1440 W** continuously, and
the pump (~52 W) and electronics sit on top of that — running all four would trip
the breaker. [`core/load_guard.c`](../components/core/src/load_guard.c) caps the
number of elements energised at once to `settings.max_active_heaters` (**3** by
default → 3 × 400 W = 1200 W, leaving headroom).

Allocation follows how the machine is used, brew boiler first:

- **Warming up:** the brew boiler takes **both** its elements (fastest heat-up);
  the steam boiler is held to its **lower element only** → brew-LO + brew-HI +
  steam-LO.
- **Brew at temperature:** the brew boiler holds on its **lower element only** (a
  PID trim needs little power), freeing a slot so steam runs **both** → brew-LO +
  steam-LO + steam-HI.

Either way at most three elements run, and the guard re-decides every control
cycle, so if the brew boiler drops out of its band (e.g. a cold-water refill mid
shot) it immediately reclaims priority. The cap is enforced at the level of
*enabled* elements: a held-off element is driven to 0 duty, so it can never be on
even for a PWM window, giving a hard ceiling on simultaneous current.

The elements are addressed individually through the HAL
(`hal_heater_set_duty(HAL_HEATER_BREW_LO, …)` etc.); the guard is a pure function
and is unit-tested in [`tests/test_load_guard.c`](../tests/test_load_guard.c).
Set `max_active_heaters` to **4** to lift the cap — e.g. on a 230 V circuit that
can comfortably carry all four elements.

### Heater elements & SSR drive

Each boiler has two heating elements — a **lower (LO)** and an **upper (HI)** — and
**each is on its own zero-cross solid-state relay**, switched active-high through
the ULN2003 buffer at 12 V. The four SSR pins (`PIN_SSR_BREW_LO/HI`,
`PIN_SSR_STEAM_LO/HI`) are listed in
[`pins.h`](../components/drivers/include/drivers/pins.h) / [hardware.md](hardware.md).

The LO and HI SSRs are **not** wired together and are not a single logical heater:

- **The PID computes one duty per boiler.** The load guard then maps that duty
  onto the boiler's elements — **LO is the primary element** (filled first), **HI
  is a boost** added only while the boiler is warming and the load budget allows.
  So a boiler running both elements drives them at the **same** duty (mirrored),
  and a boiler running one always uses **LO**.
- **Drive is slow-PWM.** One periodic timer runs a shared 1 s window (100 steps ×
  10 ms) in [`hal_esp32_actuators.c`](../components/drivers/src/hal_esp32_actuators.c);
  each element switches according to *its own* commanded duty. The windows are
  phase-aligned (one step counter), and each on/off is meant to land on a mains
  zero-crossing — drive the transitions from a zero-cross interrupt for tighter
  synchronisation.
- **Net effect:** holding temperature modulates a single 400 W element (finer
  control and lower peak current than pulsing 800 W), while a cold start adds the
  second element for 800 W of heat-up power. This is exactly what the two
  per-boiler squares on the display show (empty = element off, filled = driven).
- **Boot safety:** every ULN2003 heater input needs an external pulldown so the
  SSRs stay off during the reset window — mandatory on GPIO 14 (steam-HI), which
  idles with a weak internal pull-up. See [hardware.md](hardware.md) and
  [safety.md](safety.md).

## Brew profiles

The shot is described by a **profile of stages**
([`core/brew.c`](../components/core/src/brew.c)). The E61 lever microswitch
starts/stops the shot; the controller commands only the **pump** (the group's
3-way valve is mechanical).

Each stage has a pump mode (`OFF` / `DUTY` / future `PRESSURE`) and an end
condition (`TIME` / `VOLUME` / `MANUAL`). Global stops apply across all stages:
a **volumetric target** and a **max-time cap**.

Built-in profiles (built from the user-editable `brew_params_t`):

- **Manual** — a single full-pump stage that ends only when the lever is
  released (or a cap trips). *You* control pre-infusion with the lever.
- **Auto** — pre-infusion (gentle pump, timed) → bloom/hold (pump off, timed) →
  extraction (full pump), with the volumetric/time stops.

Select the profile from the on-machine menu ([ui.md](ui.md)) or `menuconfig`
defaults.

### Adding a profile / pressure profiling

Add stages in `brew_profile_build()` (or build a custom `brew_profile_t`). When
a pressure transducer is fitted, implement the `BREW_PUMP_PRESSURE` branch in
`brew_update()` with a pump PID targeting `stage.pressure_bar`. Nothing else in
the engine needs to change.

## Pump duty cycle

The vibratory pump (an **Ulka EFX5**) is rated for an intermittent duty cycle —
**2 min ON / 1 min OFF** — not continuous running; overrun cooks the solenoid
coil. A single shot is well inside the rating, but back-to-back shots or a long
extraction/backflush add up, so
[`core/pump_guard.c`](../components/core/src/pump_guard.c) tracks the pump's
accumulated run time as a **leaky-bucket thermal model**:

- Running fills the bucket at 1 ms/ms, capped at `on_max_ms` (2 min).
- Resting drains it at `on_max_ms / off_min_ms` (2×), so emptying a full bucket
  takes exactly `off_min_ms` (1 min) — matching the datasheet ratio.

When the bucket fills it trips a **cooling lock-out** and stays locked until it
has fully drained (a full rest). The guard also **starts locked** at power-up:
the firmware can't know how hard the pump was worked before this boot, so it
assumes a full bucket and holds off even the first shot until a full rest has
elapsed. Idle time — including the several-minute boiler warm-up — drains the
bucket, so a cold start rarely actually waits, while a warm restart straight
after heavy use correctly asks the user to wait.

The rating lives in `settings.pump` (`pump_guard_config_t`); `on_max_ms = 0`
disables the guard (e.g. for a rotary pump), and an enabled guard is required to
allow **at least one minute** of run capacity (validated in `settings.c`) so the
startup cooldown can never gate a shot for longer than the pump can even run.

`control_task` advances the model every cycle with the actual pump command and,
before it processes the lever, calls `machine_set_pump_ready()` so the
`READY → BREWING` (and `→ BACKFLUSH`) transition is **held off while the pump is
resting**. The machine stays in `READY` and the display / dashboard show
**"Pump Cooling"** with a countdown; releasing and re-engaging the lever after
the cooldown starts the shot. Any pump-on time counts as full load regardless of
the commanded duty — the conservative, pump-protecting choice.

## Auto-fill

`control_task` debounces each boiler's level probe and opens the fill valve when
the probe is uncovered, stopping when covered — gated on the reservoir having
water and the machine not being faulted (dry-fire protection). The generic
hysteresis helper is in [`core/filter.c`](../components/core/src/filter.c).

**Dry-fire interlock.** The same debounced level also gates the *heaters*: a
boiler is only heated while its probe reads covered. If it reads low/uncovered
(including while filling) or the level read is faulted (`LVL_ERROR`), that
boiler's heaters are forced fully off until auto-fill restores the level — so an
empty or unreadable boiler is never energised. This is proactive; the safety
supervisor's heat-timeout ([safety.md](safety.md)) is only the slow backstop.
