"""Read-only ESP-4 monitoring. Duration is a minimum, not an automatic stop.

Use --preflight for a short run that exits automatically. No captures are made.
Evidence is written outside the repository; only allowlisted health data is saved.
"""
import argparse
from collections import Counter
from datetime import datetime, timedelta, timezone
import json
import math
from pathlib import Path
import signal
import threading
import time
import urllib.error
import urllib.request
import uuid

URL = "http://127.0.0.1:8080/api/nodes"
PROJECT = Path(__file__).resolve().parents[1]
DUBAI = timezone(timedelta(hours=4))
FIELDS = ("online", "last_seen_ago", "mic", "uptime_s", "state", "rssi", "heap", "psram")
STOP = threading.Event()


class ObservationError(Exception):
    pass


def timestamp():
    return datetime.now(DUBAI).isoformat(timespec="milliseconds")


def select_node(payload):
    """Validate the actual API list shape; never serialize raw responses."""
    if not isinstance(payload, list) or not all(isinstance(n, dict) for n in payload):
        raise ObservationError("invalid_node_list")
    matches = [n for n in payload if n.get("id") == "ESP-4"]
    if len(matches) != 1:
        raise ObservationError("node_missing" if not matches else "duplicate_identity")
    n = matches[0]
    if n.get("name") != "Node 4":
        raise ObservationError("unexpected_name")
    if type(n.get("online")) is not bool:
        raise ObservationError("invalid_online")
    for key in ("last_seen_ago", "uptime_s", "rssi", "heap", "psram"):
        v = n.get(key)
        if type(v) not in (int, float) or not math.isfinite(v):
            raise ObservationError("invalid_health_fields")
        if key != "rssi" and v < 0:
            raise ObservationError("invalid_health_fields")
    if n.get("mic") not in ("alive", "flat") or n.get("state") not in ("idle", "recording", "sending"):
        raise ObservationError("invalid_health_state")
    return {key: n[key] for key in FIELDS}


def fetch_node():
    # Do not inherit HTTP proxies for this loopback-only request.
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    try:
        with opener.open(URL, timeout=2) as response:
            payload = json.load(response)
    except urllib.error.HTTPError as exc:
        raise ObservationError("http_error_" + str(exc.code)) from None
    except (urllib.error.URLError, TimeoutError, OSError):
        raise ObservationError("api_unreachable_or_timeout") from None
    except (ValueError, UnicodeError):
        raise ObservationError("invalid_json") from None
    return select_node(payload)


class Monitor:
    # Allow multiple cached 2-second heartbeats, polling jitter and rounding.
    UPTIME_STALL_SECONDS = 15.0

    def __init__(self, emit):
        self.emit = emit
        self.active = {}
        self.counts = Counter()
        self.samples = 0
        self.errors = 0
        self.previous_uptime = None
        self.previous_time = None
        self.progress_uptime = None
        self.last_uptime_advance = None
        self.uptime_stall_since = None

    def event(self, kind, elapsed, **fields):
        self.counts[kind] += 1
        self.emit("event", elapsed, event=kind, **fields)

    def condition(self, name, present, elapsed):
        if present and name not in self.active:
            self.active[name] = elapsed
            self.event(name, elapsed)
        elif not present and name in self.active:
            start = self.active.pop(name)
            self.event(name + "_recovered", elapsed, observed_duration_s=round(elapsed-start, 3))

    def observe(self, node, error, elapsed):
        self.samples += 1
        if error:
            self.uptime_stall_since = None
            self.errors += 1
            self.emit("observation", elapsed, error=error)
            self.condition("observation_unavailable", True, elapsed)
            # Unknown state is not evidence of a board disconnect or recovery.
            return
        self.condition("observation_unavailable", False, elapsed)
        self.emit("observation", elapsed, **node)
        self.condition("dashboard_reports_offline", not node["online"], elapsed)
        self.condition("stale_heartbeat", node["last_seen_ago"] > 8, elapsed)
        self.condition("microphone_not_alive", node["mic"] != "alive", elapsed)
        # Cached/stale observations cannot establish uptime progression.
        if node["online"] and node["last_seen_ago"] <= 8:
            uptime = node["uptime_s"]
            restarted = False
            if self.previous_uptime is not None:
                delta = uptime-self.previous_uptime
                wall_delta = elapsed-self.previous_time
                if delta < -2:
                    restarted = True
                    self.event("possible_restart", elapsed, previous_uptime_s=self.previous_uptime, uptime_s=uptime)
                elif abs(delta-wall_delta) > 10:
                    self.event("uptime_progression_anomaly", elapsed, uptime_delta_s=delta, elapsed_delta_s=round(wall_delta, 3))
            # Do not count an unobserved polling gap as evidence of a stall.
            if self.previous_time is not None and elapsed-self.previous_time > 3:
                self.uptime_stall_since = None
            if self.progress_uptime is None or restarted:
                self.progress_uptime = uptime
                self.last_uptime_advance = None
                self.uptime_stall_since = elapsed
                # A restart is not proof of recovery from an active stall.
            elif uptime > self.progress_uptime:
                # Use a high-water mark so minor backward rounding cannot
                # repeatedly reset the timer without genuine forward progress.
                self.progress_uptime = uptime
                self.last_uptime_advance = elapsed
                self.uptime_stall_since = elapsed
                self.condition("uptime_stalled", False, elapsed)
            else:
                if self.uptime_stall_since is None:
                    self.uptime_stall_since = elapsed
                if elapsed-self.uptime_stall_since > self.UPTIME_STALL_SECONDS:
                    self.condition("uptime_stalled", True, elapsed)
            self.previous_uptime, self.previous_time = uptime, elapsed
        else:
            # Unknown progression neither establishes a stall nor clears one.
            # Require a fresh observation window after offline/stale periods.
            self.uptime_stall_since = None

    def summary(self, elapsed):
        return dict(observations=self.samples, observation_errors=self.errors,
                    event_counts=dict(self.counts), active_conditions=dict(self.active),
                    elapsed_s=round(elapsed, 3))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--duration", type=float, default=3600, help="minimum seconds; normal mode continues until Ctrl+C")
    ap.add_argument("--preflight", action="store_true", help="auto-stop at duration (maximum 30 seconds); NOT a soak test")
    ap.add_argument("--output-dir", type=Path, default=Path.home()/"AcoustiScan-evidence"/"ESP-4")
    args = ap.parse_args()
    if not math.isfinite(args.duration) or args.duration <= 0:
        ap.error("duration must be finite and positive")
    if args.preflight and args.duration > 30:
        ap.error("preflight duration must be at most 30 seconds")
    out = args.output_dir.expanduser().resolve()
    if out == PROJECT or PROJECT in out.parents:
        ap.error("evidence directory must be outside the repository")
    out.mkdir(parents=True, exist_ok=True)
    stem = datetime.now(DUBAI).strftime("%Y%m%dT%H%M%S%z") + "_" + uuid.uuid4().hex[:8]
    log_path, summary_path = out/(stem+".jsonl"), out/(stem+"_summary.txt")
    STOP.clear()
    signal.signal(signal.SIGINT, lambda *_: STOP.set())
    if hasattr(signal, "SIGTERM"):
        signal.signal(signal.SIGTERM, lambda *_: STOP.set())
    start = time.monotonic()
    reason = "interrupted"
    reached = False
    with log_path.open("x", encoding="utf-8", buffering=1) as log:
        def emit(kind, elapsed, **data):
            row = dict(timestamp=timestamp(), elapsed_s=round(elapsed, 3), type=kind, **data)
            log.write(json.dumps(row, allow_nan=False)+"\n")
            log.flush()
        monitor = Monitor(emit)
        emit("start", 0, node="ESP-4", name="Node 4", minimum_duration_s=args.duration,
             preflight=args.preflight, note="Manual recordings only; no automatic soak verdict")
        print("Evidence:", log_path, flush=True)
        print("Read-only monitoring. Ctrl+C stops and writes the summary.", flush=True)
        next_poll, next_summary, last_poll = start, 300.0, None
        try:
            while not STOP.is_set():
                now = time.monotonic()
                elapsed = now-start
                if elapsed >= args.duration and not reached:
                    reached = True
                    emit("minimum_duration_reached", elapsed)
                    print("Minimum duration reached. Normal mode continues until Ctrl+C.", flush=True)
                if reached and args.preflight:
                    reason = "preflight_complete"
                    break
                if last_poll is not None and now-last_poll > 3:
                    monitor.event("monitoring_gap", elapsed, gap_s=round(now-last_poll, 3))
                last_poll = now
                try:
                    node, error = fetch_node(), None
                except ObservationError as exc:
                    node, error = None, str(exc)
                monitor.observe(node, error, time.monotonic()-start)
                elapsed = time.monotonic()-start
                if elapsed >= next_summary:
                    emit("health_summary", elapsed, health=monitor.summary(elapsed))
                    print(json.dumps(monitor.summary(elapsed)), flush=True)
                    next_summary = (int(elapsed//300)+1)*300
                next_poll += 1
                if next_poll < time.monotonic():
                    next_poll = time.monotonic()
                STOP.wait(max(0, next_poll-time.monotonic()))
        except Exception:
            reason = "monitor_error"
            raise
        finally:
            elapsed = time.monotonic()-start
            health = monitor.summary(elapsed)
            emit("final_summary", elapsed, reason=reason, minimum_duration_reached=elapsed>=args.duration, health=health)
            text = ("ESP-4 monitoring summary\n" + "Finished: " + timestamp() + "\n" +
                    "Mode: " + ("PREFLIGHT (not a soak test)" if args.preflight else "MANUAL-CAPTURE SOAK MONITOR") + "\n" +
                    "Stop reason: " + reason + "\n" +
                    f"Minimum duration: {args.duration:.1f} seconds; observed: {elapsed:.3f} seconds\n" +
                    json.dumps(health, indent=2) + "\n" +
                    "No automatic PASS: review all events, gaps and both manual recording integrity checks.\n" +
                    "API errors are observation failures, not proof of a board disconnect.\n")
            with summary_path.open("x", encoding="utf-8") as summary:
                summary.write(text)
            print(text, flush=True)
            print("Summary:", summary_path, flush=True)
    return 0


if __name__ == "__main__":
    main()
