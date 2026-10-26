# ESP-4 Hardware Test Report

Date: 26 October 2026
Node: ESP-4 / Node 4
Scope: Initial ESP-4 validation and four-node connectivity with sequential recording.

## Outcome

Initial ESP-4 hardware and microphone validation passed without clipping.
All four boards were simultaneously online and completed separate, sequential
recordings. These tests validate basic microphone recording and Wi-Fi
communication, not compressed-air leak detection or absolute acoustic accuracy.

## Verified setup and operation

- Board: ESP32-S3; USB connection: COM6 through the CH343 serial interface,
  USB VID/PID `1A86:55D3`, USB serial number `5CBD016129`.
- Firmware compilation with `espressif32@7.1.3` finished with `SUCCESS`.
- The uploader identified an ESP32-S3 (QFN56), revision v0.2, reached 100%,
  verified the uploaded firmware hash and finished with `SUCCESS`. It issued
  a hardware reset via the RTS pin.
- The pre-provisioning read-only check showed `provisioned=0`, automatic ID
  `ESP-5A58` and no display name. Provisioning as `ESP-4` / `Node 4` finished
  with `PROVISIONED` using UTF-8 console output.
- A subsequent read-only check confirmed ID `ESP-4`, name `Node 4` and
  `provisioned=1`. Connection to the home Wi-Fi and address assignment were
  confirmed; signal strength during provisioning was -41 dBm.
- Microphone wiring was functionally verified by live microphone checks and
  successful recordings. The dashboard automatically discovered ESP-4 with
  `online=true` and `mic=alive`.

## Recording results

All recordings below were saved locally under `captures/ESP-4/` as 16 kHz,
16-bit mono WAV audio with matching JSON metadata. Peak uses `peak_dbfs`;
RMS uses the dashboard's DC-removed `rms_ac_dbfs`.

| Test | Local capture reference | Duration | Samples | Peak dBFS | RMS dBFS | Clipped samples | Result |
|---|---|---:|---:|---:|---:|---:|---|
| Initial API capture | 2026-10-26_15-22-33 | 1.000 s | 16,000 | -50.2 | -59.9 | 0 | OK |
| ESP-4 Quiet Test | 2026-10-26_15-24-24 | 5.000 s | 80,000 | -47.8 | -62.9 | 0 | OK |
| ESP-4 Clap Test | 2026-10-26_15-24-42 | 5.000 s | 80,000 | -10.1 | -39.4 | 0 | OK |

All three WAV files passed integrity, metadata and recalculated-statistics
checks and reported an alive microphone. Initial capture timing was 1,025 ms
against 1,000 ms expected; both five-second tests reported 4,992 ms against
5,000 ms expected. All timing checks passed.

The Quiet and Clap audio CRC32 checksums matched the saved metadata:
`025c4df4` and `7bd089a0`, respectively. Compared with the Quiet Test, the
Clap Test increased peak by 37.7 dB and DC-removed RMS by 23.5 dB. The
`sound_event` flag changed from `false` to `true`, supporting microphone
response to the reported clap. Neither test clipped; no repeat was needed
for these basic checks.

## Four-node connectivity test

Date: 26 October 2026

- ESP-1 / Node 1, ESP-2 / Node 2, ESP-3 / Node 3 and ESP-4 / Node 4 were
  simultaneously online on the same local Wi-Fi network, with unique board
  IDs, distinct hardware/MAC addresses, different current IP addresses and
  fresh heartbeats. No stale pre-provisioning entries appeared in this check.
- All four microphones reported `alive`. The existing dashboard server was reused.
- Recordings ran sequentially in the exact order ESP-1, ESP-2, ESP-3, ESP-4.
  Each recording completed and its saved files were verified before the next began.

| Board | Local capture reference | Duration | Samples | Peak dBFS | RMS dBFS | Clipped samples | Capture timing | Result |
|---|---|---:|---:|---:|---:|---:|---:|---|
| ESP-1 / Node 1 | 2026-10-26_15-56-36 | 1.000 s | 16,000 | -51.4 | -61.3 | 0 | 1,025 ms | OK |
| ESP-2 / Node 2 | 2026-10-26_15-56-38 | 1.000 s | 16,000 | -53.3 | -63.8 | 0 | 1,025 ms | OK |
| ESP-3 / Node 3 | 2026-10-26_15-56-40 | 1.000 s | 16,000 | -52.8 | -61.8 | 0 | 1,025 ms | OK |
| ESP-4 / Node 4 | 2026-10-26_15-56-41 | 1.000 s | 16,000 | -47.4 | -60.9 | 0 | 992 ms | OK |

Each capture used 16 kHz, 16-bit mono audio. The WAV and matching JSON were
saved under the respective `captures/ESP-N/` directory. Audio checksums
matched, recalculated statistics matched the metadata, and timing checks
passed. All four boards remained online at the final check with heartbeats
approximately 1.3 seconds old, microphones alive and status returned to `idle`.
Four-node connectivity and sequential recording passed without warnings or failures.

Simultaneous or synchronized recording was NOT tested.

## Remaining limitations

- No absolute acoustic calibration: dBFS values are digital levels, not
  calibrated sound-pressure levels.
- Compressed-air leak detection performance was not validated.
- Simultaneous or synchronized multi-node recording was not verified.
- Sound source localization was not validated.
- The 16 kHz sample rate has an 8 kHz Nyquist frequency; ultrasonic acoustic
  characterization is not established by these recordings.
- One-hour soak testing and long-duration stability testing remain pending.
- Quiet and Clap Tests were not controlled acoustic measurements; clap
  strength, distance and room conditions were not controlled or measured.

Only test findings are documented here. Passwords, Wi-Fi network names,
private IP addresses and audio recordings are excluded from this report.
