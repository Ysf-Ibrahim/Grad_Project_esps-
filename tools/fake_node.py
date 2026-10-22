#!/usr/bin/env python3
"""
Fake AcoustiScan node for developing and demonstrating the dashboard without a board.

Speaks the node protocol (PROTOCOL.md): sends heartbeats to the server on
127.0.0.1:5005 every 2 s and serves STATUS / PROBE / CAPTURE on a TCP port with
synthetic audio (room noise + a tone + speech-like bursts) at real-time speed.

  .venv\\Scripts\\python tools\\fake_node.py --id FAKE-1 --name "Fake north" --port 5011
  .venv\\Scripts\\python tools\\fake_node.py --id FAKE-2 --port 5012 --flat      # dead mic
  .venv\\Scripts\\python tools\\fake_node.py --id FAKE-3 --port 5013 --drop      # disconnects mid-transfer
"""
import argparse
import json
import math
import random
import socket
import struct
import sys
import threading
import time
import zlib

RATE = 16000


def synth(seconds, flat=False, seed=1):
    n = seconds * RATE
    if flat:
        return [-30935] * n
    rnd = random.Random(seed)
    out = []
    tone_f = 2764.8
    for i in range(n):
        t = i / RATE
        v = rnd.gauss(0, 60)                                   # room noise
        if t > 2.0:
            v += 2600 * math.sin(2 * math.pi * tone_f * t)     # phone tone, about -22 dBFS
        for a, b, amp in ((3.0, 4.2, 9000), (5.1, 5.6, 6000), (6.4, 7.9, 11000)):
            if a <= t <= b:
                p = (t - a) / (b - a)
                env = math.sin(math.pi * p) * (0.55 + 0.45 * abs(math.sin(t * 27)))
                v += amp * env * math.sin(2 * math.pi * 140 * t) * (1 + 0.5 * math.sin(2 * math.pi * 280 * t))
        out.append(max(-32768, min(32767, int(v))))
    return out


def stats_of(samples):
    n = len(samples)
    mn, mx = min(samples), max(samples)
    mean = sum(samples) / n
    rms = math.sqrt(sum(s * s for s in samples) / n)
    var = max(sum((s - mean) ** 2 for s in samples) / n, 0.0)
    return {
        "samples": n, "sample_rate": RATE, "pdm_clock_hz": 2048000, "min": mn, "max": mx,
        "mean": f"{mean:.1f}", "peak": max(abs(mn), abs(mx)),
        "peak_ac": int(max(abs(mx - mean), abs(mn - mean))), "rms": f"{rms:.1f}",
        "rms_ac": f"{math.sqrt(var):.1f}", "distinct": len(set(samples)),
        "clip": sum(1 for s in samples if abs(s) >= 32700), "all_zero": int(mn == 0 and mx == 0),
        "constant": int(mn == mx), "slot_unexpected": 0, "other_min": mn, "other_max": mx,
    }


class FakeNode:
    def __init__(self, args):
        self.a = args
        self.state = "idle"
        self.t0 = time.time()
        self.lock = threading.Lock()

    def status(self):
        return {
            "id": self.a.id, "name": self.a.name, "ip": "127.0.0.1", "port": self.a.port,
            "mac": "00:11:22:33:44:%02X" % (self.a.port % 256), "rssi": -48 - (self.a.port % 7) * 4,
            "uptime_s": int(time.time() - self.t0), "mic": "flat" if self.a.flat else "alive",
            "state": self.state, "fw": "fake-0.1", "heap": 210000, "psram": 7300000, "provisioned": 1,
        }

    def heartbeat_loop(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        while True:
            try:
                s.sendto(json.dumps(self.status()).encode(), (self.a.server, 5005))
            except OSError:
                pass
            time.sleep(2)

    def serve(self):
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("0.0.0.0", self.a.port))
        srv.listen(4)
        print(f"[{self.a.id}] TCP {self.a.port}, heartbeats to {self.a.server}:5005"
              + (" (flat mic)" if self.a.flat else "") + (" (drops mid-transfer)" if self.a.drop else ""))
        while True:
            c, addr = srv.accept()
            threading.Thread(target=self.handle, args=(c,), daemon=True).start()

    def handle(self, c):
        f = c.makefile("rwb", buffering=0)
        try:
            if not self.lock.acquire(blocking=False):
                f.write(b"ERR busy\n")
                return
            try:
                c.settimeout(5)
                line = f.readline().decode("latin-1").strip()
                cmd, _, arg = line.partition(" ")
                cmd = cmd.upper()
                if cmd == "STATUS":
                    f.write(("OK " + json.dumps(self.status()) + "\n").encode())
                elif cmd == "PROBE":
                    alive = 0 if self.a.flat else 1
                    f.write(f"OK clk=146000 data={0 if self.a.flat else 53000} data_high={0 if self.a.flat else 50} alive={alive}\n".encode())
                elif cmd == "CAPTURE":
                    self.capture(f, c, arg)
                else:
                    f.write(b"ERR unknown command\n")
            finally:
                self.lock.release()
        except (OSError, ValueError):
            pass
        finally:
            self.state = "idle"
            try:
                c.close()
            except OSError:
                pass

    def capture(self, f, c, arg):
        try:
            secs = int(arg)
        except ValueError:
            secs = 0
        if not 1 <= secs <= 30:
            f.write(b"ERR bad duration (1..30)\n")
            return
        n = secs * RATE
        f.write(f"OK id={self.a.id} samples={n} rate={RATE} bytes={2 * n}\n".encode())
        self.state = "recording"
        t0 = time.time()
        total = secs * 1000
        elapsed = 0
        speed = 3 if self.a.slow else 1
        while elapsed < total:
            time.sleep(0.5 * speed)
            elapsed = min(total, int((time.time() - t0) * 1000 / speed))
            f.write(f"PROG {elapsed} {total}\n".encode())
        samples = synth(secs, self.a.flat, seed=int(t0))
        self.state = "sending"
        st = stats_of(samples)
        st["capture_ms"] = total + 16
        st["expected_ms"] = total
        st.update({"clk_transitions": 146000, "data_transitions": 0 if self.a.flat else 53000,
                   "data_high_pct": 0 if self.a.flat else 50, "mic_alive": 0 if self.a.flat else 1})
        for k, v in st.items():
            f.write(f"STAT {k}={v}\n".encode())
        pcm = struct.pack("<%dh" % n, *samples)
        f.write(f"DATA {len(pcm)}\n".encode())
        if self.a.drop:
            f.write(pcm[: len(pcm) // 3])
            time.sleep(0.2)
            c.close()
            return
        f.write(pcm)
        f.write(f"END crc32={zlib.crc32(pcm) & 0xFFFFFFFF:08x}\n".encode())
        self.state = "idle"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--id", default="FAKE-1")
    ap.add_argument("--name", default="Fake node")
    ap.add_argument("--port", type=int, default=5011)
    ap.add_argument("--server", default="127.0.0.1", help="where to send heartbeats")
    ap.add_argument("--flat", action="store_true", help="simulate a dead microphone")
    ap.add_argument("--slow", action="store_true", help="record 3x slower than real time")
    ap.add_argument("--drop", action="store_true", help="disconnect in the middle of the transfer")
    args = ap.parse_args()
    node = FakeNode(args)
    threading.Thread(target=node.heartbeat_loop, daemon=True).start()
    try:
        node.serve()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    sys.exit(main())
