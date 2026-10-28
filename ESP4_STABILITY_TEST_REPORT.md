# ESP-4 Stability Test Report

Date: 26 October 2026
Node: ESP-4 / Node 4
Scope: Single-node powered-on Wi-Fi and microphone-health stability test with manual endpoint recordings.

## Outcome

PASS within the limits of the monitoring evidence. ESP-4 completed 5,506.559
seconds (91 minutes 46.559 seconds) of monitoring, exceeding the required
3,600 seconds. No API errors, offline observations, stale heartbeats,
microphone failures or possible unexpected restarts were observed. Both
manual endpoint recordings passed integrity and audio-statistics checks.
This is a single-node stability result, NOT a four-node stability test.

## Method and evidence

- The monitoring session was collected using an earlier version of
  `tools/soak_monitor.py`. After the session, uptime-stall detection was
  improved and validated using simulated observations. The updated detection
  was not running during the completed session. Independent read-only
  examination of the original saved uptime observations found normal uptime
  progression.
- Local session reference: `20261026T164952+0400_487bb7ce`. Its JSONL log and
  final text summary were inspected; the earlier interrupted session and
  25-second preflight were excluded.
- `tools/soak_monitor.py` polled the existing dashboard approximately once
  per second, using monotonic elapsed time and Dubai timestamps (UTC+04:00).
  Recordings were triggered manually through the dashboard.
- Wall-clock timestamps run from 16:49:52.619 to 18:21:41.124 on the test date.
  The monotonic duration is authoritative for the minimum-duration check.
- The monitor validates `ESP-4` / `Node 4` before accepting health data.
  API availability, dashboard-reported offline status and stale heartbeats
  are distinct observations; an API failure alone would not prove a board
  disconnection.
- An intentional Ctrl+C ended monitoring and saved the final summary.
  Its stop reason was `interrupted`, not a reported monitoring failure.

## Monitoring results

| Measurement | Verified result |
|---|---|
| Required minimum duration | 3,600 seconds |
| Actual monotonic duration | 5,506.559 seconds |
| Total observations | 5,507 |
| Poll spacing | 0.770-1.240 seconds |
| Monitoring gaps or missed polls | No gaps suggesting missed polling cycles; no observation interval exceeded 1.5 seconds |
| Observation/API errors | 0 |
| Dashboard-reported offline observations | 0 |
| Stale heartbeats (age greater than 8 seconds) | 0 |
| Maximum observed heartbeat age | 2.1 seconds |
| Microphone health failures | 0 |
| Possible unexpected restarts or uptime anomalies | 0 |
| Board uptime | Increased from 5,383 to 10,891 seconds without decreasing |
| Five-minute summaries | 18, through minute 90 |
| Final observed state | Online, microphone alive, idle |
| Final heartbeat age | 0.3 seconds |

The final summary contained zero observation errors, no event counts and no
active conditions. These findings were checked against the raw observations,
including poll spacing and uptime progression, rather than relying solely
on the summary's empty event list.

| Measurement | Minimum | Maximum | Final |
|---|---:|---:|---:|
| RSSI | -66 dBm | -37 dBm | -48 dBm |
| Free heap | 260,768 bytes | 262,880 bytes | 262,600 bytes |
| Free PSRAM | 7,401,423 bytes | 7,410,735 bytes | 7,410,735 bytes |

## Endpoint recording results

Both recordings occurred within the monitored session and were saved locally
under `captures/ESP-4/` as WAV files with matching JSON metadata. The labels
were `ESP-4 one-hour stability - start` and `ESP-4 one-hour stability - end`.
The older start-labelled recording `2026-10-26_16-26-39` was outside this
session and was excluded.

Peak uses `peak_dbfs`; RMS uses the dashboard's DC-removed `rms_ac_dbfs`.

| Test | Local capture reference | Duration | Samples | Peak dBFS | RMS dBFS | Clipped samples | Capture timing | CRC32 | Result |
|---|---|---:|---:|---:|---:|---:|---:|---|---|
| Start | 2026-10-26_16-51-03 | 1.000 s | 16,000 | -49.2 | -59.9 | 0 | 992 ms | 68d2fc45 | OK |
| End | 2026-10-26_18-21-20 | 1.000 s | 16,000 | -49.0 | -57.6 | 0 | 992 ms | aadc2b06 | OK |

Both WAV files contain 16 kHz, 16-bit mono audio. Their lengths and CRC32
checksums matched the saved metadata, all recalculated statistics matched,
and microphone health was alive. Capture timing passed the project's
940-1,100 ms acceptance range for a one-second recording. Neither capture
clipped. The saved recording timestamps were 90 minutes 17 seconds apart.

## Remaining limitations

- One-second polling can miss brief interruptions between observations.
  The dashboard API cannot prove there were no brief Wi-Fi interruptions;
  zero observed events is not proof of uninterrupted connectivity.
- Individual monitoring observations do not retain board ID, name or MAC
  address. The monitor's identity validation supports the logical node
  association, but the log does not independently establish continuous
  physical-board identity through MAC-address evidence.
- The wall-clock span exceeds the monotonic elapsed duration by 1.946 seconds.
  The reason was not recorded. Both endpoint recordings remain within the
  monitored interval.
- Memory observations alone cannot rule out a memory leak.
- Continuous audio recording was not tested; audio was verified only in
  the two manual endpoint recordings.
- Absolute acoustic calibration, simultaneous capture, synchronization,
  localization and compressed-air leak detection remain unvalidated.
- This result applies to ESP-4 only. Multi-node stability remains pending.

Only test findings and local evidence references are documented here.
Passwords, Wi-Fi network names, private IP addresses, raw monitoring logs
and audio recordings are excluded from this report.
