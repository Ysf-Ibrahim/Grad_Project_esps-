#!/usr/bin/env python3
"""
Provision an AcoustiScan node over USB (PROTOCOL.md section 1).

  .venv\\Scripts\\python tools\\provision.py --port COM8 --id ESP-1 --name "East wall"
  .venv\\Scripts\\python tools\\provision.py --port COM8 --show
  .venv\\Scripts\\python tools\\provision.py --port COM8 --factory

Wi-Fi name/password default to the values in include/wifi_credentials.h; override
with --ssid / --pass. The tool talks to the running firmware (no reset pulse),
saves, reboots the board and then waits for it to print its IP address, so a
successful run proves the credentials work.
"""
import argparse
import os
import re
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial missing: run with .venv\\Scripts\\python")

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def header_credentials():
    path = os.path.join(PROJECT, "include", "wifi_credentials.h")
    ssid = pw = None
    try:
        text = open(path, encoding="utf-8").read()
        m = re.search(r'#define\s+WIFI_SSID\s+"((?:[^"\\]|\\.)*)"', text)
        n = re.search(r'#define\s+WIFI_PASSWORD\s+"((?:[^"\\]|\\.)*)"', text)
        ssid = m.group(1) if m else None
        pw = n.group(1) if n else None
    except OSError:
        pass
    return ssid, pw


def open_port(port):
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = 115200
    ser.timeout = 0.2
    ser.dtr = False
    ser.rts = False
    ser.open()
    return ser


def read_lines(ser, seconds):
    end = time.time() + seconds
    while time.time() < end:
        raw = ser.readline()
        if raw:
            yield raw.decode("latin-1").rstrip("\r\n")


def command(ser, line, timeout=3.0, retries=1):
    """Send a command and return the first OK/ERR reply line (other lines are echoed)."""
    for attempt in range(retries + 1):
        ser.reset_input_buffer()
        ser.write((line + "\n").encode("latin-1"))
        ser.flush()
        for text in read_lines(ser, timeout):
            if text.startswith("OK") or text.startswith("ERR"):
                return text
            print(f"  [esp] {text}")
    return None


def main():
    ap = argparse.ArgumentParser(description="Provision an AcoustiScan node over USB")
    ap.add_argument("--port", required=True)
    ap.add_argument("--id", help="node id, e.g. ESP-1 (A-Z a-z 0-9 - _, max 15)")
    ap.add_argument("--name", help="display name, e.g. \"East wall\"")
    ap.add_argument("--ssid")
    ap.add_argument("--pass", dest="password")
    ap.add_argument("--show", action="store_true", help="only print the stored configuration")
    ap.add_argument("--factory", action="store_true", help="erase the stored configuration and reboot")
    ap.add_argument("--no-wait", action="store_true", help="do not wait for the Wi-Fi join after reboot")
    args = ap.parse_args()

    ser = open_port(args.port)
    print(f"[pc] {args.port} open")
    reply = command(ser, "SHOW", retries=2)
    if reply is None:
        print("[pc] no reply to SHOW. Is the node firmware running on this port? (pio run -e node -t upload)")
        return 2
    print(f"[esp] {reply}")

    if args.show:
        return 0
    if args.factory:
        print(f"[esp] {command(ser, 'FACTORY')}")
        return 0

    ssid, pw = header_credentials()
    ssid = args.ssid or ssid
    pw = args.password if args.password is not None else pw
    sets = []
    if args.id:
        sets.append(f"SET id={args.id}")
    if args.name is not None:
        sets.append(f"SET name={args.name}")
    if ssid:
        sets.append(f"SET ssid={ssid}")
    if pw is not None:
        sets.append(f"SET pass={pw}")
    if not sets:
        print("[pc] nothing to set (use --id / --name / --ssid / --pass)")
        return 1

    for s in sets:
        reply = command(ser, s)
        shown = s if not s.startswith("SET pass=") else "SET pass=****"
        print(f"[pc] {shown:32s} -> {reply}")
        if not reply or not reply.startswith("OK"):
            print("[pc] stopped: the node rejected a value")
            return 3

    reply = command(ser, "SAVE")
    print(f"[pc] SAVE -> {reply}")
    if not reply or not reply.startswith("OK"):
        return 3
    reply = command(ser, "REBOOT", timeout=1.0)
    print(f"[pc] REBOOT -> {reply}")

    if args.no_wait:
        return 0
    print("[pc] waiting for the node to boot and join Wi-Fi (up to 40 s) ...")
    ip = None
    for text in read_lines(ser, 40):
        print(f"  [esp] {text}")
        m = re.match(r"WIFI ip=(\S+) rssi=(-?\d+)", text)
        if m:
            ip = m.group(1)
            print(f"\n[pc] PROVISIONED: id={args.id or '(unchanged)'} joined {ssid!r} with IP {ip}, signal {m.group(2)} dBm")
            break
        if "PROVISION READY" in text and ip is None:
            pass
    if ip is None:
        print("\n[pc] saved, but the node did not report an IP within 40 s. Check the hotspot is on and the password.")
        return 4
    return 0


if __name__ == "__main__":
    sys.exit(main())
