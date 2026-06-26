# Architecture

ESP.Resso is split so that **all decision logic is hardware-independent and
unit-testable**, while hardware specifics live behind a thin Hardware
Abstraction Layer (HAL).

```
        +----------------------------------------------------+
        |                     main/ (ESP32)                  |
        |  FreeRTOS tasks: control · safety · ui · net        |
        |  - own timing, priorities, queues, the mutex        |
        |  - read HAL inputs, call core, write HAL outputs    |
        +------------------------+---------------------------+
                                 | calls
                 +---------------v----------------+
                 |        core/ (portable C)       |   <-- unit-tested on host
                 |  pid · boiler · brew(profiles)  |
                 |  state_machine · safety         |
                 |  buttons · ui · filter · rtd    |
                 |  settings · types               |
                 +---------------+----------------+
                                 | uses types only
                 +---------------v----------------+
                 |        hal/ (interfaces)        |   <-- contract, headers only
                 +------+-----------------+--------+
                        | implemented by  | faked by
            +-----------v-----+   +-------v--------------+
            | drivers/ (ESP32)|   | tests/ fakes (fff)   |
            | MAX31865, SSR,  |   | for task-level tests |
            | pump, valves,   |   +----------------------+
            | flow, level,    |
            | SSD1306, NVS    |
            +-----------------+
```

## Layers

- **`core/`** — pure C, no FreeRTOS/ESP-IDF/hardware includes. Functions take
  readings/time in and return desired outputs. This is the heart of the machine
  and the bulk of the test suite. See [control.md](control.md), [ui.md](ui.md).
- **`hal/`** — interface headers only: the contract between `core`/tasks and the
  physical machine (temperature, heaters, pump, valves, flow, level, display,
  input, storage, time).
- **`drivers/`** — ESP32 implementations of the HAL (SPI MAX31865, SSR slow-PWM,
  GPIO, pulse counter, I2C SSD1306, NVS). Built only under ESP-IDF.
- **`main/`** — the application: brings up hardware, wires HAL→core, and runs the
  FreeRTOS tasks.

`core` does **not** depend on `hal`; it is driven entirely through function
parameters. Only the tasks and drivers touch `hal`. This is what lets `core`
compile and run natively for tests.

## Task model (firmware)

| Task | Priority | Core/Period | Responsibility |
|------|----------|-------------|----------------|
| `safety`  | highest | 1 / 50 ms  | Independent supervisor; cuts heaters and posts `EV_FAULT` on a trip. |
| `control` | high    | 1 / 100 ms | Read temps, run boiler PIDs → SSR duty, run the brew profile, auto-fill, drain the machine-event queue. |
| `ui`      | medium  | 0 / 125 ms | Poll buttons/switches → gestures/events, render the OLED. |
| `net`     | low     | 0 / —      | Wi-Fi station + HTTP dashboard (optional). |

Shared state lives in one `app_state_t` guarded by a mutex; tasks communicate
machine transitions through a FreeRTOS queue (`machine_event_t`). Sensors are
sampled outside the lock; only the brief state update is inside it.

## Data flow for a shot

1. The E61 lever closes the brew microswitch → `ui_task` posts `EV_BREW_LEVER_ON`.
2. `control_task` dispatches it; the machine enters `BREWING` (only allowed from
   `READY`).
3. `control_task` starts the active brew profile and each tick feeds it the
   monotonic clock + accumulated flow volume, applying the returned pump duty.
4. The profile ends on its stages, the volumetric target, the time cap, or the
   lever releasing (`EV_BREW_LEVER_OFF`) → back to `READY`.

See the per-topic docs: [control.md](control.md), [safety.md](safety.md),
[ui.md](ui.md), [networking.md](networking.md), [hardware.md](hardware.md), and
the decision records under [adr/](adr/).
