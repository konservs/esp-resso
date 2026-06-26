# Building ESP.Resso

There are **two independent builds**:

| Build | What it produces | Tooling | Runs on |
|-------|------------------|---------|---------|
| **Host tests** | Native executable unit tests of the portable `core` | CMake + gcc/clang/MinGW + Ninja | Windows & Linux |
| **Firmware** | ESP32 binary you flash to the machine | ESP-IDF (CMake + Xtensa toolchain) | ESP32 |

The host build never needs ESP-IDF, and the firmware build never needs the host
test tools. Pick whichever you're working on.

---

## 1. Host unit tests (cross-platform)

These compile the hardware-independent `core` (PID, brew profiles, state
machine, safety, UI logic) and run the Unity test suite. This is the fast,
hardware-free way to develop and verify control logic.

### Prerequisites

- **CMake ≥ 3.21**
- **Ninja**
- A C11 compiler: **gcc ≥ 11** / clang / MinGW-w64
- **git** (CMake `FetchContent` downloads Unity + fff on first configure)

#### Windows

One-stop with MSYS2 (recommended):

```powershell
winget install MSYS2.MSYS2
# then in the "MSYS2 UCRT64" shell:
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
                   mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-clang
# add C:\msys64\ucrt64\bin to PATH
```

Or add tools individually: `winget install Kitware.CMake`,
`winget install Ninja-build.Ninja`, and a real MinGW-w64
(`winget install BrechtSanders.WinLibs.POSIX.UCRT.Base`).

#### Linux

```bash
sudo apt install build-essential cmake ninja-build git    # Debian/Ubuntu
```

### Build & run

From the repo root:

```bash
# Windows PowerShell
scripts\test-host.ps1

# Linux / macOS / Git Bash
scripts/test-host.sh
```

Or drive CMake directly (the presets live in `tests/`):

```bash
cd tests
cmake --preset host
cmake --build --preset host
ctest --preset host --output-on-failure
```

Build artifacts land in `build/host/` (git-ignored). A `host-release` preset
also exists for an optimised build.

---

## 2. Firmware (ESP32)

### Install ESP-IDF v5.x

- **Windows:** run the [ESP-IDF Windows installer](https://dl.espressif.com/dl/esp-idf/)
  (bundles the Xtensa toolchain, Python, CMake, Ninja). Use the "ESP-IDF
  PowerShell" / "ESP-IDF CMD" shortcut it creates.
- **Linux/macOS:**
  ```bash
  git clone -b v5.4 --recursive https://github.com/espressif/esp-idf.git
  cd esp-idf && ./install.sh esp32 && . ./export.sh
  ```

### Build, flash, monitor

From the repo root (with IDF exported):

```bash
idf.py set-target esp32      # one-time
idf.py menuconfig            # optional: Wi-Fi dashboard creds, etc. (ESP.Resso menu)
idf.py build
idf.py -p <PORT> flash monitor
```

`<PORT>` is e.g. `COM5` on Windows or `/dev/ttyUSB0` on Linux. The
`scripts/flash.*` helpers wrap the flash+monitor step.

`sdkconfig.defaults` (committed) seeds the configuration; the generated
`sdkconfig` is git-ignored. See [hardware.md](hardware.md) for the GPIO wiring
and [networking.md](networking.md) for the Wi-Fi dashboard.

---

## Troubleshooting

- **`idf.py: command not found`** — you haven't exported ESP-IDF in this shell
  (`. $IDF_PATH/export.sh`, or use the ESP-IDF shell shortcut on Windows).
- **CMake can't find a compiler / Ninja** — ensure they're on `PATH`; on Windows
  the MSYS2 UCRT64 `bin` directory must be on `PATH`.
- **FetchContent fails** — first configure needs internet to clone Unity/fff;
  afterwards it's cached under `build/`.
