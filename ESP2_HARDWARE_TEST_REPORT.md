# ESP-2 Hardware Test Report

Date: 25 October 2026
Node: ESP-2 / Node 2
Scope: Initial ESP-2 validation and two-node connectivity with sequential recording.

## Outcome

Initial ESP-2 hardware and microphone validation passed without clipping.
ESP-1 and ESP-2 also operated on the same local network and completed separate,
sequential recordings. These tests validate basic recording and Wi-Fi
communication, not compressed-air leak detection or absolute acoustic accuracy.

## Verified setup and operation

- Board: ESP32-S3; USB connection: COM4 through the CH343 serial interface.
- Firmware compilation with `espressif32@7.1.3` finished with `SUCCESS`.
- Firmware upload to COM4 finished with `SUCCESS`; uploaded data was verified.
- The pre-provisioning status check showed an unconfigured board. Provisioning
  as `ESP-2` / `Node 2` finished with `PROVISIONED` using UTF-8 console output.
- A subsequent read-only status check confirmed ID `ESP-2`, name `Node 2`,
  `provisioned=1`, successful Wi-Fi connection and address assignment.
- The dashboard detected ESP-2 with `online=true` and `mic=alive`.

## Recording results

All recordings below were saved locally as 16 kHz, 16-bit mono audio. Peak uses
`peak_dbfs`; RMS uses the dashboard's DC-removed `rms_ac_dbfs`. The Quiet and
Clap WAV files were complete, matched their saved checksums, and reproduced
the saved analysis statistics. Both reported a live microphone and valid timing.

| Test | Local capture reference | Duration | Peak dBFS | RMS dBFS | Clipped samples | Result |
|---|---|---:|---:|---:|---:|---|
| Initial API capture | 2026-10-25_16-43-31 | 1 s | -46.6 | -57.3 | 0 | OK |
| ESP-2 Quiet Test | 2026-10-25_16-49-45 | 5 s | -52.2 | -62.7 | 0 | OK |
| ESP-2 Clap Test | 2026-10-25_16-50-06 | 5 s | -2.9 | -38.4 | 0 | OK |

Compared with the Quiet Test, the Clap Test increased RMS by 24.3 dB and peak
by 49.3 dB. The `sound_event` flag changed from `false` to `true`, supporting
microphone response to the reported claps. Neither recording clipped.

## Two-node connectivity test

- ESP-1 / Node 1 and ESP-2 / Node 2 were simultaneously online on the same
  local network, with unique board IDs and different current IP addresses.
- Both microphones reported `alive`.
- ESP-1 completed a one-second capture before ESP-2's one-second capture began.
  Both recordings were saved with result `OK` and zero clipped samples.
- Local capture references: ESP-1 `2026-10-25_16-48-04` and
  ESP-2 `2026-10-25_16-48-06`.
- Simultaneous recording was NOT tested.
- The offline `ESP-E0EC` entry was a stale pre-provisioning identity of ESP-2,
  confirmed by the matching hardware address, not a third board. It was not deleted.

## Remaining limitations

- No absolute acoustic calibration: dBFS values are digital levels, not
  calibrated sound-pressure levels.
- Compressed-air leak detection performance was not validated.
- Simultaneous multi-node recording, synchronization and localization were not tested.
- The 16 kHz sample rate has an 8 kHz Nyquist limit; these recordings cannot
  characterize ultrasonic signals.
- Long-duration stability testing, including a one-hour soak test, remains pending.
- Clap distance, strength and room conditions were not controlled or measured.

Only test findings are documented here. Credentials, network names, private
addresses and audio recordings are excluded from this report.
