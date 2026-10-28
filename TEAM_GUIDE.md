# AcoustiScan: Team Guide

How to set up your PC, connect each ESP32-S3 node to the network, and add it to the
dashboard. Read this first, then use [README.md](README.md) and [PROTOCOL.md](PROTOCOL.md)
as reference.

## Contents

1. [What the system is](#1-what-the-system-is)
2. [Status and what is next](#2-status-and-what-is-next)
3. [Set up your PC (once)](#3-set-up-your-pc-once)
4. [Network: connecting the ESPs and the laptop](#4-network-connecting-the-esps-and-the-laptop)
5. [Wiring and the board](#5-wiring-and-the-board)
6. [Add one ESP, step by step](#6-add-one-esp-step-by-step)
7. [Add one ESP with an AI prompt (ESP-1 to ESP-4)](#7-add-one-esp-with-an-ai-prompt-esp-1-to-esp-4)
8. [Daily use](#8-daily-use)
9. [Troubleshooting](#9-troubleshooting)
10. [Reference](#10-reference)

---

## 1. What the system is

```
  ESP-1 + mic ─┐
  ESP-2 + mic ─┤        one Wi-Fi network         ┌─ Laptop: dashboard (Flask)
  ESP-3 + mic ─┼──── (phone hotspot or router) ───┤   http://127.0.0.1:8080
  ESP-4 + mic ─┘                                  └─ records, stores, plots
```

- A **node** is one ESP32-S3-WROOM-1 N16R8 board with one Knowles SPH0641LU4H-1 PDM
  microphone.
- Each node joins the Wi-Fi, announces itself to the laptop (UDP 5005), records when the
  laptop asks (TCP 5010) and sends the audio back (16 kHz, 16-bit, mono).
- The **dashboard** is a local web page on the laptop. "Connect to the web" in this
  project means "appear in that page". The nodes do **not** need internet.
- A node is set up **once over USB** (firmware, ID, Wi-Fi name and password stored in its
  flash). After that it only needs 5 V from any USB charger or power bank.

## 2. Status and what is next

ESP-1 and ESP-2 status updated: 25 October 2026. ESP-3, ESP-4, three-node and four-node testing updated: 26 October 2026; other entries retain prior recorded status.
See [ESP-1 Hardware Test Report](ESP1_HARDWARE_TEST_REPORT.md) for verified measurements and limitations.
See [ESP-2 Hardware Test Report](ESP2_HARDWARE_TEST_REPORT.md) for ESP-2 measurements and the two-node sequential recording test.
See [ESP-3 Hardware Test Report](ESP3_HARDWARE_TEST_REPORT.md) for ESP-3 measurements, limitations and the three-node sequential recording test.
See [ESP-4 Hardware Test Report](ESP4_HARDWARE_TEST_REPORT.md) for ESP-4 measurements, limitations and the four-node sequential recording test.
See [ESP-4 Stability Test Report](ESP4_STABILITY_TEST_REPORT.md) for the single-node stability test completed on 26 October 2026 and its monitoring limitations.

| Item | State |
|---|---|
| Board joins the phone hotspot | Done |
| PDM microphone smoke test (16 kHz) | Done, passed after swapping one dead mic |
| Node firmware, provisioning tool, dashboard | Built. Firmware compiles (rebuilt cleanly on 14 Oct 2026); dashboard verified end to end with simulated nodes (`tools/fake_node.py`) |
| First real board through the dashboard | ESP-1 passed: discovered online, microphone alive, real capture OK; repeat clap test had zero clipped samples |
| Second real board through the dashboard | ESP-2 passed: flashed and provisioned, online, microphone alive; initial, quiet and clap recordings OK with zero clipped samples |
| Third real board through the dashboard | ESP-3 passed: wiring functionally verified, flashed and provisioned, automatically discovered online, microphone alive; initial, quiet and clap recordings OK with zero clipped samples |
| Fourth real board through the dashboard | ESP-4 passed: wiring functionally verified, flashed and provisioned, automatically discovered online, microphone alive; initial, Quiet and Clap Tests OK with zero clipped samples |
| Two-node connectivity and sequential recording | ESP-1 and ESP-2 simultaneously online with unique IDs and different addresses; both microphones alive; separate sequential one-second captures OK with zero clipped samples. Simultaneous recording not tested |
| Three-node connectivity and sequential recording | ESP-1, ESP-2 and ESP-3 simultaneously online with unique IDs, different addresses and fresh heartbeats; all microphones alive; sequential one-second captures in that order OK with zero clipped samples. All three remained online at the final check. Simultaneous recording, clock synchronization and localization not tested |
| Four-node connectivity and sequential recording | ESP-1, ESP-2, ESP-3 and ESP-4 simultaneously online on the same Wi-Fi network with unique IDs, distinct hardware addresses, different IP addresses and fresh heartbeats; all microphones alive; sequential one-second captures in that order OK with zero clipped samples. All four remained online with fresh heartbeats and returned to idle at the final check. Simultaneous recording, synchronization, localization and leak detection not validated |
| Network discovery check | ESP-1 automatically discovered over Wi-Fi on the tested router network; no firewall settings changed. Hotspot-specific validation remains unrecorded |
| 1 hour soak test of one node | ESP-4 passed a 91-minute 46.559-second monitored single-node stability test with zero observed API errors, offline observations, stale-heartbeat events, microphone failures or possible unexpected restarts. Both endpoint recordings passed integrity checks. PASS within the limits of the monitoring evidence; see the stability report |
| Multi-node stability | Pending. The ESP-4 stability result covers one node only; synchronization, localization and leak-detection validation remain pending |

Four real nodes online and sequential recording: completed. Next, in order: capture from several nodes at once, time sync
between nodes, localization, evaluate a dedicated deployment router/network (current tests used home Wi-Fi), 8 nodes.
The final system is meant to go to higher-rate (ultrasonic) PDM capture later; today's
capture is deliberately 16 kHz.

Node register (fill in as you go):

| Board | ID | Name | Mic wired | Flashed + provisioned | Seen in dashboard | Sticker on board |
|---|---|---|---|---|---|---|
| 1 | `ESP-1` | Node 1 | [x] | [x] | [x] | [ ] |
| 2 | `ESP-2` | Node 2 | [x] | [x] | [x] | [ ] |
| 3 | `ESP-3` | Node 3 | [x] | [x] | [x] | [ ] |
| 4 | `ESP-4` | Node 4 | [x] | [x] | [x] | [ ] |

IDs must be **unique**. Two boards with the same ID overwrite each other in the dashboard.

---

## 3. Set up your PC (once)

### 3.1 Install

| Software | Why | Notes |
|---|---|---|
| [VS Code](https://code.visualstudio.com/) | editor and terminal | |
| VS Code extension **PlatformIO IDE** (`platformio.platformio-ide`) | builds and flashes the ESP32 | VS Code offers it as a recommendation when you open the folder |
| [Python 3.14](https://www.python.org/downloads/) | dashboard and tools | tick "Add python.exe to PATH" and keep the `py` launcher. 3.14 is the only version tested |
| Claude Code extension (optional) | to use the prompts in section 7 | any coding assistant that can run terminal commands in the folder also works |
| USB data cable (USB-C) | flashing | a **charge-only cable looks like a dead board**: the LED lights but no COM port appears |

On Windows 11 the built-in serial driver worked for our boards. If a data cable still
gives no COM port, install the WCH CH343 driver (or Silicon Labs CP210x, depending on the
chip printed near the connector).

### 3.2 Get the folder

The project is not in git yet. Two ways to share it:

- **Best:** put it in a private GitHub repository. `.gitignore` already keeps out the
  Wi-Fi password, recordings, `.venv` and `.pio`.
- **Zip:** on the PC that has the folder, run this in PowerShell. It leaves out the
  Wi-Fi password, the Python environment, the build cache and the recordings, and puts
  `acoustiscan.zip` on the Desktop:

  ```powershell
  $src = 'C:\gradproject 2'
  $tmp = Join-Path $env:TEMP 'acoustiscan_share'
  robocopy $src $tmp /E /XD .venv .pio captures __pycache__ /XF wifi_credentials.h /NFL /NDL /NJH /NJS
  Compress-Archive -Path "$tmp\*" -DestinationPath "$env:USERPROFILE\Desktop\acoustiscan.zip" -Force
  ```

  Never share `.venv` (it contains paths of the original PC and will not run elsewhere)
  or `include\wifi_credentials.h` (it holds a real password).

Unzip somewhere **without** special characters if you can, for example
`C:\AcoustiScan`.

### 3.3 Open it in VS Code

1. **File > Open Folder...** and pick the folder that contains `platformio.ini`.
2. Click **Yes, I trust the authors** and install the recommended extension if asked.
3. Wait until PlatformIO finishes its first-time setup (progress in the bottom-right,
   needs internet, a few minutes). The first firmware build also downloads the ESP32
   platform and toolchains (several hundred MB).
4. Open a terminal in VS Code (**Terminal > New Terminal**, PowerShell). Paste this at the
   start of every terminal session; the commands in this guide use these two variables:

   ```powershell
   $pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
   $py  = ".\.venv\Scripts\python.exe"
   ```

### 3.4 Wi-Fi credentials file (required to build)

The firmware includes `include\wifi_credentials.h`, which is not in the repository.
Create it from the template and edit the two lines:

```powershell
Copy-Item include\wifi_credentials.example.h include\wifi_credentials.h
code include\wifi_credentials.h
```

```c
#define WIFI_SSID     "your network name"
#define WIFI_PASSWORD "your network password"
```

This is the network the nodes and the laptop will share (section 4). `provision.py` reads
these two values and writes them into each node.

### 3.5 Python environment

Either double-click `run_dashboard.cmd` (it creates `.venv`, installs the packages and
starts the dashboard), or do it by hand:

```powershell
py -3.14 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r server\requirements.txt
```

`run_dashboard.cmd` asks for Python 3.14 specifically. With another Python 3.11+ use the
manual commands (`py -3 -m venv .venv`); that is untested here.

### 3.6 Check that it all works (no board needed)

```powershell
& $pio run -e node                      # compiles the node firmware; ends with SUCCESS
& $py tools\fake_node.py --id FAKE-1    # terminal 1: a simulated node, leave it running
& $py server\app.py                     # terminal 2: starts the dashboard, opens the browser
```

`FAKE-1` should appear under **Nodes** within a few seconds. Select it, choose 1 s, press
**Start capture**: the result column says **OK**. That proves the Python environment and
the dashboard. Stop both terminals with Ctrl+C. (The firewall prompt of section 4 may
appear now; allow it.)

> The platform is not pinned in `platformio.ini`. We built with **espressif32 7.1.3**
> (Arduino core 2.0.17). If a fresh install fails to compile the microphone code, change
> `platform = espressif32` to `platform = espressif32@7.1.3`.

---

## 4. Network: connecting the ESPs and the laptop

**The rule:** the laptop and every ESP must be on the **same Wi-Fi network**. The ESPs find
the laptop by UDP broadcast, so the network must let devices talk to each other.

| Requirement | Why |
|---|---|
| **2.4 GHz** only | the ESP32-S3 cannot join 5 GHz. "SSID not found" is the usual symptom |
| Plain WPA2 password | no captive portal, no login page, no WPA2-Enterprise. University and hotel Wi-Fi usually fail this. Use a phone hotspot or your own router |
| **Client isolation / AP isolation OFF** | with it ON, devices cannot see each other and the dashboard stays empty |
| Internet not needed | a hotspot with mobile data off, or a router with no uplink, is fine |
| One dashboard laptop per network | a node answers the last laptop whose beacon it heard |

### Phone hotspot (what we use now, Samsung Galaxy S24 Ultra)

1. **Settings > Connections > Mobile Hotspot and Tethering > Mobile Hotspot > Configure.**
2. Set **Band = 2.4 GHz**. Note the network name and password.
3. Switch the hotspot on. Keep it on while testing; phones can turn a hotspot off when idle.
4. Connect the laptop to the hotspot as well.

### Router

Use a 2.4 GHz WPA2 network, make sure client isolation ("AP isolation") is off, and
connect the laptop to it.

### Windows firewall (do this once on each laptop)

The first time the dashboard runs, Windows asks whether `python.exe` may use the network.
Tick **both Private and Public networks**. Hotspots are often classed as Public, and then
the nodes' heartbeats (UDP 5005) are dropped.

If you already clicked Cancel, open PowerShell **as Administrator** and run:

```powershell
New-NetFirewallRule -DisplayName "AcoustiScan discovery (UDP 5005)" -Direction Inbound -Protocol UDP -LocalPort 5005 -Action Allow -Profile Any
```

### Check the network before blaming a board

With the laptop on the network, run `& $py tools\udp_probe.py`. It sends the beacon and
prints every node heartbeat it receives. A node line appearing within a few seconds proves
the network, the firewall and the node together.

### Changing the network later

A node remembers the network it was provisioned with. To move a node to another network,
plug it in over USB and run `provision.py` with only the new network. ID and name are
kept and no re-flash is needed. Repeat for every board.

```powershell
& $py tools\provision.py --port COM8 --ssid "new network name" --pass "new password"
```

Without `--ssid` / `--pass` the tool uses the values in `include\wifi_credentials.h`.

---

## 5. Wiring and the board

Every node is wired identically. All four share one firmware build, so **never change the
GPIO pins in the code** for one board.

| Mic pin | ESP32-S3 pin |
|---|---|
| 3.3 (VDD) | 3V3 (never 5 V) |
| GND | GND |
| CLK | GPIO4 |
| DATA | GPIO5 |
| SEL | GND (do not leave it floating) |

The board has two USB-C connectors:

| Silk-screen | Use |
|---|---|
| **COM** | use this one for flashing and provisioning (UART bridge) |
| USB | native USB. Not needed |

Plug **one** cable into **COM**. Plugging both makes the same board appear as two ports.

RGB LED colours (if your board has no RGB LED, use the serial monitor and its `STATUS`
command instead, section 6):

| Colour | Meaning |
|---|---|
| Blue | connecting to Wi-Fi |
| **Green** | online and healthy |
| Red | the microphone is not driving DATA. Check wiring, or the mic itself is dead (this happened once) |
| Yellow | online but never provisioned (still using the compiled-in network and a `ESP-XXXX` ID) |
| White blinking | recording |
| Cyan | sending audio |

---

## 6. Add one ESP, step by step

Do boards **one at a time**, so there is never any doubt about which COM port is which.
Replace `COM8` with your port and `ESP-1` / `Node 1` with the board's ID and name.

**1. Plug in** only this board, into the **COM** connector.

**2. Find the port.**

```powershell
& $pio device list
```

Look for the entry whose *Hardware ID* is `VID:PID=1A86:55D3` (CH343, our boards),
`10C4:EA60` (CP210x) or `303A:...` (native USB). Ignore Bluetooth ports. No entry usually
means a charge-only cable.

**3. Flash the node firmware.**

```powershell
& $pio run -e node -t upload --upload-port COM8
```

The first build is slow (downloads); later ones take about 20 seconds. If the upload
cannot connect, hold **BOOT**, tap **RST**, release BOOT, then run the command again.
Close any serial monitor first: it holds the port.

**4. Provision it** (stores ID, name and Wi-Fi in the board and reboots it):

```powershell
& $py tools\provision.py --port COM8 --id ESP-1 --name "Node 1"
```

Success ends with `PROVISIONED: id=ESP-1 joined '<network>' with IP <x.x.x.x>, signal <n> dBm`.
No IP after 40 s: see Troubleshooting. Other options: `--show` prints what is stored,
`--factory` wipes it, `--ssid` / `--pass` override the network.

**5. Check it in the dashboard.** Start the dashboard (`run_dashboard.cmd`). The node
appears in the left column within a few seconds, with its LED green. Select it, choose
1 s and press **Start capture**. The result column should say **OK**.

Or check from PowerShell (change `ESP-1`):

```powershell
$base = 'http://127.0.0.1:8080'
Invoke-RestMethod "$base/api/nodes" | Select-Object id, name, ip, online, mic, rssi | Format-Table
$job = Invoke-RestMethod "$base/api/capture" -Method Post -ContentType 'application/json' -Body '{"node_id":"ESP-1","seconds":1}'
do { Start-Sleep 1; $j = Invoke-RestMethod "$base/api/jobs/$($job.job_id)" } until ($j.phase -in 'done','error')
$j.message
if ($j.phase -eq 'done') { (Invoke-RestMethod "$base/api/captures/$($j.capture_id)").stats | Select-Object result, rms_ac, peak_ac, dominant_hz }
```

Healthy: `online True`, `mic alive`, `result OK`. `Mic flat` means the microphone is not
delivering data (wiring or a dead mic).

**6. Move it.** Unplug from the laptop and power it from any 5 V USB source. It reconnects
to Wi-Fi by itself and shows up in the dashboard again.

**7. Label the board** (`ESP-1`) and tick it in the register in section 2.

To watch the live log of a board: `& $pio device monitor -p COM8 -b 115200`. In that
monitor you can type `SHOW`, `STATUS`, `SCAN` (lists the Wi-Fi networks the board sees) or
`PROBE` (tests the mic lines). Close the monitor before flashing or provisioning.

---

## 7. Add one ESP with an AI prompt (ESP-1 to ESP-4)

Use these with Claude Code (or any assistant that can run terminal commands) in VS Code
with this folder open. Approve its terminal commands when it asks.

How to use:

1. Do section 3 first (the folder, VS Code, `wifi_credentials.h`).
2. Plug in **only the board that the prompt is for**, into the **COM** connector, with the
   microphone already wired (section 5).
3. Paste the prompt for that board. The assistant flashes it, provisions it, starts the
   dashboard and proves the board shows up there.
4. Do the boards in order. ESP-1 also tests the network, so do it first.

Check the final report yourself: IP address, `online`, `mic alive`, capture `OK`, and a
**green** LED.

### Prompt for ESP-1 (first board, also tests the network)

```text
Set up AcoustiScan node ESP-1 (display name "Node 1") on this PC and get it into the
dashboard. This is the first real board, so it also proves the network works.

First read TEAM_GUIDE.md (sections "Network" and "Troubleshooting") and README.md in this
folder. Exactly one board is plugged in, through the USB-C connector marked COM. Do these
steps in order and stop and tell me if one fails:

1. Find the board's serial port with `pio device list` (pio.exe is at
   %USERPROFILE%\.platformio\penv\Scripts\pio.exe). Accept hardware IDs VID:PID
   1A86:55D3, 10C4:EA60 or 303A:*; ignore Bluetooth ports. If there is not exactly one
   candidate, stop and ask me.
2. Check, without displaying the password, that include\wifi_credentials.h exists and no
   longer contains the PUT_MY_ placeholders. If not, stop and ask me for the network
   details. Tell me the SSID it contains (never the password).
3. Flash: pio run -e node -t upload --upload-port <port>
4. Provision: .venv\Scripts\python.exe tools\provision.py --port <port> --id ESP-1 --name "Node 1"
   It must end with PROVISIONED and an IP address.
5. If the dashboard is not already answering on http://127.0.0.1:8080/api/info, start it
   in the background: .venv\Scripts\python.exe server\app.py --no-browser
6. Confirm ESP-1 is listed by GET /api/nodes with online=true and mic="alive". Then take a
   1 second test capture: POST /api/capture with {"node_id":"ESP-1","seconds":1}, poll
   /api/jobs/<job_id> until it is done, read stats.result from /api/captures/<capture_id>.
   Expected result: "OK".
7. If the board joined Wi-Fi (step 4 printed an IP) but never appears in the dashboard, run
   tools\udp_probe.py and diagnose: Windows firewall for python.exe on Public networks,
   client isolation on the hotspot or router, or the laptop being on a different network.
   Report what you find; do not change firewall rules without asking me.

Rules: do not edit any firmware source file or change any GPIO pin. Use only the project's
.venv Python and the PlatformIO pio.exe. Never print or log the Wi-Fi password.

Finish with a short report: serial port, IP, RSSI, mic status, capture result. Then ask me
to confirm the board's LED is green.
```

### Prompt for ESP-2

```text
Set up AcoustiScan node ESP-2 (display name "Node 2") on this PC and add it to the
dashboard. ESP-1 is already set up and may be running; do not touch it.

First read TEAM_GUIDE.md (sections "Add one ESP, step by step" and "Troubleshooting") and
README.md in this folder. Exactly one new board is plugged in through the USB-C connector
marked COM. Do these steps in order and stop and tell me if one fails:

1. Find the new board's serial port with `pio device list` (pio.exe is at
   %USERPROFILE%\.platformio\penv\Scripts\pio.exe). Accept hardware IDs VID:PID
   1A86:55D3, 10C4:EA60 or 303A:*; ignore Bluetooth ports. If there is not exactly one
   candidate, stop and ask me.
2. Check, without displaying the password, that include\wifi_credentials.h exists and no
   longer contains the PUT_MY_ placeholders. If not, stop and ask me. Tell me the SSID it
   contains (never the password) so I can confirm it is the network ESP-1 uses.
3. Flash: pio run -e node -t upload --upload-port <port>
4. Run .venv\Scripts\python.exe tools\provision.py --port <port> --show. If the board already
   reports provisioned=1 with an id of ESP-1 or any other existing node, stop and ask me,
   so two boards never share an ID.
5. Provision: .venv\Scripts\python.exe tools\provision.py --port <port> --id ESP-2 --name "Node 2"
   It must end with PROVISIONED and an IP address that differs from ESP-1's.
6. If the dashboard is not already answering on http://127.0.0.1:8080/api/info, start it
   in the background: .venv\Scripts\python.exe server\app.py --no-browser
7. Confirm GET /api/nodes lists ESP-2 with online=true and mic="alive", and that ESP-1 is
   still listed (if ESP-1 is powered). Then take a 1 second test capture from ESP-2:
   POST /api/capture with {"node_id":"ESP-2","seconds":1}, poll /api/jobs/<job_id> until
   done, read stats.result from /api/captures/<capture_id>. Expected result: "OK".
8. If it joined Wi-Fi but does not appear in the dashboard, run tools\udp_probe.py and
   diagnose (see the Network section). ESP-1 working means the firewall is already fine,
   so look at this board's signal, ID and credentials first.

Rules: do not edit any firmware source file or change any GPIO pin. Use only the project's
.venv Python and the PlatformIO pio.exe. Never print or log the Wi-Fi password.

Finish with a short report: serial port, IP, RSSI, mic status, capture result. Then ask me
to confirm the board's LED is green.
```

### Prompt for ESP-3

```text
Set up AcoustiScan node ESP-3 (display name "Node 3") on this PC and add it to the
dashboard. ESP-1 and ESP-2 are already set up and may be running; do not touch them.

First read TEAM_GUIDE.md (sections "Add one ESP, step by step" and "Troubleshooting") and
README.md in this folder. Exactly one new board is plugged in through the USB-C connector
marked COM. Do these steps in order and stop and tell me if one fails:

1. Find the new board's serial port with `pio device list` (pio.exe is at
   %USERPROFILE%\.platformio\penv\Scripts\pio.exe). Accept hardware IDs VID:PID
   1A86:55D3, 10C4:EA60 or 303A:*; ignore Bluetooth ports. If there is not exactly one
   candidate, stop and ask me.
2. Check, without displaying the password, that include\wifi_credentials.h exists and no
   longer contains the PUT_MY_ placeholders. If not, stop and ask me. Tell me the SSID it
   contains (never the password) so I can confirm it is the network the other nodes use.
3. Flash: pio run -e node -t upload --upload-port <port>
4. Run .venv\Scripts\python.exe tools\provision.py --port <port> --show. If the board already
   reports provisioned=1 with an id of ESP-1, ESP-2 or any other existing node, stop and
   ask me, so two boards never share an ID.
5. Provision: .venv\Scripts\python.exe tools\provision.py --port <port> --id ESP-3 --name "Node 3"
   It must end with PROVISIONED and an IP address that differs from the other nodes'.
6. If the dashboard is not already answering on http://127.0.0.1:8080/api/info, start it
   in the background: .venv\Scripts\python.exe server\app.py --no-browser
7. Confirm GET /api/nodes lists ESP-3 with online=true and mic="alive", and that the
   earlier nodes that are powered are still listed. Then take a 1 second test capture
   from ESP-3: POST /api/capture with {"node_id":"ESP-3","seconds":1}, poll
   /api/jobs/<job_id> until done, read stats.result from /api/captures/<capture_id>.
   Expected result: "OK".
8. If it joined Wi-Fi but does not appear in the dashboard, run tools\udp_probe.py and
   diagnose (see the Network section). The earlier nodes working means the firewall is
   already fine, so look at this board's signal, ID and credentials first.

Rules: do not edit any firmware source file or change any GPIO pin. Use only the project's
.venv Python and the PlatformIO pio.exe. Never print or log the Wi-Fi password.

Finish with a short report: serial port, IP, RSSI, mic status, capture result. Then ask me
to confirm the board's LED is green.
```

### Prompt for ESP-4 (last board, also checks all four together)

```text
Set up AcoustiScan node ESP-4 (display name "Node 4") on this PC and add it to the
dashboard. ESP-1, ESP-2 and ESP-3 are already set up and may be running; do not touch
them.

First read TEAM_GUIDE.md (sections "Add one ESP, step by step" and "Troubleshooting") and
README.md in this folder. Exactly one new board is plugged in through the USB-C connector
marked COM. Do these steps in order and stop and tell me if one fails:

1. Find the new board's serial port with `pio device list` (pio.exe is at
   %USERPROFILE%\.platformio\penv\Scripts\pio.exe). Accept hardware IDs VID:PID
   1A86:55D3, 10C4:EA60 or 303A:*; ignore Bluetooth ports. If there is not exactly one
   candidate, stop and ask me.
2. Check, without displaying the password, that include\wifi_credentials.h exists and no
   longer contains the PUT_MY_ placeholders. If not, stop and ask me. Tell me the SSID it
   contains (never the password) so I can confirm it is the network the other nodes use.
3. Flash: pio run -e node -t upload --upload-port <port>
4. Run .venv\Scripts\python.exe tools\provision.py --port <port> --show. If the board already
   reports provisioned=1 with an id of ESP-1, ESP-2, ESP-3 or any other existing node,
   stop and ask me, so two boards never share an ID.
5. Provision: .venv\Scripts\python.exe tools\provision.py --port <port> --id ESP-4 --name "Node 4"
   It must end with PROVISIONED and an IP address that differs from the other nodes'.
6. If the dashboard is not already answering on http://127.0.0.1:8080/api/info, start it
   in the background: .venv\Scripts\python.exe server\app.py --no-browser
7. Confirm GET /api/nodes lists ESP-4 with online=true and mic="alive". Take a 1 second
   test capture from ESP-4: POST /api/capture with {"node_id":"ESP-4","seconds":1}, poll
   /api/jobs/<job_id> until done, read stats.result from /api/captures/<capture_id>.
   Expected result: "OK".
8. Final check of the whole set: list /api/nodes and report, for each of ESP-1 to ESP-4,
   whether it is online, its IP (all four must differ), RSSI and mic status. A node that
   is not powered counts as offline, so tell me which boards still need power. Then take a
   1 second capture from each online node, one after another (not at the same time), and
   report each stats.result.
9. If a node joined Wi-Fi but does not appear in the dashboard, run tools\udp_probe.py and
   diagnose (see the Network section).

Rules: do not edit any firmware source file or change any GPIO pin. Use only the project's
.venv Python and the PlatformIO pio.exe. Never print or log the Wi-Fi password.

Finish with a short report: serial port, IP, RSSI, mic status, capture result for ESP-4,
then the table from step 8. Then ask me to confirm the board's LED is green.
```

---

## 8. Daily use

1. Connect the laptop to the same network as the nodes.
2. Power the nodes (any 5 V USB supply). Wait for the green LED.
3. Double-click `run_dashboard.cmd`. The page opens at `http://127.0.0.1:8080`.
4. Select a node, choose 1, 5, 10 or 30 s, optionally add a label, press **Start capture**.
   The dashboard records one node at a time.
5. The result shows statistics, waveform, spectrum and spectrogram. The history table lists
   earlier captures of that node.

Recordings go to `captures\<node ID>\<date_time>.wav` (16 kHz, 16-bit, mono) with a `.json`
file next to each one holding the label and the numbers. Any audio editor can open the WAV.

No board at hand? `& $py tools\fake_node.py --id FAKE-1 --port 5011` simulates a node.

## 9. Troubleshooting

| Symptom | Likely cause and fix |
|---|---|
| `pio device list` shows no board | Charge-only cable, wrong connector, or a missing driver (section 3.1). Try another cable |
| Upload says the port is busy or cannot open | A serial monitor or another program holds it. Close it and retry |
| Upload cannot connect | Hold BOOT, tap RST, release BOOT, upload again |
| `provision.py`: "no reply to SHOW" | Wrong port, or the `node` firmware is not on the board (repeat step 3) |
| Provisioning ends with "did not report an IP within 40 s" | Hotspot off, wrong password, or the network is 5 GHz. In the monitor type `SCAN` to see what the board sees. The log line says why: `201` network not found, `202`/`15`/`204` wrong password |
| LED stays blue | Cannot join Wi-Fi. Same causes as above |
| LED yellow | Never provisioned. Run `provision.py` |
| LED red, node shows `mic: flat`, or capture result "Mic flat" | Microphone does not drive DATA. Re-check 3V3, GND, GPIO4, GPIO5, SEL to GND. A dead mic unit happened once; swap it |
| Node has an IP but the dashboard stays empty | Almost always firewall or client isolation. Run `tools\udp_probe.py`. Check the Windows firewall (section 4). Check the laptop is on the same network. Workaround: type the node's IP into **Add a node by IP address** |
| Node flips between online and offline | Weak signal (move closer; a RSSI worse than about -75 dBm is trouble), or two boards share one ID. Check IDs with `provision.py --show` |
| Capture fails part way or says busy | Another client is talking to that node, or the Wi-Fi dropped. Retry; check RSSI |
| `run_dashboard.cmd`: "Python 3.14 not found" | Install Python 3.14, or create `.venv` by hand (section 3.5) |
| Build fails in the microphone code on a fresh PC | Pin the platform version (note at the end of section 3.6) |
| Everything stopped after the phone was used | The hotspot probably switched off. Switch it on; the nodes reconnect on their own |

## 10. Reference

Ports and protocol

| What | Value |
|---|---|
| Dashboard | HTTP 8080 on the laptop (`127.0.0.1` only) |
| Discovery | UDP 5005, both directions |
| Node commands | TCP 5010 on each node, one client at a time |
| Node serial | 115200 baud, either connector |

Where things are

| Path | What |
|---|---|
| `src/node/` | node firmware (`pio run -e node`) |
| `server/` | dashboard: Flask server and the web page in `server/static/` |
| `tools/provision.py` | one-time node setup over USB |
| `tools/udp_probe.py` | network and discovery check |
| `tools/fake_node.py` | simulated node |
| `include/wifi_credentials.h` | network name and password (yours, never committed) |
| `captures/` | recordings (not committed) |
| `platformio.ini` | build settings. `upload_port` and `monitor_port` are pinned to `COM8` for the original bench PC |
| `PROTOCOL.md` | serial, UDP and TCP protocol |

The VS Code build and upload buttons use the port pinned in `platformio.ini`. On your PC
either change those two lines to your port or use the terminal commands in this guide,
which pass the port explicitly.

Dashboard API (used by the checks above)

| Call | Returns |
|---|---|
| `GET /api/nodes` | all nodes with `online`, `ip`, `rssi`, `mic`, `state` |
| `POST /api/nodes/add` `{"ip": "..."}` | register a node by IP |
| `POST /api/capture` `{"node_id": "ESP-1", "seconds": 1}` | `{"job_id": ...}`. Seconds: 1, 5, 10 or 30 |
| `GET /api/jobs/<job_id>` | `phase` (`connecting`, `recording`, `transferring`, `analyzing`, `done`, `error`) and `capture_id` |
| `GET /api/captures/<capture_id>` | statistics and charts of a capture |
| `GET /api/captures?node=ESP-1` | capture history |
