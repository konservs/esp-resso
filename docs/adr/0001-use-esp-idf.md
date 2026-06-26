# ADR 0001 — Use ESP-IDF (FreeRTOS) as the firmware base

- Status: Accepted
- Date: 2026-06-26

## Context

We need an ESP32 firmware base that gives us FreeRTOS, a CMake build, the device
drivers we need (SPI, I2C, GPIO, pulse counter, Wi-Fi, NVS), and ideally a path
to testing. Candidates considered: bare `FreeRTOS-Kernel`, Arduino-ESP32,
PlatformIO, and Espressif's ESP-IDF.

## Decision

Use **ESP-IDF v5.x**.

ESP-IDF *is* an SMP port of the FreeRTOS kernel, so "start from FreeRTOS" is
satisfied without hand-porting a scheduler. It additionally provides, out of the
box: all the ESP32 peripheral drivers we use, a CMake-based build with the
`idf.py` workflow, the Unity test framework, Kconfig-based configuration, an
OTA/partition system, and Wi-Fi + an HTTP server for the dashboard.

## Consequences

- The firmware build requires ESP-IDF installed; it is not a plain CMake project
  at the root.
- We keep the build cross-platform for *host* testing by isolating control logic
  in a portable `core` library that does not depend on ESP-IDF (see ADR 0002).
- Bare FreeRTOS-Kernel was rejected: it would mean re-creating drivers and build
  tooling for no benefit. Arduino/PlatformIO were rejected to keep direct,
  low-level control and to match Espressif's first-party tooling and docs.
