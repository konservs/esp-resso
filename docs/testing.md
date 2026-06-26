# Testing

The portable `core/` is covered by host unit tests using
**[Unity](https://github.com/ThrowTheSwitch/Unity)** (assertions/runner) and,
for code that calls the HAL, **[fff](https://github.com/meekrosoft/fff)** (fake
functions). Both are fetched by CMake at configure time.

## Running

See [building.md](building.md). In short:

```bash
cd tests
cmake --preset host
cmake --build --preset host
ctest --preset host --output-on-failure
```

Each module gets its own test executable (one per `tests/test_*.c`), registered
with CTest so `ctest` runs them all and reports pass/fail.

## What is covered

| Suite | Module | Examples |
|-------|--------|----------|
| `test_pid` | PID | output/integrator clamping, derivative-on-measurement |
| `test_filter` | filters | EMA seeding, debounce streak, fill hysteresis |
| `test_rtd` | RTD math | PT100 at 0/50/100 °C |
| `test_boiler` | boiler | ready band, heats when cold, off when over |
| `test_brew` | brew profiles | auto stage sequence, volumetric/time stop, manual follows lever |
| `test_state_machine` | machine | brew-requires-ready guard, fault latch, no-sleep-while-brewing |
| `test_safety` | safety | over-temp/sensor-fault/heat-timeout trips, latch & clear |
| `test_settings` | settings | defaults valid, range + cutoff + profile validation |
| `test_buttons` | input | tap vs hold (+repeat), both-tap, both-hold, chord suppression |
| `test_ui` | menu | enter config, edit & dirty flag, profile cycle, exit |
| `test_hal_mock` | HAL mock demo | faking `hal_heater_*` with fff |

## Writing a new test

Because `core` modules are pure (readings/time in, decisions out), most tests
need no mocking — construct the object, drive it, assert the output. Use the
`fake_clock` helper for time-dependent behaviour:

```c
#include "unity.h"
#include "core/pid.h"

void setUp(void) {}
void tearDown(void) {}

static void test_thing(void) {
    pid_ctrl_t pid; pid_config_t cfg = { /* ... */ };
    pid_init(&pid, &cfg);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 20.0f, pid_update(&pid, 30.0f, 20.0f, 1.0f));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_thing);
    return UNITY_END();
}
```

Then add the file's base name to `CORE_TESTS` in
[`tests/CMakeLists.txt`](../tests/CMakeLists.txt).

## Mocking the HAL (task-level code)

To test glue that calls the HAL, fake the relevant functions with fff — see
[`tests/test_hal_mock.c`](../tests/test_hal_mock.c):

```c
#include "fff.h"
DEFINE_FFF_GLOBALS;
FAKE_VOID_FUNC(hal_heater_set_duty, hal_boiler_id_t, float);
/* ... call the code under test, then: */
TEST_ASSERT_EQUAL_UINT(1, hal_heater_set_duty_fake.call_count);
```

## Coverage (optional)

Configure with coverage flags and use gcov/lcov:

```bash
cd tests
cmake --preset host -DCMAKE_C_FLAGS="--coverage"
cmake --build --preset host
ctest --preset host
gcovr -r .. --html-details -o coverage.html   # or lcov/genhtml
```

## On-target tests (future)

ESP-IDF supports running Unity on the device and via QEMU/pytest-embedded. The
portable split means the bulk of logic is already covered on the host; on-target
tests would focus on the drivers (SPI timing, GPIO, I2C).
