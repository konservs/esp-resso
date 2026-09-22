# Safety

> An espresso machine combines **mains electricity**, **water**, and
> **pressurised vessels at ~125 °C**. Treat safety as the first requirement, not
> a feature. This document describes the philosophy; it is not a substitute for
> qualified electrical work.

## Defence in depth

The firmware is the **second** line of defence. It must never be the only thing
standing between a fault and a hazard.

1. **Hardware (primary, must exist regardless of firmware):**
   - A **thermal fuse / klixon** on each boiler that physically cuts the heating
     element if it overheats — independent of the ESP32.
   - A **mechanical over-pressure / safety valve** on the steam boiler.
   - Proper mains design: correct fusing, earth/ground bonding, a contactor or
     fused path for the heating elements, adequate insulation and clearances.
   - SSRs sized for the element load with appropriate heatsinking.

2. **Firmware safety supervisor (secondary):**
   [`core/safety.c`](../components/core/src/safety.c) runs in its own
   high-priority task and **latches** a fault that forces `MACHINE_FAULT` and
   cuts all heaters. It trips on:
   - **Over-temperature** — either boiler exceeds an absolute cutoff well above
     the highest setpoint.
   - **Sensor fault** — the MAX31865 reports an open/short, *or* the reading is
     physically implausible (a near-zero resistance from an absent/silent
     front-end, which would otherwise look like ~−247 °C), so the temperature
     can't be trusted; better to stop heating than to fly blind.
   - **Heat timeout / runaway** — a heater driven continuously without the
     temperature making progress, which indicates a dry boiler, a dead element,
     or a *welded SSR contact*.

3. **Control-loop hygiene:** PID output is clamped to [0, 1]; a **per-boiler
   dry-fire interlock** keeps that boiler's heaters fully off whenever its water
   level is not confirmed present (low, filling, or a faulted level reading),
   until auto-fill covers the probe again — the proactive counterpart to the
   heat-timeout backstop; auto-fill itself is gated on the reservoir having water
   and is disabled during a fault; a **mains load guard** caps how many heater
   elements run at once so the heaters + pump can't overload the supply circuit
   (see [control.md](control.md)); and a **pump duty-cycle guard** stops a
   vibratory pump being run past its rating.

## Fail-safe defaults

- Heaters, pump and fill valves all initialise **off / de-energised**.
- A latched safety trip stays latched until an explicit operator action
  (`safety_clear` / `EV_FAULT_CLEAR`) — it never silently self-recovers.
- A faulted temperature sensor disables heating rather than guessing.

### A missing measurement has no single safe substitute

Water level is the case that makes this concrete, and it is worth stating
plainly because the tempting shortcut — seed the level "full" at boot and get on
with it — is only half safe.

"Full" *is* the safe assumption for the **fill valve**. A boiler whose level is
not yet known must not be filled: opening the valve on an unread probe admits
line pressure blind, and on a boot loop it does so once per reset. Assuming
full means the valve stays shut until the probe affirmatively says otherwise.

For the **heaters** the same assumption is the dangerous one. An unknown level
treated as "full" satisfies the dry-fire interlock, so the elements are free to
energise in a boiler that may well be empty — which is precisely the failure the
interlock exists to prevent, and it destroys the element rather than merely
making a mess. Here the safe assumption is the opposite: "empty".

No single seeded boolean can serve both. Whichever way it is seeded, one of the
two consumers is handed the answer that hurts it. So the absent measurement is
represented as its own state — `HAL_LEVEL_UNKNOWN` — and carried through to the
control loop intact, where each consumer resolves it in its own safe direction:

| Consumer | Needs to see | An unknown level means |
|---|---|---|
| Auto-fill valve | an affirmative `HAL_LEVEL_DRY` | stay shut |
| Heater (dry-fire interlock) | an affirmative `HAL_LEVEL_WET` | stay off |

Both interlocks are therefore gated on a **positive** measurement rather than on
the absence of a negative one, and an unknown level fails both closed. That is
the general rule this codebase follows for any interlock: require the
affirmative reading, never infer permission from a missing one.

`HAL_LEVEL_UNKNOWN` is produced in two places: `level_task` holds a probe there
until it has actually settled (see [level-sensing.md](level-sensing.md)), and
`control_task` treats a published state that has gone stale as untrusted the
same way. Before this was explicit, the level debouncer seeded itself "full" and
published immediately — safe for the valve, but it meant an empty boiler passed
the dry-fire interlock for the first few hundred milliseconds of every boot.

The same reasoning applies to temperature: a faulted probe disables heating
rather than substituting a plausible-looking number.

## Validation

The settings validator
([`core/settings.c`](../components/core/src/settings.c)) refuses configurations
where the safety cutoff is not safely above the highest setpoint, so a careless
menu edit can't disarm the over-temperature trip. This rule is unit-tested.

## What the firmware does NOT protect against

- A welded SSR while the ESP32 is unpowered or hung — only the hardware thermal
  fuse covers this.
- Over-pressure — only the mechanical valve covers this.
- Electrical faults, leakage, or shock — covered by correct mains design and an
  RCD/GFCI.

If you are not confident wiring mains-powered heating elements, have a qualified
person do it.
