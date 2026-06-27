# ESP.Resso

[![CI](https://github.com/konservs/esp-resso/actions/workflows/ci.yml/badge.svg)](https://github.com/konservs/esp-resso/actions/workflows/ci.yml)

Firmware for an **ESP32-controlled dual-boiler E61 espresso machine**, written
in C on **ESP-IDF (FreeRTOS)**.

Espresso (/ɛˈsprɛsoʊ/) is a concentrated form of coffee produced by forcing hot water under high pressure through finely ground coffee beans. Originating in Italy, espresso has become one of the most popular coffee-brewing methods worldwide.

The control logic lives in a **portable `core`** that is compiled and
unit-tested natively on **Windows and Linux** — no hardware required — and also
runs on the device. Hardware specifics sit behind a small Hardware Abstraction
Layer (HAL).

> ⚠️ **Mains voltage, hot water, pressurised boilers.** Read
> [docs/safety.md](docs/safety.md) before building hardware. The firmware is a
> *secondary* safety layer, never the only one.

## Features

- **Dual-boiler PID** temperature control (brew ~93 °C, steam ~125 °C) with
  anti-windup and derivative-on-measurement.
- **Brew profiles** (stage-based): *Manual* (you control pre-infusion with the
  E61 lever) and *Auto* (timed pre-infusion → bloom → extraction), with
  volumetric and time stops. Pressure profiling is reserved for a future sensor.
- **Independent safety supervisor**: over-temp, sensor-fault, and heater-runaway
  trips that latch and cut the heaters.
- **Boiler auto-fill** with level-probe hysteresis and dry-fire protection.
- **Modular UI**: 2-button (− / +) control with tap / hold / both-tap /
  both-hold gestures, a config menu, and a 128×64 **I2C SSD1306 OLED**.
- **Wi-Fi dashboard** (optional): live stats over HTTP + JSON telemetry.
- **Cross-platform unit tests** (Unity + CTest) for the whole control core.

## Repository layout

```
components/core/      Portable control logic (pure C, unit-tested)
components/espresso_hal/  HAL interface headers (the hardware contract)
components/drivers/    ESP32 drivers + HAL implementations (MAX31865, SSD1306, ...)
main/                  Firmware app: app_main + FreeRTOS tasks (control/safety/ui/net)
tests/                 Host unit tests (Unity/fff/CTest) + CMake presets
docs/                  Architecture, hardware, building, testing, control, safety, ...
scripts/               test-host / format / flash helpers (.ps1 + .sh)
.github/workflows/     CI: host tests (Ubuntu+Windows) · firmware build · lint
sdkconfig.defaults     Committed ESP-IDF defaults     partitions.csv  Flash layout
```

## Quick start

### Host unit tests (no hardware, no ESP-IDF)

Needs CMake ≥ 3.21, Ninja, a C11 compiler, git. See [docs/building.md](docs/building.md).

```bash
# Windows
scripts\test-host.ps1
# Linux / macOS / Git Bash
scripts/test-host.sh
```

This builds the `core` library and runs every `tests/test_*.c` suite.

### Firmware (ESP32)

Needs ESP-IDF v5.x. See [docs/building.md](docs/building.md).

```bash
idf.py set-target esp32
idf.py menuconfig          # optional: Wi-Fi dashboard, setpoints (ESP.Resso menu)
idf.py build
idf.py -p <PORT> flash monitor
```

## Documentation

| Doc | What |
|-----|------|
| [architecture.md](docs/architecture.md) | Layers, task model, data flow |
| [hardware.md](docs/hardware.md) | BOM, GPIO map, wiring, sensor notes |
| [building.md](docs/building.md) | Full build steps (host + firmware, Win + Linux) |
| [control.md](docs/control.md) | PID tuning, brew profiles, auto-fill |
| [level-sensing.md](docs/level-sensing.md) | AC conductivity probes (anti-electrolysis) |
| [ui.md](docs/ui.md) | Display + 2-button gestures + config menu |
| [networking.md](docs/networking.md) | Wi-Fi dashboard + telemetry API |
| [safety.md](docs/safety.md) | Fail-safe philosophy (read this) |
| [testing.md](docs/testing.md) | Running and writing tests |
| [adr/](docs/adr/) | Architecture decision records |

## License

MIT — see [LICENSE](LICENSE).
