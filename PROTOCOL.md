# AcoustiScan protocol (firmware 0.1.x)

One document for the node firmware (`src/node/`), the laptop server (`server/`),
the fake node (`tools/fake_node.py`) and the provisioning tool (`tools/provision.py`).

## 1. Serial provisioning (USB, 115200 baud, either connector)

Line based, `\n` terminated. Every command answers exactly one line starting with
`OK` or `ERR`. Commands that change state answer `ERR busy` while the node is
recording or sending.

| Command | Reply | Notes |
|---|---|---|
| `SET id=<id>` | `OK id=<id>` | `[A-Za-z0-9_-]{1,15}`; becomes hostname and capture folder |
| `SET name=<text>` | `OK name=<text>` | up to 31 printable characters, no `"` or `\` |
| `SET ssid=<text>` | `OK ssid=<text>` | Wi-Fi network name (up to 32) |
| `SET pass=<text>` | `OK pass=****` | Wi-Fi password (up to 63) |
| `SHOW` | `OK id=… name=… ssid=… pass=**** provisioned=0/1 ip=… fw=…` | staged values |
| `SAVE` | `OK saved` | writes staged values to flash (NVS) |
| `REBOOT` | `OK rebooting` | restart, new values take effect |
| `FACTORY` | `OK factory` | erases the saved config and restarts |
| `STATUS` | `OK {json}` | same JSON as the heartbeat |
| `PROBE` | `OK clk=<n> data=<n> data_high=<pct> alive=0/1` | direct GPIO probe of the mic lines |
| `SCAN` | `OK <n> networks` + one line per network | blocking Wi-Fi scan (2-3 s) |
| `LED r g b` | `OK led` | drives the RGB LED, to find out whether the board has one |
| `HELP` | `OK` + command list | |

Boot banner ends with the line `PROVISION READY`. When Wi-Fi joins, the node prints
`WIFI ip=<ip> rssi=<dBm> ssid=<ssid>`.

Unprovisioned boards use `ESP-XXXX` (last two MAC bytes) as ID and the compiled-in
credentials from `include/wifi_credentials.h`, and report `provisioned=0`.

## 2. Discovery (UDP port 5005, both directions)

- Server → broadcast every 2 s: `ACOUSTISCAN-SERVER 1`
- Node → unicast to the beacon's source IP, port 5005, immediately on each beacon and
  then every 2 s; if no beacon was heard for 10 s the node broadcasts instead.

Heartbeat / status JSON (one line, no nesting):

```json
{"id":"ESP-1","name":"East wall","ip":"172.30.40.207","port":5010,"mac":"44:B1:76:CE:D2:C4",
 "rssi":-52,"uptime_s":734,"mic":"alive","state":"idle","fw":"0.1.0",
 "heap":215000,"psram":7300000,"provisioned":1}
```

`mic` is `alive` or `flat` (from a GPIO probe run every 2 s while idle).
`state` is `idle`, `recording` or `sending`. The server treats a node as offline
after 8 s without a heartbeat and always uses the UDP source address, not `ip`.

## 3. Commands (TCP port 5010 on the node, one client at a time)

The client sends one text line. A second client while one is served gets
`ERR busy\n` and is closed.

| Command | Response |
|---|---|
| `STATUS` | `OK {json}\n` then close |
| `PROBE` | `OK clk=… data=… data_high=… alive=…\n` then close |
| `CAPTURE <seconds>` | framed transfer below, `seconds` = 1…30 |

Capture framing, all text lines `\n` terminated:

```
OK id=ESP-1 samples=160000 rate=16000 bytes=320000
PROG 500 10000            (every 500 ms while recording: elapsed ms, total ms)
PROG 1000 10000
…
STAT capture_ms=10016     (statistics computed on the node, key=value, ~20 lines)
STAT min=-2431
…
DATA 320000               (byte count, then exactly that many raw bytes:
<raw int16 little-endian mono PCM>   16 kHz, mic slot only)
END crc32=9eff007b        (CRC32 of the raw bytes, same as Python zlib.crc32)
```

`ERR <reason>\n` may appear instead of any line and ends the exchange. The node
aborts and returns to idle if the client disconnects at any point.

## 4. Storage on the laptop

`captures/<node_id>/<YYYY-MM-DD_HH-MM-SS>.wav` (16 kHz, mono, 16-bit) and a `.json`
sidecar with the label, node snapshot, node statistics and the laptop analysis.
Capture id = `<node_id>/<YYYY-MM-DD_HH-MM-SS>`.
