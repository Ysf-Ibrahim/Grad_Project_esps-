# AcoustiScan

Multi-microphone capture system: ESP32-S3 nodes with one PDM microphone each
(Knowles SPH0641LU4H-1), a laptop dashboard that records from any node over Wi-Fi
and shows statistics, waveform, spectrum and spectrogram.

## Run the dashboard

Double-click `run_dashboard.cmd` (or run `.venv\Scripts\python server\app.py`).
It opens `http://127.0.0.1:8080`. The laptop must be on the same Wi-Fi network as
the nodes (the phone hotspot for now). Nodes appear in the left column within a
few seconds of being powered.

If Windows asks whether to allow `python.exe` through the firewall, tick
**Public networks** as well as private, otherwise the nodes' heartbeats (UDP 5005)
are blocked.

To try the dashboard without any board:

```
.venv\Scripts\python tools\fake_node.py --id FAKE-1 --port 5011
```

## Add a node (one-time, over USB)

1. Plug the board into the laptop (the connector labeled COM / UART) and flash the
   node firmware:
   ```
   %USERPROFILE%\.platformio\penv\Scripts\pio.exe run -e node -t upload --upload-port COM8
   ```
2. Give it an ID and a name (the Wi-Fi name/password are taken from
   `include/wifi_credentials.h` unless `--ssid` / `--pass` are given):
   ```
   .venv\Scripts\python tools\provision.py --port COM8 --id ESP-1 --name "East wall"
   ```
   The tool saves the settings in the board's flash, reboots it and prints the IP
   address it received. From then on the board only needs 5 V from any USB supply.
3. Check it any time with `--show`, or wipe it with `--factory`.

RGB LED on the board: blue = connecting, green = online, red = microphone not
driving data, yellow = not provisioned, white blink = recording, cyan = sending.

## Wiring of each node

| Mic pin | ESP32-S3 |
|---|---|
| 3.3 | 3V3 |
| GND | GND |
| CLK | GPIO4 |
| DATA | GPIO5 |
| SEL | GND |

## Layout

| Path | What |
|---|---|
| `src/node/` | node firmware (`pio run -e node`) |
| `src/main.cpp`, `src/mic_test.cpp` | earlier bench tests (Wi-Fi hotspot test, USB microphone test) |
| `server/` | dashboard server (Flask + numpy) and the web page in `server/static/` |
| `tools/provision.py` | one-time node setup over USB |
| `tools/fake_node.py` | node simulator for developing the dashboard |
| `tools/udp_probe.py` | discovery debugging on a new network |
| `tools/mic_capture.py`, `tools/wav_fft.py` | USB capture and offline FFT tools |
| `captures/<node>/` | recordings (`.wav`) with a `.json` report each |
| `PROTOCOL.md` | serial, UDP and TCP protocol reference |

## Not yet

Time synchronization between nodes, localization, capturing from several nodes at
once, a dedicated router instead of the phone hotspot.
