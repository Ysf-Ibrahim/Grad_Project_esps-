#!/usr/bin/env python3
"""
Network gate / debugging aid for AcoustiScan discovery (PROTOCOL.md section 2).

Sends the server beacon `ACOUSTISCAN-SERVER 1` to the broadcast address every 2 s
from every local IPv4 address, and prints every datagram that arrives on UDP 5005
(node heartbeats). Optionally also asks one node for STATUS over TCP.

  .venv\\Scripts\\python tools\\udp_probe.py              # listen + beacon, Ctrl+C to stop
  .venv\\Scripts\\python tools\\udp_probe.py --tcp 172.30.40.207
"""
import argparse
import json
import socket
import sys
import time

PORT = 5005
BEACON = b"ACOUSTISCAN-SERVER 1"


def local_ipv4s():
    ips = set()
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ips.add(info[4][0])
    except socket.gaierror:
        pass
    return sorted(ip for ip in ips if not ip.startswith("127."))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tcp", help="node IP to query with STATUS over TCP 5010 and exit")
    ap.add_argument("--seconds", type=float, default=0, help="stop after this many seconds (0 = until Ctrl+C)")
    args = ap.parse_args()

    if args.tcp:
        with socket.create_connection((args.tcp, 5010), timeout=5) as s:
            s.sendall(b"STATUS\n")
            print(s.makefile("rb").readline().decode("latin-1").rstrip())
        return

    listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("", PORT))
    listener.settimeout(0.2)

    senders = {}
    print(f"[probe] listening on UDP {PORT}; local IPv4: {', '.join(local_ipv4s()) or 'none'}")
    t_end = time.time() + args.seconds if args.seconds else None
    last_beacon = 0
    seen = {}
    try:
        while t_end is None or time.time() < t_end:
            now = time.time()
            if now - last_beacon >= 2:
                last_beacon = now
                for ip in local_ipv4s():
                    if ip not in senders:
                        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                        s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
                        try:
                            s.bind((ip, 0))
                        except OSError:
                            continue
                        senders[ip] = s
                    try:
                        senders[ip].sendto(BEACON, ("255.255.255.255", PORT))
                    except OSError as exc:
                        print(f"[probe] beacon from {ip} failed: {exc}")
            try:
                data, addr = listener.recvfrom(2048)
            except socket.timeout:
                continue
            if data.startswith(b"ACOUSTISCAN-SERVER"):
                continue
            text = data.decode("latin-1", "replace").strip()
            try:
                hb = json.loads(text)
                key = hb.get("id")
                gap = f" (+{now - seen[key]:.1f} s)" if key in seen else " (first)"
                seen[key] = now
                print(f"[{time.strftime('%H:%M:%S')}] heartbeat from {addr[0]}: id={hb.get('id')} name={hb.get('name')!r} "
                      f"rssi={hb.get('rssi')} mic={hb.get('mic')} state={hb.get('state')} fw={hb.get('fw')}{gap}")
            except ValueError:
                print(f"[{time.strftime('%H:%M:%S')}] {addr[0]}: {text}")
    except KeyboardInterrupt:
        pass
    print(f"[probe] nodes seen: {', '.join(seen) or 'none'}")
    return 0 if seen else 1


if __name__ == "__main__":
    sys.exit(main())
