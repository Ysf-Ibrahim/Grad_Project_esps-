# ESP-1 Hardware Test Report

Date: 25 October 2026
Node: ESP-1 / Node 1
Scope: Initial single-node hardware, Wi-Fi, dashboard and microphone verification.

## Outcome

Initial ESP-1 testing passed, including an unclipped repeat clap recording.
This milestone validates basic microphone recording and Wi-Fi communication
for one node. It does not validate compressed-air leak detection, absolute
acoustic calibration or multi-node operation.

## Verified setup and operation

- The node firmware compiled successfully with `espressif32@7.1.3`.
- PlatformIO detected the CH343 USB serial interface on COM3; the uploader
  confirmed an ESP32-S3, verified the written data and finished with `SUCCESS`.
- Provisioning saved ID `ESP-1` and name `Node 1`. A Windows output-encoding
  error interrupted the initial confirmation. A subsequent read-only `--show`
  check using UTF-8 confirmed the saved identity and `provisioned=1`.
- Wi-Fi connection and address assignment were confirmed. The dashboard
  automatically discovered ESP-1 with `online=true` and `mic=alive`.
- A real one-second API capture completed with result `OK`; waveform, spectrum
  and spectrogram data were available.

## Recording results

Values below come from local capture statistics. Repeat-test values were also
recalculated from the WAV and matched the saved statistics. Peak uses
`peak_dbfs`; RMS uses the dashboard's DC-removed `rms_ac_dbfs`.

| Test | Local capture reference | Duration | Peak dBFS | RMS dBFS | Clipped samples | Result |
|---|---|---:|---:|---:|---:|---|
| Initial API capture | 2026-10-25_15-13-47 | 1 s | -42.0 | -59.6 | 0 | OK |
| Quiet Test | 2026-10-25_15-19-04 | 5 s | -46.8 | -63.7 | 0 | OK |
| Original Clap Test | 2026-10-25_15-19-47 | 5 s | 0.0 | -34.2 | 2 | Clipped 2 |
| Repeat: Clap Test 2 | 2026-10-25_15-28-46 | 5 s | -5.5 | -38.1 | 0 | OK |

All captures used 16 kHz, 16-bit mono audio and reported valid timing and an
alive microphone. Both clap tests set `sound_event=true`; the quiet test did
not. These results support microphone response to the reported clap tests.

In `server/analysis.py`, clipping counts samples with absolute amplitude at
least 32700. The original clap recording contained two samples at -32768,
the negative full-scale limit: two samples, not two claps. Its overloaded
peaks limit accurate peak and spectral interpretation. The repeat recording
had no samples meeting the clipping threshold and returned `OK`.

## Remaining limitations

- Tests cover one node and short recordings; a one-hour soak test remains open.
- Clap distance, strength and room conditions were not controlled or measured.
- Levels are digital dBFS, not calibrated sound-pressure levels. Microphone
  sensitivity, frequency response and absolute acoustic accuracy remain unverified.
- The 16 kHz sample rate has an 8 kHz Nyquist limit; these recordings cannot
  characterize ultrasonic signals. Compressed-air leak detection performance
  was not tested.
- Multi-node capture, synchronization, localization and ultrasonic operation
  were not tested in this milestone.

Only test findings are documented here. Credentials, network identifiers,
private addresses and audio recordings are excluded from this report.
