# Room Light Controller with Energy Logger

<div align="center">

[![PlatformIO](https://img.shields.io/badge/PlatformIO-ESP32S3-orange?style=flat-square&logo=platformio)](https://platformio.org/)
[![ESP32-S3](https://img.shields.io/badge/Silicon-ESP32--S3-red?style=flat-square&logo=espressif)](https://www.espressif.com/)
[![Arduino Core](https://img.shields.io/badge/Framework-Arduino-blue?style=flat-square&logo=arduino)](https://www.arduino.cc/)
[![C++](https://img.shields.io/badge/Language-C++17-blue?style=flat-square&logo=c%2B%2B)](https://isocpp.org/)
[![HTML5](https://img.shields.io/badge/Frontend-HTML5-orange?style=flat-square&logo=html5)](https://developer.mozilla.org/)
[![CSS3](https://img.shields.io/badge/Styling-CSS3-blue?style=flat-square&logo=css3)](https://developer.mozilla.org/)
<br>
[![Build Status](https://img.shields.io/github/actions/workflow/status/MATHEESHA2/Lumina-edge-esp32s3/main.yml?style=flat-square&logo=githubactions)](https://github.com/MATHEESHA2/Lumina-edge-esp32s3/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg?style=flat-square)](LICENSE)
[![LinkedIn](https://img.shields.io/badge/LinkedIn-Connect-blue?style=flat-square&logo=linkedin)](https://www.linkedin.com/in/matheesha-gamage)

</div>

**G. P. Matheesha** (BSc Eng (Hons) Computer Engineering Undergraduate)  
*Developed for Eminence 6.0*

An ESP32-S3 edge node that dims a 12 V LED strip from ambient light and occupancy, measures power and energy use, and serves a REST API for a live web dashboard.

## Features

- **Automatic mode:** brightness follows ambient light (LDR); PIR motion switches the lamp on.
- **Manual mode:** brightness slider, on/off, and a mechanical wall switch.
- **Self-calibrating LDR** range, stored in NVS.
- **Energy logging:** power, current, runtime and cumulative Wh, saved every 5 minutes.
- **Resilient networking:** non-blocking Wi-Fi reconnect; the lamp keeps working offline.
- **Secure login:** SHA-256 challenge-response, expiring tokens, lockout after 5 failed attempts.

## Architecture

```
LDR ──┐
PIR ──┼─▶ ESP32-S3 firmware ─▶ HTTP/JSON API (:80) ◀──▶ Web dashboard
Switch┘        │
               └─ PWM ─▶ TIP120 ─▶ 12 V LED strip
```

```
.
├── README.md
├── docs/wiring-schematic.png
├── firmware/
│   ├── platformio.ini
│   ├── include/
│   │   ├── config.h.example     Wi-Fi and admin credentials template
│   │   ├── settings.h           pins and tunables
│   │   ├── auth.h
│   │   └── json_util.h
│   └── src/
│       ├── main.cpp             single entry point: setup() and loop()
│       ├── auth.cpp
│       └── json_util.cpp
└── dashboard/
    ├── index.html
    ├── style.css
    ├── app.js
    └── config.example.js        API URL template
```

`main.cpp` is the only file defining `setup()` and `loop()`. Everything else is a helper module.

## Bill of materials

| Part | Notes |
|---|---|
| ESP32-S3 DevKitC-1 | Firmware target |
| 12 V 3 A PSU | Powers strip and buck converter |
| Buck converter | Set to 5.00 V |
| TIP120 NPN transistor | Low-side LED switch |
| 12 V LED strip | About 1.5 W at full brightness |
| LDR, 10 kΩ, 1 kΩ resistors | Light sensing and base resistor |
| HC-SR501 PIR sensor | 5 V supply |
| SPST switch | Manual toggle |

## 1. Hardware setup

Pinout used by the firmware:

| Signal | GPIO | Connection |
|---|---|---|
| LED PWM | 18 | 1 kΩ to TIP120 base (HIGH = lamp on) |
| LDR divider | 4 | 3V3 → LDR → node → 10 kΩ → GND |
| PIR output | 15 | HC-SR501 OUT |
| Manual switch | 17 | Switch to GND, internal pull-up |
| Status RGB LED | 48 | On-board LED (GPIO 38 on DevKitC-1 v1.1) |

Wiring steps:

1. Connect PSU 12 V+ to the buck converter input and to the LED strip positive. Join all grounds (PSU, buck, ESP32, TIP120 emitter, PIR, 10 kΩ resistor, switch).
2. **Adjust the buck converter to 5.00 V with a multimeter before connecting the ESP32.**
3. Connect buck 5 V to the ESP32 `5V` pin and to the PIR `VCC`.
4. Wire the LED strip negative to the TIP120 collector, the emitter to GND, and the base through 1 kΩ to GPIO 18.
5. Build the LDR divider on GPIO 4 as in the table.
6. Connect PIR `OUT` to GPIO 15 and the switch between GPIO 17 and GND.

`docs/wiring-schematic.png` was drawn for an ESP32-WROOM DevKit with different GPIO numbers (LDR 34, PIR 35, button 23). Follow the table above for the ESP32-S3.

## 2. Configure the firmware

```bash
cp firmware/include/config.h.example firmware/include/config.h
```

Edit `firmware/include/config.h`:

```cpp
#define WIFI_SSID      "YOUR_WIFI_SSID"
#define WIFI_PASS      "YOUR_WIFI_PASSWORD"
#define ADMIN_PASSWORD "CHANGE_ME"
```

`config.h` is git-ignored. The build stops with a clear error if it is missing. Pins and timing live in `include/settings.h`.

## 3. Build and flash

Requires [PlatformIO](https://platformio.org/install).

```bash
cd firmware
pio run -t upload
pio device monitor
```

After Wi-Fi connects, the serial monitor prints the node's IP address. Wait about 60 seconds after boot for the PIR to warm up. Send `c` over serial to reset the LDR calibration.

## 4. Run the dashboard

1. Make the node reachable, for example with ngrok:
   ```bash
   ngrok http http://<node-ip>:80
   ```
   On a local network you can use `http://<node-ip>` directly.
2. Create the dashboard config:
   ```bash
   cp dashboard/config.example.js dashboard/config.js
   ```
   Set `API_BASE` to the ngrok or LAN URL.
3. Serve it:
   ```bash
   cd dashboard
   python3 -m http.server 8080
   ```
4. Open `http://localhost:8080`. Viewing is public. Click **Sign in** and enter `ADMIN_PASSWORD` to unlock controls.

## API reference

Base URL: the node address, for example `http://<node-ip>`. All responses are JSON.

| Method | Endpoint | Auth | Description |
|---|---|---|---|
| GET | `/api/status` | None | Power, sensor and mode data |
| GET | `/api/challenge` | None | Issues a one-time nonce (60 s) |
| POST | `/api/login` | None | Exchanges a proof for a bearer token (12 h) |
| POST | `/api/control` | Bearer | Sets mode, lamp and brightness |

**Status**

```bash
curl http://<node-ip>/api/status
```

```json
{"ok":true,"online":true,"mode":"auto","brightness":120,"manualBrightness":128,
 "power":{"volts":12.00,"amps":0.0588,"watts":0.706,"avgW10":0.700,"runtimeH":0.01200,"wh":0.01500},
 "sensors":{"ldr":1800,"ambientPct":53,"pir1":false,"motion":true,"lamp":true},
 "uptimeSec":3600,"rssi":-58,"ip":"192.168.1.50"}
```

**Login**

1. `GET /api/challenge` returns `{"ok":true,"nonce":"<hex>"}`.
2. Compute `proof = SHA256( SHA256(password) + nonce )` as lowercase hex.
3. `POST /api/login` with `{"nonce":"<hex>","proof":"<hex>"}`.
4. Success returns `{"ok":true,"token":"<hex>"}`. Errors: `401` wrong password or expired nonce, `429` locked out for 30 s.

**Control**

```bash
curl -X POST http://<node-ip>/api/control \
  -H "Authorization: Bearer <token>" \
  -H "Content-Type: application/json" \
  -d '{"mode":"manual","brightness":200}'
```

| Field | Type | Description |
|---|---|---|
| `mode` | `"auto"` or `"manual"` | Optional |
| `lamp` | boolean | Optional master on/off |
| `brightness` | 0 to 255 | Optional. In manual mode, 0 turns the lamp off and above 0 turns it on |

The response is the full status object. Errors return `{"ok":false,"error":"..."}` with `400` or `401`.

## Troubleshooting

| Symptom | Check |
|---|---|
| Build error about `config.h` | Complete step 2 |
| Status LED stays red | Wi-Fi credentials, 2.4 GHz network |
| Dashboard shows "Set API_BASE" | Create `dashboard/config.js` |
| Lamp never turns on from motion | PIR warm-up (60 s), PIR wiring, mode must be Auto |
| Lamp behaviour inverted | Set `PWM_INVERTED` in `settings.h` |

## Security notes

- Never commit `config.h` or `config.js`; use a strong `ADMIN_PASSWORD`.
- The node speaks plain HTTP. Use an HTTPS tunnel for remote access.
- CORS allows all origins; restrict it to your dashboard origin for production.

---
<div align="center">
  <img src="https://komarev.com/ghpvc/?username=MATHEESHA2&repo=lumina-edge-esp32s3&color=blue&style=flat-square" alt="Repository Views" />
</div>
