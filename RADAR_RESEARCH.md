# Radar: what the badge can do (phase 1 research)

**Historical design research.** The implementation now uses BBFT v2 on channel 6, AP MAC identities,
raw RANGE broadcasts and median filtering. The BOSR/channel-1 proposal below is superseded; see
[README.md](README.md#ranging-compatibility) and [main/bbft.h](main/bbft.h) for the current protocol.

Goal: show other badge os badges nearby: who, how far, and if possible which direction.
Hardware facts: ESP32-S3-WROOM-1 (module PCB antenna, 2.4 GHz only), no IMU, no compass (DEVICE_INFO.md).
Optional AS5600 knob on an SAO port. ESP-IDF v6.0.2. "Checked" = read in the local IDF tree; "[n]" = source list at the end.

## 1. Wi-Fi FTM (802.11mc round-trip time)

**Supported on the S3: yes, both roles.** Checked: `SOC_WIFI_FTM_SUPPORT 1` in `soc/esp32s3/.../soc_caps.h`.

| Question | Answer |
|---|---|
| Initiator | Station interface only: `esp_wifi_ftm_initiate_session(&cfg)` with `resp_mac`, `channel`, `frm_count` (0/16/24/32/64), `burst_period`. Result arrives as `WIFI_EVENT_FTM_REPORT` (`dist_est` cm, `rtt_est`, `rtt_raw` ns, status). Checked in `esp_wifi.h`. |
| Responder | SoftAP only on ESP chips: `wifi_config.ap.ftm_responder = true` [1][3]. The initiator does **not** need to associate [3]: it only needs the AP's MAC and channel. |
| Both at once | Mode `WIFI_MODE_APSTA`: own SoftAP = responder, STA = initiator. The API docs explicitly cover "initiated on the channel the internal SoftAP is started in" (normal burst; another channel falls back to one ASAP burst). Checked. |
| Build switch | `CONFIG_ESP_WIFI_FTM_ENABLE` is **off by default** (initiator/responder sub-options default on once enabled). Checked in `esp_wifi/Kconfig`. **OS change needed** (sdkconfig.defaults, owned by idfport). |
| Calibration | Fixed offset is real and big. ESP-IDF example: read the distance at 0 m, set it with `esp_wifi_ftm_resp_set_offset(cm)` on the responder [1]. Reports for S3 responders: ~40-64 m offset at 0 m, and the offset **changed** between associated and not associated [4]; S2 pairs showed ~0 offset [4]. Espressif blamed calibration. So: measure on our badges, always in the same (unassociated) state, keep the constant in code. |
| Accuracy | S3 to S3 (SoftAP responder, unassociated, 20 MHz, 2.4 GHz): median error **~1.2 m indoors** in a hallway with one channel; worst case **~9-15 m** (multipath). Taking the **minimum RTT** over several channels cut the median to 30-70 cm and the worst case to ~2.5-10 m [3]. S2 study: ~1 m outdoors after offset, ~5 m indoors (75% < 5 m, 10% > 8 m), "far from usable in complex indoor environments" [5]. Expect ~1-2 m typical, occasional 5-10 m outliers in a crowded hall. |
| Burst size bug | S3 sessions with `frm_count` > 8 failed often (status FAIL) in IDF 4.4/5.x reports; `-c 8` worked reliably [4]. Closed without a fix. To test on v6.0.2; fall back to 8 or 16. |
| Time per measurement | 16-frame burst **~325 ms** on S3; a channel switch **~500 ms** [3]. S2: 636 ms per operation, 75.6 mA average during FTM [5]. So one peer per ~0.3-0.6 s, sequentially. |
| How many peers | One initiator session at a time (single `esp_wifi_ftm_end_session`, single report buffer). Round-robin: 8 peers ≈ 3-5 s per full sweep. A responder serving several initiators at once is undocumented: assume collisions, retry with jitter. |
| Alongside ESP-NOW | ESP-NOW runs in STA or AP mode on the current channel. With everything on one fixed channel (Pong uses 1) discovery and FTM share the radio; nothing found says they conflict, but FTM bursts will delay ESP-NOW packets for ~0.3 s. Needs a test. |
| Responder cost | The badge must run a SoftAP: it beacons every ~100 ms and phones see the SSID. Hidden SSID (`ssid_hidden=1`) should still answer FTM (the initiator addresses the MAC directly) - **unverified, test it**. SoftAP MAC = STA MAC + 1 on ESP32; announce it in the discovery packet anyway. |

## 2. RSSI as distance (ESP-NOW or BLE)

- Free: every ESP-NOW receive callback carries `info->rx_ctrl->rssi` (dBm). No OS change, unlimited peers.
- Model: `d = 10^((RSSI_1m - RSSI) / (10 n))`, `RSSI_1m` ~ -40..-50 dBm, n ~ 2 (open) to 2.7-4 (indoor) [6][7].
- How bad: reliable within ~4 m line of sight, < 25% error; beyond that, or behind people/walls, it overestimates
  badly; one wall = -6 dB at 1 m [6]. In one building RSSI at 1.5 m equalled RSSI beyond 10 m [5].
  A 6-8 dB swing (normal fading, a body in the way, the badge turned around) is a factor ~2 in distance.
  A badge worn on a chest has the wearer's body on one side: ~10-20 dB shadow behind them.
- Smoothing: per peer, take the **max** (or 80th percentile) RSSI over a 3-5 s window (the strongest packet is
  the least obstructed path, same idea as RTT-min), then an EMA. Show **buckets**, not metres:
  "here (< 2 m)", "near (2-6 m)", "around (> 6 m)", "fading" (not heard for 10 s).
- BLE: no better than ESP-NOW for RSSI, adds the BT stack and Wi-Fi/BT coexistence. Not worth it.

## 3. Direction

- **BLE direction finding (AoA/AoD, CTE): not on the S3.** Checked: `SOC_BLE_CTE_SUPPORTED` exists only for
  ESP32-C5, C61, H2, H21. It also needs an antenna array; the badge has one antenna.
- No IMU, no compass, one antenna: **the badge cannot measure a bearing by itself.**
- What is credible:
  1. **Body-shadow sweep** (fox-hunting trick): hold the badge against the chest and turn slowly; the peer is
     where RSSI peaks (your body blocks 10-20 dB behind you). The app can show live RSSI per peer and remember
     the peak. The badge cannot know which way you face, so the user marks it: turn the **AS5600 knob** as
     the bearing ("knob points where I face"), or press A at the peak. Works for one target at a time; slow.
  2. **Relative map from shared ranges**: each badge broadcasts its FTM distances to the others. With 3+
     badges in range, classical MDS/trilateration places everyone in 2D **up to rotation and mirror**. With
     1-2 m errors and peers a few metres apart, the shape is rough; it improves with more badges.
     Rotation/mirror can be fixed by one bearing from (1) or the knob. Nice demo, most code, most testing.
  3. **Walk and watch**: distance (FTM) going down while you walk = you are heading toward it. A trend arrow
     "warmer / colder" per peer is cheap and honest.
- Not credible: phase/ToF tricks with one antenna, Wi-Fi CSI direction (needs multiple antennas).

## 4. Discovery and identity

- Broadcast an ESP-NOW packet every ~1 s (+ random jitter) on a fixed channel (1, same as Pong):
  `magic "BRDR"`, version, OS version, SoftAP MAC, name (≤ 12 chars), and (option C) a short list of
  `(peer id, distance cm)`. ~250 bytes max per packet.
- Name: badge os has no name today and no keyboard. Options: a pronounceable handle derived from the MAC
  ("blue-otter-3F") with zero setup; or a name picked letter by letter with the buttons/knob and saved in NVS;
  or an OS-wide "badge name" setting (ask idfport). No personal data in the repo either way.
- Channel: all radar badges must share one channel. A badge joined to the conference Wi-Fi sits on the AP's
  channel, so the radar must not call `wifi_connect` (it does not need to).
- Power: Wi-Fi on with power save off (Pong does this; modem sleep drops packets) is ~100 mA receive; FTM
  ~75 mA average [5]. The radar is a "look now" app, not a background service: the home screen turns Wi-Fi off
  when it returns. Battery capacity is not in DEVICE_INFO.md, so no run-time figure.
- Mode change is app-side: `wifi_on()` sets STA once; the app switches to `WIFI_MODE_APSTA` and must put STA
  back before returning. Only the FTM Kconfig switch needs the OS.

## 5. What the e-paper can show

- ~0.5 s per frame, ~2 frames/s at best, ghosts where ink was erased. So: no sweeping beam, no smooth motion.
- Good: fixed rings/grid drawn once; peer dots or names moved every 2-5 s; a list sorted by distance with big
  numbers ("3.2 m") and a small trend arrow; scale 3-4 text (24-32 px) for names. `ui_wipe` every few minutes
  or on scene change to clear ghosts. Redraw only when a bucket or rounded distance changes.

## 6. Design options

**A. Nearby list (RSSI only).** ESP-NOW discovery, list of badges sorted by smoothed RSSI with buckets
("here / near / around") and signal bars; optional "find" mode for one badge: big live bar plus
body-shadow sweep hint.
+ No OS change, works with 50+ badges, cheap, small code. − No metres, distance is a rough guess, no direction.

**B. Range radar (ESP-NOW discovery + FTM).** APSTA: own hidden SoftAP answers FTM; round-robin FTM to the
~8 strongest peers, RTT-min over a few bursts, fixed calibration offset. Screen: concentric rings at 2/5/10 m,
peers as labelled dots at their distance, spread around the circle by id (the angle is not a bearing, say so
on screen), plus a sorted list. Walk-toward trend arrows.
+ Real metres, ~1-2 m typical. − OS Kconfig change; S3 offset/bug risks need hardware tests first; ~3-5 s per
sweep with 8 peers; still no true direction.

**C. Map (B + shared ranges + manual bearing).** Badges broadcast their measured distances; with 3+ badges
the app solves a 2D layout (MDS) and draws everyone as a map; the knob (or one body-shadow sweep) rotates/
flips it into the real world.
+ The only "where", the best demo. − Most code; layout is shaky with 1-2 m errors; needs 3+ badges running
radar at once; depends on B working.

Recommendation: build **A first** (it is B's discovery layer anyway), run the FTM experiments below on two
badges, then grow into **B** if the numbers hold. C only if B looks good at the con.

## 7. Hardware experiments that settle it (for the humans, each > 15 s)

1. FTM offset and spread: two badges with a test build (FTM on, APSTA, hidden SSID), at 0, 1, 3, 5, 10 m,
   30 readings each, `frm_count` 8 and 16. Gives the offset, the noise, the burst-size bug on v6.0.2, and
   whether hidden SSID works.
2. Offset stability: same at 0 m after reboot and on a second pair of badges (is one constant enough?).
3. ESP-NOW while ranging: packet loss of 1 Hz beacons during continuous FTM.
4. Body shadow: badge on chest, other badge 5 m away, RSSI facing toward vs away.

## Sources
1. ESP-IDF FTM example README (v6.0.2 tree, `examples/wifi/ftm/README.md`; also
   [GitHub](https://github.com/espressif/esp-idf/blob/master/examples/wifi/ftm/README.md)) and `esp_wifi.h`.
2. [ESP-IDF issue #9285, enabling FTM responder on a SoftAP](https://github.com/espressif/esp-idf/issues/9285)
3. Roesler et al., "Improving WiFi Ranging Through Frequency Diversity and Mobility" (HopFTM, ESP32-S3),
   [TU Berlin PDF](https://www.tkn.tu-berlin.de/bib/roesler2025improving/roesler2025improving.pdf)
4. [ESP-IDF issue #8976, ESP32-S3 FTM fails when c > 8, offsets](https://github.com/espressif/esp-idf/issues/8976)
5. "Fine Time Measurement for the Internet of Things: A Practical Approach Using ESP32" (ESP32-S2),
   [arXiv 2401.16517](https://arxiv.org/html/2401.16517v1)
6. BLE RSSI log-distance study, ELKHA Jurnal Teknik Elektro 2025,
   [record](https://garuda.kemdiktisaintek.go.id/documents/detail/5507138)
7. [ESP-AirTag distance docs (path-loss exponents)](https://grgevansdwy.github.io/ESP-AirTag/distance_8cpp.html)
