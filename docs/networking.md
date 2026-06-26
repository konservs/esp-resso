# Wi-Fi dashboard

The ESP32 can join your Wi-Fi and serve a small live dashboard — handy for
watching warm-up, dialing in shots, and (later) logging statistics. It is
optional and isolated in `net_task` so it can never disturb control or safety.

## Enable & configure

In `idf.py menuconfig` → **ESP.Resso**:

- **Enable Wi-Fi dashboard** (on by default)
- **Wi-Fi SSID** / **Wi-Fi password**
- **Dashboard HTTP port** (default 80)

(Or edit the `CONFIG_ESPRESSO_*` defaults; see
[`main/Kconfig.projbuild`](../main/Kconfig.projbuild).)

Build & flash, then open `http://<device-ip>/` (the assigned IP is printed in
`idf.py monitor`).

## What it serves

- **`/`** — a self-contained HTML page (no external assets) that polls telemetry
  once per second and shows machine state, both boiler temperatures vs setpoints
  with duty bars, fault status, and live shot time/volume.
- **`/api/telemetry`** — a JSON snapshot:

  ```json
  {
    "state": "READY", "safety": "OK", "ready": true,
    "brew":  { "t": 92.9, "sp": 93.0, "duty": 0.12, "ok": true },
    "steam": { "t": 124.6, "sp": 125.0, "duty": 0.20, "ok": true },
    "shot":  { "ml": 0.0, "ms": 0 }
  }
  ```

The data comes from `app_get_telemetry()`
([`main/app_main.c`](../main/app_main.c)), which copies a consistent snapshot
under the lock — the web layer never reaches into control state directly.

## Extending it

`net_task` is the place for richer features without touching control/safety:

- **MQTT** publish of telemetry / shot history.
- A **shot log** (time/volume/temperature curves) and charts.
- **OTA** firmware updates (`esp_https_ota`) — the partition table already
  reserves two OTA app slots (see [`partitions.csv`](../partitions.csv)).
- Wi-Fi **provisioning** (SoftAP or BLE) instead of compiled-in credentials.

## Security note

The dashboard is plain HTTP with no authentication — fine on a trusted home
network, but do not expose it directly to the internet. Put it behind your
router/firewall, or add auth/TLS before remote access.
