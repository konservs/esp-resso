# Wi-Fi dashboard

The ESP32 can join your Wi-Fi and serve a small live dashboard — handy for
watching warm-up, dialing in shots, and (later) logging statistics. It is
optional and isolated in `net_task` so it can never disturb control or safety.

## Getting onto Wi-Fi

The device chooses credentials in this order:

1. **Compiled-in** `CONFIG_ESPRESSO_WIFI_SSID`, if you set one in menuconfig.
2. **Portal-provisioned** credentials saved in NVS (see below).
3. Otherwise it hosts a **SoftAP setup portal**.

So the recommended flow needs no rebuild to change networks — leave the SSID
blank and provision at runtime:

1. On first boot (or whenever it can't connect) the device starts a Wi-Fi
   access point named **`ESP-Resso-setup`** (password `espresso` by default).
2. Join that network from a phone or laptop. The setup page **opens
   automatically** — the device runs a captive portal (it hijacks DNS to
   `192.168.4.1` and redirects any URL to the page), so most phones show the
   "sign in to network" sheet on connect. If yours doesn't, open
   **`http://192.168.4.1/`** manually.
3. The setup page scans for nearby networks — pick yours, enter the password,
   and also set a **user** and an **admin** password for the web dashboard.
   Then **Save & connect**.
4. Credentials are written to NVS and the device reboots and joins your network.

To change networks later, click **Reconfigure Wi-Fi** on the dashboard (or call
`net_request_provisioning()`): it clears the saved credentials and reboots back
into the setup portal.

> The portal always listens on **port 80** (captive-portal detection only probes
> port 80), regardless of the dashboard port below.

## Enable & configure

In `idf.py menuconfig` → **ESP.Resso**:

- **Enable Wi-Fi dashboard** (on by default)
- **Wi-Fi SSID / password** — *optional*; leave blank to use the setup portal
- **Setup-portal AP name / password** — the `ESP-Resso-setup` AP defaults
- **Dashboard HTTP port** (default 80)
- **Use a static IP in station mode** — off = **DHCP** (default); on exposes
  **IP / Gateway / Netmask / DNS** fields for a fixed address

(Or edit the `CONFIG_ESPRESSO_*` defaults; see
[`main/Kconfig.projbuild`](../main/Kconfig.projbuild).)

Once joined, open `http://<device-ip>/` — with DHCP the assigned IP is printed
in `idf.py monitor`; with a static IP it's the address you configured.

## What it serves

- **`/`** — a self-contained HTML page (no external assets) that polls telemetry
  once per second and shows a **component self-check**: display and button-expander
  health (with live press indicators for the two buttons and the brew-lever /
  steam-knob switches — filled/outlined circles),
  reservoir, and per-boiler temperature-sensor status (value + OK, or the
  decoded MAX31865 fault) and water level (Initializing / Full / Empty /
  Sensor error / No reading), plus
  machine state, live shot time/volume, **pump duty-cycle status, both fill-solenoid
  states (Open/Closed), and the live flow rate (ml/s)**. It also renders a **live
  mirror of the OLED** on a canvas (4× zoom; the top 16 rows drawn yellow and the
  lower 48 blue, matching a two-colour panel). A `<noscript>` banner warns if
  JavaScript is disabled, since the page needs it to load and refresh (the setup
  portal shows the same warning).
- **`/api/display`** — the raw 1 bpp OLED framebuffer in panel-native page format
  (`width*height/8` = 1024 bytes for the 128×64 panel), polled a few times a
  second to drive the on-page mirror.
- **`/api/telemetry`** — a JSON snapshot:

  ```json
  {
    "state": "READY", "safety": "OK", "ready": true, "role": "admin",
    "display": true, "buttons": true,
    "btn": { "a": false, "b": false, "brew": false, "steam": false }, "reservoir": true,
    "brew":  { "t": 92.9,  "sp": 93.0,  "ok": true, "fault": 0, "level": 0 },
    "steam": { "t": 124.6, "sp": 125.0, "ok": true, "fault": 0, "level": 0 },
    "shot":  { "ml": 0.0, "ms": 0 },
    "pump":  { "cooling": false, "ms": 0 },
    "fill":  { "brew": false, "steam": false },
    "debug": { "out": "none", "ms": 0 },
    "flow":  { "mls": 0.0 }
  }
  ```

  `buttons` is the input-expander health; `btn.a`/`btn.b`/`btn.brew`/`btn.steam`
  are the live engaged states of the two UI buttons and the E61 brew-lever / steam-knob
  switches (all on the same expander), shown on the dashboard as filled/outlined
  circles next to the Buttons status. `fault` is the MAX31865 fault byte when `ok`
  is false (`255` = no SPI comms);
  `level` is the boiler's level probe on its own — free of the reservoir, the
  machine state and the fill decision: `0`=Initializing (no settled reading yet),
  `1`=Full (probe covered), `2`=Empty (probe uncovered), `3`=Sensor error (sense
  path faulted), `4`=No reading (the level task stopped publishing). Whether a
  fill is actually running is `fill.brew`/`fill.steam`, and whether there is
  water to fill *from* is `reservoir`. `pump.cooling` +
  `pump.ms` are the duty-cycle guard (rest remaining); `fill.brew`/`fill.steam`
  are the auto-fill solenoid commands; `flow.mls` is the smoothed flow rate in
  ml/s. `debug.out`/`debug.ms` report a running output test (below) — the output
  being driven and the milliseconds left on it.
- **`/api/debug/pulse?out=…`** (POST, **admin only**) — bench output test: drive
  one actuator directly for two seconds, so you can confirm a relay switches and
  that the right load is behind it. `out` is `pump`, `fill_brew`, `fill_steam`,
  or `none` to cancel early.

  ```
  POST /api/debug/pulse?out=pump    -> {"ok":true,"out":"pump","ms":2000}
  POST /api/debug/pulse?out=none    -> {"ok":true,"out":"none","ms":0}
  ```

  Refused with **409** only while a shot or backflush is running (they own the
  pump); **400** for an unknown output. The dashboard exposes these as buttons
  in the admin card, with a live countdown and a Stop button.

  **A latched safety trip does not block a pulse.** Every trip the supervisor
  raises is about temperature, and none of these outputs is a heater — gating on
  it only made the feature unavailable on the half-wired bench machine it exists
  to diagnose, which trips `SENSOR_FAULT` on every boot. This holds only while
  no heating element is drivable from here; adding one means restoring the gate.

  **The heating elements are deliberately not exposed.** The dry-fire interlock
  that keeps an element from being energised without water lives in the control
  loop, and this feature exists to bypass the control loop — so a heater button
  would be a button for destroying an element. Test the heater SSRs with the
  boilers filled and the normal PID, or with the elements disconnected.

  The duration is fixed in firmware rather than taken from the request, and the
  window is expired by the control loop rather than by whoever started it
  ([`core/debug_pulse.h`](../components/core/include/core/debug_pulse.h)): a
  client that posts and then disappears, or a Wi-Fi task that dies mid-pulse,
  cannot leave an output energised. Only one output runs at a time.

The data comes from `app_get_telemetry()`
([`main/app_main.c`](../main/app_main.c)), which copies a consistent snapshot
under the lock — the web layer never reaches into control state directly.

## Extending it

`net_task` is the place for richer features without touching control/safety:

- **MQTT** publish of telemetry / shot history.
- A **shot log** (time/volume/temperature curves) and charts.
- **OTA** firmware updates (`esp_https_ota`) — the partition table already
  reserves two OTA app slots (see [`partitions.csv`](../partitions.csv)).
- **mDNS** (`espresso.local`) so the dashboard has a stable name without needing
  a static IP or hunting for the DHCP address.

## Access control

The dashboard uses **HTTP Basic auth** with two roles, set on the setup portal:

- **`user`** — read-only: the status + diagnostics view.
- **`admin`** — everything `user` can see, plus control actions (currently
  *Reconfigure Wi-Fi*; machine settings and network re-setup land here too).

Log in with the username `user` or `admin` and the matching password. The page
hides admin-only controls when you are logged in as `user`. Passwords are stored
as salted SHA-256 hashes in NVS (key `auth`), never in clear text.

If no credentials are configured yet (e.g. a device provisioned before this
feature existed), the dashboard is **open** so you are never locked out — set
passwords by re-running the setup portal (*Reconfigure Wi-Fi*).

## Security note

Basic auth sends credentials base64-encoded over **plain HTTP (no TLS)**, so they
cross your LAN unencrypted, and the password hashing is a fixed-salt SHA-256
(not bcrypt). This is fine on a trusted home network — but do not expose the
dashboard directly to the internet. Keep it behind your router/firewall, or add
TLS and a stronger scheme before any remote access.
