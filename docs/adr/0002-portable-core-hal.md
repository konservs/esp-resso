# ADR 0002 — Portable core behind a HAL

- Status: Accepted
- Date: 2026-06-26

## Context

We want to (a) develop and test the machine's control logic quickly without
flashing hardware, and (b) build the project on both Windows and Linux. Embedded
code that calls hardware/RTOS APIs directly is slow to test and not portable.

## Decision

Split the codebase into layers:

- **`core/`** — all decision logic (PID, boiler/brew control, brew profiles,
  state machine, safety supervisor, button gestures, UI/menu, filters, RTD
  math). Pure C11, no FreeRTOS/ESP-IDF/hardware includes. Driven entirely by
  function parameters (readings + time in, desired outputs out).
- **`hal/`** — interface headers: the contract between the logic and the device.
- **`drivers/`** — ESP32 implementations of the HAL.
- **`main/`** — FreeRTOS tasks that own timing/concurrency and wire HAL → core.

`core` does **not** depend on `hal`. Its `CMakeLists.txt` is "dual-mode": under
ESP-IDF it registers as a component; under a plain CMake host build it compiles
as a static library the unit tests link against.

## Consequences

- The bulk of the logic is unit-tested natively with Unity/CTest on Windows and
  Linux, with no hardware and no ESP-IDF — fast feedback and CI coverage.
- Hardware specifics (sensor type, pin map, display) change in one layer without
  touching the tested logic; the input scheme and display are swappable.
- Slight indirection cost: tasks must marshal data between the HAL and `core`.
  This is a deliberate, cheap trade for testability and portability.
- Code that *does* call the HAL (task glue) is tested by faking the HAL with fff.
