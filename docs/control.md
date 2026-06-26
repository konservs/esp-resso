# Temperature & brew control

All control logic is in `core/` and unit-tested in `tests/`.

## Dual-boiler PID

Each boiler is regulated by an independent PID ([`core/pid.c`](../components/core/src/pid.c))
wrapped by a boiler controller ([`core/boiler.c`](../components/core/src/boiler.c)).
The PID output is a normalised **heater duty in [0, 1]** driven onto the SSR via
slow-PWM.

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

## Auto-fill

`control_task` debounces each boiler's level probe and opens the fill valve when
the probe is uncovered, stopping when covered — gated on the reservoir having
water and the machine not being faulted (dry-fire protection). The generic
hysteresis helper is in [`core/filter.c`](../components/core/src/filter.c).
