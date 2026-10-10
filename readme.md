# The Loner Wardriving Project.

**A pocket-sized, local wireless survey console for Raspberry Pi.**

loner_ turns a Raspberry Pi 3 into a WPA-protected hotspot and serves a mobile-first dashboard for observing nearby Wi-Fi networks and Bluetooth devices. A second Wi-Fi adapter performs passive scans; tapping a network requests a fresh, channel-specific inspection of its broadcast information. Observations can be saved with coordinates from gpsd or an opt-in iPhone Shortcut.

Developed by [@mayshecry](https://github.com/mayshecry).

## Features

- **Phone-friendly dashboard:** connect to the Pi's `loner` Wi-Fi hotspot and open the address printed at startup. Works locally without a CDN or internet connection.
- **Passive Wi-Fi survey:** records SSID, BSSID, frequency, signal, and advertised security using a separate USB Wi-Fi radio.
- **Tap-to-inspect:** requests another passive scan on a selected AP's last-known channel and displays available beacon information, including authentication suites, ciphers, PHY generation, vendor OUIs, and management-frame protection.
- **Bluetooth discovery:** displays nearby devices and tries to resolve missing names from BlueZ's cached device list and device information. No pairing is required.
- **CSV logging:** saves Wi-Fi and Bluetooth observations in separate files.
- **Optional location:** uses a local gpsd fix or a recent location shared manually from an iPhone Shortcut.

This is a **survey tool**, not a packet sniffer, deauth tool, or Bluetooth disconnection tool. It does not join discovered networks, pair with nearby devices, or capture client traffic.

## Hardware and software

- Raspberry Pi 3 running a Raspberry Pi OS setup with **NetworkManager** managing its hotspot interface. Raspberry Pi OS Bookworm is the intended configuration.
- **Two Wi-Fi radios:** typically the Pi's built-in `wlan0` for the hotspot and a USB adapter on `wlan1` for passive scanning. One Wi-Fi radio alone is not supported by this project.
- The Pi's built-in Bluetooth controller for Bluetooth discovery. Wi-Fi surveying continues if Bluetooth is unavailable.
- `gcc`, `make`, `nmcli`, `iw`, `ip`, and `bluetoothctl`.

A TP-Link Archer T3U with an RTL8812BU chipset and the `rtw88_8822bu` driver has been used in the development setup. Interface names and adapter support can vary. Check yours with `iw dev`, `lsusb`, and `ethtool -i wlan1`. **Monitor-mode support is not required** for loner_.

Install missing dependencies on Raspberry Pi OS:

```sh
sudo apt update
sudo apt install build-essential iw network-manager iproute2 bluez rfkill
```

Changing network managers can disconnect a remotely accessed Pi. Configure NetworkManager locally first if it is not already in use.

## Quick start

Clone or download the complete project, then run **on the Pi**, from the directory containing `Makefile` and `assets/`:

```sh
make
sudo WARDRIVE_PASSWORD='choose-a-strong-password' ./wardriver
```

The hotspot password must contain **8–63 printable ASCII characters**. The default SSID is `loner`. The default interfaces are `wlan0` for the hotspot and `wlan1` for scanning. To change them:

```sh
sudo WARDRIVE_PASSWORD='choose-a-strong-password' \
  WARDRIVE_SSID='MySurveyPi' ./wardriver wlan0 wlan1
```

Connect your phone to the Pi's hotspot and open the URL printed in the terminal, often `http://10.42.0.1:8080/`. The actual address comes from NetworkManager and may differ. Keep `assets/dashboard.html` in the `assets/` directory next to the compiled binary. Use **Ctrl+C** for a normal shutdown.

If you see `Exec format error`, do not copy a binary compiled on a different architecture. Rebuild from source on the Pi:

```sh
make clean && make
```

## How it works

```text
Pi built-in Wi-Fi / wlan0 ──> WPA hotspot ──> phone dashboard
USB Wi-Fi / wlan1 ─────────> passive scan ─> Wi-Fi list and wardrive.csv
Pi built-in Bluetooth ─────> BlueZ scan ───> Bluetooth list and bluetooth.csv
gpsd or iPhone Shortcut ───> location ─────> coordinates on new CSV rows
```

At startup, loner_ temporarily marks the USB scan adapter unmanaged by NetworkManager, brings it up, and creates a temporary NetworkManager hotspot profile on the Pi's built-in radio. The Wi-Fi worker runs `iw` passive scans, then waits about 10 seconds before its next scan. The Bluetooth worker discovers devices through BlueZ and performs limited, read-only cached-name lookups when discovery does not report a name. Both workers update a shared in-memory status snapshot used by the dashboard.

A Wi-Fi row can be inspected only if its BSSID appears in the latest displayed scan. The Pi scans that network's last-known frequency again; only one inspection runs at a time. APs can change channels or leave range, so an inspection can fail without indicating that the original scan was wrong. The inspection reads advertised information; it does not connect or send Wi-Fi probe requests.

The dashboard refreshes every three seconds. The live list holds up to **256 Wi-Fi networks** and **128 Bluetooth devices** per survey cycle. Session-unique counts reset when the program restarts. Bluetooth devices that do not advertise a readable name can remain **Unknown device** even after cached lookups; privacy addresses may also rotate.

## Logs and location

The program creates these files in the directory **from which it is launched**:

| File | Contents |
| --- | --- |
| `wardrive.csv` | UTC timestamp, BSSID, SSID, frequency, signal, advertised security, optional latitude and longitude |
| `bluetooth.csv` | UTC timestamp, Bluetooth address, available name, available RSSI, optional latitude and longitude |

Fresh iPhone fixes take priority over gpsd fixes. Without either source, coordinate fields are blank. Bluetooth coordinates are approximate because they use the most recently available fix. A GPS receiver is not built into the Pi 3.

### Option A: gpsd

If gpsd is listening at `127.0.0.1:2947` and has a location fix, loner_ can use it automatically. Configuring a receiver and gpsd is separate from this project.

### Option B: iPhone GPS via Shortcuts

The local dashboard runs over plain HTTP. Safari cannot provide browser geolocation to this page without a trusted HTTPS origin. Instead, an **opt-in iPhone Shortcut** can send a location to the Pi when you run it. This is **not** unattended background tracking.

Enable the receiver with a private token of **12–64 letters, digits, underscores, or hyphens**:

```sh
sudo WARDRIVE_PASSWORD='choose-a-strong-password' \
  WARDRIVE_GPS_TOKEN='replaceWithYourPrivateToken2026' ./wardriver
```

On an iPhone connected to the hotspot, create a Shortcut with:

1. **Get Current Location**.
2. **Get Details of Locations** → **Latitude** from that location.
3. **Get Details of Locations** → **Longitude** from that location.
4. **URL** → `http://10.42.0.1:8080/api/phone-gps` (use the Pi's printed IP if different).
5. **Get Contents of URL** → **POST**, **Request Body: JSON**, with `lat` set to the Latitude result, `lon` to the Longitude result, and `token` to `WARDRIVE_GPS_TOKEN`.

A successful request returns `{"accepted":true}`. The dashboard labels the location source **iPhone**. A phone fix expires after **two minutes**; run the Shortcut again to update it. If you need continuous GPS, use a gpsd-compatible receiver or a suitable dedicated phone GPS streaming solution instead.

Keep the token private. Shortcut coordinates are sent over HTTP on the WPA-protected local hotspot, **not end-to-end HTTPS**. Anyone with hotspot access can view coordinates on the dashboard; only a client with the token can submit phone fixes.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| `Exec format error` | Build on the Pi with `make clean && make`. |
| Password rejected | Use an 8–63-character printable ASCII `WARDRIVE_PASSWORD`. |
| No `wlan1` | Check `iw dev`, `lsusb`, the USB adapter, and its driver. |
| Hotspot fails | Check `nmcli device status`; NetworkManager must manage the hotspot interface. |
| Wi-Fi inspection fails | The AP may have moved or the adapter may not allow a passive scan on that channel. |
| Bluetooth says `org.bluez.Error.NotReady` | Check the Bluetooth controller and rfkill state using the commands below. |
| Bluetooth name says Unknown device | The remote device may not advertise its name; this is not necessarily a scan failure. |
| iPhone GPS does not update | Verify the token, printed hotspot IP, Shortcut JSON keys, Wi-Fi connection, and two-minute fix lifetime. |

For a Bluetooth controller that stays unavailable:

```sh
sudo systemctl enable --now bluetooth
rfkill list bluetooth
sudo rfkill unblock bluetooth
sudo bluetoothctl power on
bluetoothctl show
```

`bluetoothctl show` should contain `Powered: yes`. loner_ attempts to power on an unpowered controller and retries a transient `NotReady` scan once, but it does not bypass an rfkill block or fix a hardware/firmware failure. Wi-Fi scanning continues if Bluetooth cannot start.

## Project layout

| Path | Purpose |
| --- | --- |
| `src/main.c`, `src/ap.c` | Startup, hotspot creation, interface preparation, and cleanup |
| `src/scan.c`, `src/inspect.c` | Periodic Wi-Fi survey and on-demand passive inspection |
| `src/bluetooth.c` | Bluetooth discovery and cached-name resolution |
| `src/gps.c`, `src/location.c` | gpsd lookup and token-protected phone-location input |
| `src/web.c`, `assets/dashboard.html` | Local web API and mobile dashboard |
| `src/util.c` | Child-process execution, UTC timestamps, and CSV formatting |
| `tests/` | Mock-based parser and API tests |

Run `make test` to check the parsers and HTTP flows on Linux. Tests do **not** verify radio or iPhone behavior on real hardware.

## Privacy and scope

The hotspot password limits access to the local dashboard; there is **no separate dashboard login**. Logs may contain nearby device identifiers and your coordinates. Newly created logs have restricted permissions, but running with `sudo` means they may be owned by root. Avoid committing `wardrive.csv`, `bluetooth.csv`, tokens, or private location data to a public repository. NetworkManager's shared-hotspot mode may give clients internet access if the Pi has an upstream connection.

On Ctrl+C or SIGTERM, loner_ stops and removes its temporary hotspot profile and restores the scan adapter's previous NetworkManager-managed state. A hard power-off or `kill -9` skips this cleanup. Survey only where collecting wireless identifiers is permitted.
