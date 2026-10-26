# ESP-3 Hardware Test Report

Date: 26 October 2026
Node: ESP-3 / Node 3
Scope: Initial ESP-3 validation and three-node connectivity with sequential recording.

## Outcome

Initial ESP-3 hardware and microphone validation passed without clipping.
ESP-1, ESP-2 and ESP-3 were simultaneously online and completed separate,
sequential recordings. These tests validate basic microphone recording and
Wi-Fi communication, not compressed-air leak detection or absolute acoustic accuracy.

## Verified setup and operation

- Board: ESP32-S3; USB connection: COM5 through the CH343 serial interface,
  USB serial number `5C93109295`.
- Firmware compilation with `espressif32@7.1.3` finished with `SUCCESS`.
- The uploader identified the chip as ESP32-S3, reached 100%, verified the
  uploaded firmware hash and finished with `SUCCESS`.
- The pre-provisioning read-only check showed `provisioned=0` (unprovisioned).
  Provisioning as `ESP-3` / `Node 3` finished with `PROVISIONED` using UTF-8
  console output.
- A subsequent read-only identity check confirmed ID `ESP-3`, name `Node 3`
  and `provisioned=1`. Connection to the home Wi-Fi and address assignment
  were confirmed.
- Microphone wiring was functionally verified by the live microphone checks
  and successful recordings; the dashboard automatically discovered ESP-3
  with `online=true` and `mic=alive`.

## Recording results

All recordings below were saved locally as 16 kHz, 16-bit mono WAV audio with
matching JSON metadata. Peak uses `peak_dbfs`; RMS uses the dashboard's
DC-removed `rms_ac_dbfs`.

| Test | Local capture reference | Duration | Samples | Peak dBFS | RMS dBFS | Clipped samples | Result |
|---|---|---:|---:|---:|---:|---:|---|
| Initial API capture | 2026-10-26_10-05-01 | 1.000 s | 16,000 | -57.6 | -67.3 | 0 | OK |
| ESP-3 Quiet Test | 2026-10-26_10-07-46 | 5.000 s | 80,000 | -56.0 | -67.8 | 0 | OK |
| ESP-3 Clap Test | 2026-10-26_10-08-06 | 5.000 s | 80,000 | -22.0 | -54.1 | 0 | OK |

The Quiet and Clap WAV files passed checksum, metadata, recalculated
measurement and timing checks. Both reported an alive microphone.
Compared with the Quiet Test, the Clap Test increased peak by 34.0 dB and
DC-removed RMS by 13.7 dB. The `sound_event` flag changed from `false` to
`true`, supporting microphone response to the reported clap. Neither test
clipped; no repeat was required for these checks.

## Three-node connectivity test

Date: 26 October 2026

- ESP-1 / Node 1, ESP-2 / Node 2 and ESP-3 / Node 3 were simultaneously
  online on the same local network, with unique board IDs, different current
  IP addresses and fresh heartbeats. Distinct hardware addresses confirmed
  three active boards rather than stale pre-provisioning entries.
- All three microphones reported `alive`.
- Recordings ran sequentially in the order ESP-1, ESP-2, ESP-3. Each recording
  completed and its saved files were verified before the next began.

| Board | Local capture reference | Duration | Samples | Peak dBFS | RMS dBFS | Clipped samples | Result |
|---|---|---:|---:|---:|---:|---:|---|
| ESP-1 / Node 1 | 2026-10-26_10-13-18 | 1.000 s | 16,000 | -55.0 | -66.5 | 0 | OK |
| ESP-2 / Node 2 | 2026-10-26_10-13-19 | 1.000 s | 16,000 | -58.3 | -68.1 | 0 | OK |
| ESP-3 / Node 3 | 2026-10-26_10-13-21 | 1.000 s | 16,000 | -57.4 | -68.3 | 0 | OK |

Each capture used 16 kHz, 16-bit mono audio. The WAV and matching JSON were
saved locally, checksums matched, recalculated measurements matched the
saved statistics, and timing checks passed. All three boards remained online
at the final check. Three-node connectivity and sequential recording passed.

Simultaneous recording, clock synchronization and localization were NOT tested.

## Remaining limitations

- No absolute acoustic calibration: dBFS values are digital levels, not
  calibrated sound-pressure levels.
- Compressed-air leak detection performance was not validated.
- No synchronized multi-node recording or localization validation was performed.
- The 16 kHz sample rate has an 8 kHz Nyquist frequency; these recordings
  cannot characterize ultrasonic signals.
- One-hour soak/stability testing remains pending.
- Quiet and Clap Tests were not controlled acoustic measurements; clap
  strength, distance and room conditions were not controlled or measured.

Only test findings are documented here. Passwords, Wi-Fi network names,
private IP addresses and audio recordings are excluded from this report.
