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
   and **Save & connect**.
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
- **mDNS** (`espresso.local`) so the dashboard has a stable name without needing
  a static IP or hunting for the DHCP address.

## Security note

The dashboard is plain HTTP with no authentication — fine on a trusted home
network, but do not expose it directly to the internet. Put it behind your
router/firewall, or add auth/TLS before remote access.
