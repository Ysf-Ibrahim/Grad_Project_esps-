#!/usr/bin/env python3
"""
Laptop side of the SPH0641 PDM microphone smoke test (firmware: src/mic_test.cpp,
PlatformIO env `mic-test`).

What it does
  1. opens the board's COM port and resets the board (RTS pulse, DTR held low)
  2. waits for `PDM_INIT PASS` + `READY`
  3. counts down 3-2-1, sends 'r', and plays a laptop beep during the capture
     window so there is always SOME sound even if nobody claps
  4. receives the WAV (base64 between WAV_BEGIN / WAV_END), checks length + CRC32
  5. writes captures/mic_test.wav, verifies it with the stdlib `wave` module,
     recomputes statistics, writes captures/mic_test_waveform.png
  6. prints a report block

Dependencies: Python 3 stdlib + pyserial. The system Python on this laptop has no
pyserial, so run it with PlatformIO's interpreter:

  & "$env:USERPROFILE\\.platformio\\penv\\Scripts\\python.exe" tools\\mic_capture.py --port COM8

Close the PlatformIO serial monitor first (the COM port is exclusive).
"""

import argparse
import base64
import math
import os
import re
import struct
import sys
import time
import wave
import zlib

try:
    import serial  # pyserial
except ImportError:
    sys.exit("pyserial is missing. Run this script with PlatformIO's python:\n"
             "  %USERPROFILE%\\.platformio\\penv\\Scripts\\python.exe tools\\mic_capture.py")

try:
    import winsound
except ImportError:  # not on Windows
    winsound = None

PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CAPTURE_DIR = os.path.join(PROJECT_DIR, "captures")
B64_LINE = re.compile(r"^[A-Za-z0-9+/=]+$")


# ----------------------------------------------------------------- serial I/O --

def open_port(port, baud):
    """Open without letting Windows assert DTR/RTS (that would reset / boot-mode the board)."""
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = baud
    ser.timeout = 0.2
    ser.dtr = False
    ser.rts = False
    ser.open()
    try:
        # Windows: enlarge the driver's receive buffer so a ~430 KB burst at
        # 921600 baud cannot overrun it while Python is busy.
        ser.set_buffer_size(rx_size=2 * 1024 * 1024, tx_size=64 * 1024)
    except (AttributeError, ValueError, OSError):
        pass
    return ser


def reset_board(ser):
    """RTS asserted while DTR is low pulls EN low on the DevKitC auto-reset circuit."""
    ser.rts = True
    time.sleep(0.1)
    ser.rts = False
    time.sleep(0.05)
    ser.reset_input_buffer()


def lines_until(ser, deadline):
    """Yield stripped text lines until the deadline (yields None on idle timeouts).

    Reads in bulk (everything waiting in the driver) instead of byte-by-byte, so a
    430 KB burst at 921600 baud does not overrun the COM port buffer.
    """
    buf = bytearray()
    while time.time() < deadline:
        chunk = ser.read(max(1, ser.in_waiting))
        if not chunk:
            yield None
            continue
        buf += chunk
        while True:
            i = buf.find(b"\n")
            if i < 0:
                break
            line = bytes(buf[:i])
            del buf[:i + 1]
            yield line.decode("latin-1").rstrip("\r")


def beep(freq, ms):
    if winsound is not None:
        try:
            winsound.Beep(freq, ms)
            return
        except RuntimeError:
            pass
    time.sleep(ms / 1000.0)


# ----------------------------------------------------------------- analysis --

def analyse(samples, capture_ms=None, expected_ms=1000.0):
    n = len(samples)
    mn, mx = min(samples), max(samples)
    mean = sum(samples) / n
    rms = math.sqrt(sum(s * s for s in samples) / n)
    var_ac = max(sum((s - mean) ** 2 for s in samples) / n, 0.0)
    rms_ac = math.sqrt(var_ac)
    peak = max(abs(mn), abs(mx))
    peak_ac = max(abs(mx - mean), abs(mn - mean))
    distinct = len(set(samples))
    clip = sum(1 for s in samples if abs(s) >= 32700)
    constant = (mn == mx)
    all_zero = (mn == 0 and mx == 0)

    # 50 ms windows (800 samples @ 16 kHz) after DC removal -> loud vs quiet parts
    win = 800
    win_rms = []
    for i in range(0, n - win + 1, win):
        chunk = samples[i:i + win]
        win_rms.append(math.sqrt(sum((s - mean) ** 2 for s in chunk) / win))
    win_sorted = sorted(win_rms)
    max_win = win_sorted[-1] if win_sorted else 0.0
    p20_win = win_sorted[int(0.2 * (len(win_sorted) - 1))] if win_sorted else 0.0

    alive = (not constant and distinct >= 64 and abs(mean) <= 8000
             and (capture_ms is None or 0.94 * expected_ms <= capture_ms <= 1.10 * expected_ms))
    sound_event = (peak_ac >= 500 and (max_win >= 4.0 * max(p20_win, 1.0) or peak_ac >= 3000))

    return {
        "count": n, "min": mn, "max": mx, "mean": mean, "rms": rms, "rms_ac": rms_ac,
        "peak": peak, "peak_ac": peak_ac, "distinct": distinct, "clip": clip,
        "constant": constant, "all_zero": all_zero,
        "max_win_rms": max_win, "p20_win_rms": p20_win,
        "alive": alive, "sound_event": sound_event,
    }


# --------------------------------------------------------------------- PNG --

def write_png(path, width, height, rgb_rows):
    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + bytes(row) for row in rgb_rows)
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 6))
           + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def waveform_png(path, samples, sample_rate, mean, width=1600, height=400):
    bg, grid, mid, ink, dc = (250, 250, 250), (225, 225, 225), (160, 160, 160), (20, 90, 200), (220, 60, 60)
    canvas = bytearray(width * height * 3)

    def put(x, y, c):
        if 0 <= x < width and 0 <= y < height:
            i = (y * width + x) * 3
            canvas[i:i + 3] = bytes(c)

    for y in range(height):
        for x in range(width):
            put(x, y, bg)
    # vertical grid every 100 ms
    n = len(samples)
    step_ms = 100 if n <= 2 * sample_rate else 1000
    for t_ms in range(0, int(1000 * n / sample_rate) + 1, step_ms):
        x = int(t_ms / 1000.0 * sample_rate / n * width)
        for y in range(height):
            put(x, y, grid)
    half = height / 2.0 - 8
    cy = height // 2
    for x in range(width):
        put(x, cy, mid)
    ydc = int(cy - mean / 32768.0 * half)
    for x in range(0, width, 4):
        put(x, ydc, dc)
    # per-column min/max envelope
    for x in range(width):
        a = int(x * n / width)
        b = max(a + 1, int((x + 1) * n / width))
        seg = samples[a:b]
        lo, hi = min(seg), max(seg)
        y1 = int(cy - hi / 32768.0 * half)
        y2 = int(cy - lo / 32768.0 * half)
        for y in range(min(y1, y2), max(y1, y2) + 1):
            put(x, y, ink)
    rows = [canvas[r * width * 3:(r + 1) * width * 3] for r in range(height)]
    write_png(path, width, height, rows)


# -------------------------------------------------------------------- main --

def main():
    ap = argparse.ArgumentParser(description="Receive the ESP32-S3 PDM mic test capture as WAV")
    ap.add_argument("--port", default="COM8")
    ap.add_argument("--baud", type=int, default=921600)
    ap.add_argument("--seconds", type=float, default=10.0, help="recording length compiled into the firmware")
    ap.add_argument("--out", default=os.path.join(CAPTURE_DIR, "mic_test.wav"))
    ap.add_argument("--png", default=os.path.join(CAPTURE_DIR, "mic_test_waveform.png"))
    ap.add_argument("--no-reset", action="store_true", help="do not pulse RTS to reset the board")
    ap.add_argument("--no-beep", action="store_true", help="do not play the laptop beep during capture")
    ap.add_argument("--beep-hz", type=int, default=0, help="play one laptop tone at this frequency instead of the marker beeps")
    ap.add_argument("--beep-ms", type=int, default=3000, help="length of that tone in ms")
    ap.add_argument("--watch", action="store_true", help="only print the board output (live line status) until Ctrl+C")
    ap.add_argument("--resend-only", action="store_true",
                    help="do not record; ask the board to resend the last capture (transfer test)")
    args = ap.parse_args()

    problems = []
    board_stats = {}

    print(f"[pc] opening {args.port} @ {args.baud}")
    ser = open_port(args.port, args.baud)
    if args.resend_only:
        args.no_reset = True          # the capture lives in the board's RAM; a reset would erase it
    if not args.no_reset:
        print("[pc] resetting board")
        reset_board(ser)

    if args.watch:
        print("[pc] watch mode: re-seat the mic wires and watch the READY line (Ctrl+C to stop)")
        try:
            while True:
                raw = ser.readline()
                if raw:
                    print(f"  [esp] {raw.decode('latin-1').rstrip()}")
        except KeyboardInterrupt:
            pass
        ser.close()
        return

    # ---- wait for init + READY ----
    pdm_init = None
    ready = False
    if args.resend_only:
        pdm_init, ready = "PASS", True
    for line in lines_until(ser, time.time() + 20):
        if line is None:
            continue
        print(f"  [esp] {line}")
        if line.startswith("PDM_INIT PASS"):
            pdm_init = "PASS"
        elif line.startswith("PDM_INIT FAIL"):
            pdm_init = "FAIL " + line[len("PDM_INIT FAIL"):].strip()
            break
        elif line.startswith("READY") and pdm_init == "PASS":
            ready = True
            break
    if pdm_init is None:
        ser.close()
        print("\nPDM INIT: UNKNOWN (no PDM_INIT line seen within 20 s - wrong port? board not running mic-test firmware?)")
        sys.exit(2)
    if pdm_init != "PASS":
        ser.close()
        print(f"\nPDM INIT: {pdm_init}")
        sys.exit(2)
    if not ready:
        ser.close()
        print("\nPDM INIT: PASS but no READY within 20 s")
        sys.exit(2)

    # ---- countdown, trigger, beep ----
    print("\n[pc] Recording in 3 ...");  beep(600, 120); time.sleep(0.9)
    print("[pc] Recording in 2 ...");    beep(600, 120); time.sleep(0.9)
    print("[pc] Recording in 1 ...");    beep(600, 120); time.sleep(0.9)
    ser.reset_input_buffer()
    ser.write(b"r")
    ser.flush()
    t_trigger = time.time()
    print(f"[pc] >>> START TALKING NOW - keep talking for {args.seconds:.0f} seconds <<<")
    if not args.no_beep:
        time.sleep(0.45)                 # firmware discards the first 300 ms
        if args.beep_hz:
            beep(args.beep_hz, args.beep_ms)   # one reference tone (frequency check)
        else:
            beep(1000, 250)              # default marker: two short beeps
            time.sleep(0.15)
            beep(1500, 250)

    # ---- receive stats + WAV (with automatic resend on a bad transfer) ----
    def receive_transfer(deadline):
        """Read board lines until WAV_END. Returns (data_or_None, error_text_or_None).

        The board sends the WAV in CRC-checked blocks; each `BLK <idx> <crc>` line
        is answered with 'a' (accepted) or 'n' (resend), so lost bytes anywhere on
        the serial path are repaired instead of failing the whole transfer.
        """
        expected_len = None
        expected_crc = None
        chunks = []          # accepted blocks, in order
        pending = []         # base64 lines of the block being received
        next_idx = 0
        nacks = 0
        got_end = False
        for line in lines_until(ser, deadline):
            if line is None:
                continue
            if expected_len is not None:                  # inside the transfer
                if line.startswith("BLK "):
                    parts = line.split()
                    idx, bcrc = int(parts[1]), int(parts[2], 16)
                    try:
                        blk = base64.b64decode("".join(pending), validate=True)
                    except (ValueError, base64.binascii.Error):
                        blk = None
                    pending = []
                    if idx == next_idx and blk is not None and (zlib.crc32(blk) & 0xFFFFFFFF) == bcrc:
                        chunks.append(blk)
                        next_idx += 1
                        ser.write(b"a")
                    elif idx < next_idx:                  # duplicate of an accepted block (ack got lost)
                        ser.write(b"a")
                    else:
                        nacks += 1
                        ser.write(b"n")
                    continue
                if line.startswith("WAV_END"):
                    got_end = True
                    break
                if line.startswith("WAV_ABORT"):
                    return None, line
                if B64_LINE.match(line):
                    pending.append(line)
                else:
                    print(f"  [esp] {line}")
                continue
            print(f"  [esp] {line}")
            if line.startswith("STAT "):
                key, _, val = line[5:].partition("=")
                board_stats[key.strip()] = val.strip()
            elif line.startswith("WAV_BEGIN"):
                parts = line.split()
                expected_len = int(parts[1])
                expected_crc = int(parts[2], 16)
            elif line.startswith("CAPTURE FAIL") or line.startswith("PDM_INIT FAIL") or line.startswith("NO_WAV"):
                return None, line
        if not got_end:
            return None, "WAV_END not received in time"
        data = b"".join(chunks)
        if nacks:
            print(f"[pc] {nacks} block(s) had to be resent (serial link lost bytes; repaired)")
        board_stats["block_resends"] = str(nacks)
        if len(data) != expected_len:
            return None, f"transfer length {len(data)} != announced {expected_len}"
        crc = zlib.crc32(data) & 0xFFFFFFFF
        if crc != expected_crc:
            return None, f"CRC32 mismatch pc={crc:08x} esp={expected_crc:08x}"
        return data, None

    data, err = receive_transfer(t_trigger + args.seconds + 90)
    attempt = 0
    while data is None and attempt < 3 and err and ("length" in err or "CRC32" in err or "base64" in err or "WAV_END" in err):
        attempt += 1
        print(f"[pc] transfer problem ({err}) - asking the board to resend (attempt {attempt}/3)")
        time.sleep(0.5)
        ser.reset_input_buffer()
        ser.write(b"s")
        ser.flush()
        data, err = receive_transfer(time.time() + 90)
    ser.close()

    if data is None:
        print(f"\nCAPTURE: FAIL ({err})")
        sys.exit(3)
    crc = zlib.crc32(data) & 0xFFFFFFFF
    print(f"[pc] received {len(data)} bytes, CRC32 {crc:08x} OK" + (f" (after {attempt} resend)" if attempt else ""))

    # ---- save + verify WAV ----
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(data)

    with wave.open(args.out, "rb") as w:
        channels, sampwidth, rate, nframes = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        pcm = w.readframes(nframes)
    duration = nframes / float(rate)
    samples = list(struct.unpack("<%dh" % nframes, pcm))

    if channels != 1:
        problems.append(f"channels={channels} (expected 1)")
    if sampwidth != 2:
        problems.append(f"sample width={sampwidth} bytes (expected 2)")
    if rate != 16000:
        problems.append(f"sample rate={rate} (expected 16000)")
    if not (0.95 * args.seconds <= duration <= 1.05 * args.seconds):
        problems.append(f"duration={duration:.3f}s (expected ~{args.seconds:.1f})")

    capture_ms = int(board_stats["capture_ms"]) if "capture_ms" in board_stats else None
    st = analyse(samples, capture_ms, args.seconds * 1000.0)

    if capture_ms is not None and not (0.94 * args.seconds * 1000 <= capture_ms <= 1.10 * args.seconds * 1000):
        problems.append(f"capture took {capture_ms} ms for {nframes} frames (clock/DSR problem)")
    if st["all_zero"]:
        problems.append("samples are all zero")
    elif st["constant"]:
        problems.append(f"samples are constant ({st['min']}) - no mic signal (unpowered / DATA not connected)")
    if abs(st["mean"]) > 8000:
        problems.append(f"huge DC offset {st['mean']:.0f} - wrong slot/format?")
    if "clk_transitions" in board_stats and int(board_stats["clk_transitions"]) < 100:
        problems.append("PDM clock on GPIO4 is NOT toggling (ESP32 side)")
    if "data_transitions" in board_stats and int(board_stats["data_transitions"]) < 100:
        problems.append("DATA line GPIO5 is flat - the mic is not driving it (check mic 3V3/GND, CLK to mic, DATA to GPIO5)")
    if st["clip"] >= 0.005 * st["count"]:
        problems.append(f"{st['clip']} clipped samples (too loud / too close)")
    if "ch1_min" in board_stats and int(board_stats["ch1_min"]) != st["min"]:
        problems.append("board ch1_min differs from file min (transfer/slot mismatch)")
    if "ch1_max" in board_stats and int(board_stats["ch1_max"]) != st["max"]:
        problems.append("board ch1_max differs from file max (transfer/slot mismatch)")

    # ---- PNG ----
    png_ok = True
    try:
        waveform_png(args.png, samples, rate, st["mean"])
    except Exception as exc:  # noqa: BLE001
        png_ok = False
        problems.append(f"PNG not written: {exc}")

    responding = st["alive"] and st["sound_event"]

    print("\n================ MIC TEST REPORT (laptop side) ================")
    print(f"PDM INIT: PASS")
    print(f"CAPTURE: PASS")
    print(f"WAV FILE: {os.path.abspath(args.out)}")
    print(f"WAVEFORM PNG: {os.path.abspath(args.png) if png_ok else 'not written'}")
    print(f"DURATION: {duration:.3f} s ({nframes} samples; board capture_ms={capture_ms})")
    print(f"SAMPLE RATE: {rate} Hz, {channels} ch, {8 * sampwidth}-bit")
    print(f"MIN SAMPLE: {st['min']}")
    print(f"MAX SAMPLE: {st['max']}")
    print(f"MEAN (DC): {st['mean']:.1f}")
    print(f"PEAK: {st['peak']}   PEAK (DC removed): {st['peak_ac']:.0f}")
    print(f"RMS: {st['rms']:.1f}   RMS (DC removed): {st['rms_ac']:.1f}")
    print(f"DISTINCT VALUES: {st['distinct']}   CLIPPED: {st['clip']}")
    print(f"LINE PROBE: CLK transitions={board_stats.get('clk_transitions', '?')}   DATA transitions={board_stats.get('data_transitions', '?')}")
    print(f"LOUDEST 50 ms WINDOW RMS: {st['max_win_rms']:.1f}   QUIET (20th pct) WINDOW RMS: {st['p20_win_rms']:.1f}")
    print(f"MIC ALIVE: {'YES' if st['alive'] else 'NO'}   SOUND EVENT DETECTED: {'YES' if st['sound_event'] else 'NO'}")
    print(f"MIC RESPONDING TO SOUND: {'YES' if responding else 'NO'}")
    print(f"ANY PROBLEM: {'; '.join(problems) if problems else 'none'}")
    print("===============================================================")
    sys.exit(0 if (responding and not problems) else 4)


if __name__ == "__main__":
    main()
