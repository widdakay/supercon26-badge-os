# badge os

A small operating system for the Hackaday Supercon 2026 "Billboard Badge" (ESP32-S3 with 8 MB PSRAM and a 9.7"
960x672 black/white/red e-paper), written in C on ESP-IDF. The badge boots to an iOS-style home screen of app icons.
Apps take over the badge; hold MENU + and MENU - together to get back home.

## Features
- **Home screen**: 8 app icons per page, OS version bottom right.
- **Calm partial e-paper updates**: about 0.5 s per frame, only the pixels that change move, nothing flashes.
  This is a custom waveform for the panel's two Pervasive Displays B1 controllers. The panel can't refresh faster
  than about 2 frames per second.
- **Top bar on every screen**: what each button does right now, printed beside the button; app name, username,
  OS version and battery in the middle.
- **Online update**: pulls the latest GitHub release over Wi-Fi (OTA, with rollback if the new version doesn't
  boot).
- **Badge-to-badge update**: one badge flashes its own firmware onto another over a USB-C cable (USB host mode).
- **Wi-Fi**: the radio runs ranging in the background; an app that needs it otherwise (Pong, Online update, the
  feed and weather apps) takes it over.
- **Ranging service**: BBFT v2 ESP-NOW on channel 6 interoperates with
  [Hackaday badges and C3 ftm_node](https://github.com/widdakay/hackaday-2026-badge).
  Wi-Fi FTM (802.11mc round-trip time) measures distance; raw results are broadcast to share the distance graph.
- **Settings**: saved in flash and kept across updates; edited from a phone through the Settings app.
- **Add-ons**: AS5600 magnetic knob on an SAO port (I2C 0x36) as an optional controller.

## Apps
| App | What it does |
|---|---|
| Badge | Name tag: "HELLO my name is", or your own text or picture from Settings |
| Update | Online update from GitHub, or flash another badge over USB-C |
| Pong | Two-badge Pong over ESP-NOW, buttons or knob |
| Snake | Nonstop snake with wrap-around walls |
| Life | Conway's Game of Life, 2340 cells, runs by itself |
| Arcade games | Three single-player games: falling blocks, a wave of descending aliens, and a field of drifting rocks to shoot |
| Radar | Nearby BBFT badges and C3 nodes, with distances, and a map once 3+ badges/nodes are in range |
| Source | A QR code to this repo |
| Hackaday | Hackaday's latest posts with their images, one per screen; open one to read it |
| News | BBC World headlines, same reader |
| Weather | Current weather and a 5-day forecast (Open-Meteo) for a saved city or coordinates |
| Settings | Opens a Wi-Fi hotspot "Badge-XXXX" (password `supercon`) with a settings page: username, Wi-Fi, Badge text or picture, weather place and units |

## Controls
The four buttons sit two in each top corner: MENU + above A on the left, MENU - above B on the right. The top bar
shows what each one does, right beside it. On the home screen MENU + / MENU - move left / right and A or B opens
the app. Hold MENU + and MENU - together for half a second to go back home from any app.

## Install
Download `badge_os_full.bin` from the [latest release](https://github.com/yigitabi5444/supercon26-badge-os/releases/latest)
and flash it at 0x0:
```sh
esptool --chip esp32s3 -p /dev/cu.usbmodem101 write-flash 0x0 badge_os_full.bin
```
A badge running the stock MicroPython firmware has to be put into download mode first: hold BOOT, tap RESET.
After that, use Update -> Online update, or flash other badges from one badge with Update -> Flash another badge.

## Build
ESP-IDF 6.0 (tested with v6.0.2):
```sh
idf.py build
idf.py -p /dev/cu.usbmodem101 flash
```
Online update needs Wi-Fi credentials at build time in `main/wifi_secret.h` (gitignored):
```c
#define WIFI_SSID "your-network"
#define WIFI_PASS "your-password"
```
Without the file everything else works, and Online update says it has no Wi-Fi.

Releases: pushing a `v*` tag runs `.github/workflows/release.yml`, which builds that version and attaches
`badge_os.bin` (what Online update downloads) and `badge_os_full.bin` to the GitHub release.

## Writing an app
Apps draw into a 1-bit frame buffer and call `ui_frame()`. The OS handles the panel, buttons, battery, Wi-Fi,
ranging and the knob. The whole API is in [main/badge.h](main/badge.h). Rules and the app contract are in
[AGENTS.md](AGENTS.md).

## Ranging compatibility

The ranging service speaks only **BBFT version 2**, matching
`widdakay/hackaday-2026-badge` at commit `52400b30a2a19d7a68c43b029961e3ae5a558088`.
Discovery and FTM use channel **6**; each badge's **SoftAP MAC** is both its identity and ESP-NOW sender.
Old badge-os `BOSR` discovery on channel 1 is no longer supported. Pong has its own separate protocol/channel
and still takes the radio over while playing.

- A 26-byte beacon advertises the name and sequence once per second. Names have 16 wire bytes; badge-os's
  existing UI displays at most 12 characters. An unnamed badge advertises `badge-XXXX`.
- A 43-byte RANGE packet broadcasts each completed local session's raw distance, RTT statistics, RSSI and
  status. Received successful results become directed edges in Radar. Results received from peers are not relayed.
- FTM uses 16 frames, burst period 0 and a 1 s session timeout. Displayed distances use the median of the last
  three successful sessions, with each sample expiring after 30 s; discovery expires after 10 s.
- Calibration matches Hackaday's defaults: subtract 190 cm for badge initiators, 0 cm for peers named `node-*`,
  and clamp corrected distances to at least 5 cm. Override with `cfg_set("rng_badge_cm", "0")` or
  `cfg_set("rng_node_cm", "0")` in code, then restart ranging. These NVS keys are not exposed in the phone form.
  Raw statistics are always broadcast before correction. Check bias against measured distances on real hardware.
- BBFT SYNC (type 4) packets count as peer presence; firefly LED synchronization is not implemented.
- Hackaday's Python badge service currently receives shared ranges but does not transmit its own RANGE packets;
  C3 nodes do. A complete shared map depends on which devices transmit their results and which pairs are measured.

Host checks (C compiler and Python 3):
```sh
python3 tools/test_bbft.py
cc -Imain tools/test_link.c -o /tmp/test_link && /tmp/test_link
```
The BBFT test checks exact packets against the upstream Python struct formats, rejects malformed/legacy
messages, and checks filtering, calibration, expiry and bounds under AddressSanitizer/UndefinedBehaviorSanitizer.

Before deploying, build with the project's ESP-IDF v6.0.2 and test two badges (one of each firmware) plus a
C3 node at measured distances. Check names, direct ranges, shared C3 edges, and returning from Pong/Online update
to Radar. No firmware is flashed by the host checks.

## Hardware notes
Pins, buttons, LEDs, power, the panel's refresh modes, timings, ghosting and the rules that keep it alive:
[DEVICE_INFO.md](DEVICE_INFO.md). The panel driver started in
[supercon26-badge-idf](https://github.com/yigitabi5444/supercon26-badge-idf).

Unofficial. Not affiliated with Hackaday or Pervasive Displays.
