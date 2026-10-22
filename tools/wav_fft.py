#!/usr/bin/env python3
"""
FFT analysis of a mic capture, standard library only (no numpy on this laptop).

Outputs next to the WAV (default captures/mic_test.wav):
  <name>_fft.png          averaged power spectrum (Welch, Hann window), dBFS vs Hz
  <name>_fft.csv          frequency_hz, level_dbfs
  <name>_spectrogram.png  time-frequency view (512-point FFT, 16 ms hop)

dBFS reference: a full-scale sine (amplitude 32768) reads 0 dBFS at its bin.

Run:  & "$env:USERPROFILE\\.platformio\\penv\\Scripts\\python.exe" tools\\wav_fft.py
"""

import argparse
import cmath
import math
import os
import struct
import sys
import wave
import zlib

FULL_SCALE = 32768.0


# ----------------------------------------------------------------- FFT core --

def fft(a):
    """In-place iterative radix-2 FFT of a list of complex numbers (len = power of 2)."""
    n = len(a)
    if n & (n - 1):
        raise ValueError("FFT length must be a power of two")
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j |= bit
        if i < j:
            a[i], a[j] = a[j], a[i]
    length = 2
    while length <= n:
        half = length // 2
        tw = [cmath.exp(-2j * math.pi * k / length) for k in range(half)]
        for i in range(0, n, length):
            for k in range(half):
                u = a[i + k]
                v = a[i + k + half] * tw[k]
                a[i + k] = u + v
                a[i + k + half] = u - v
        length <<= 1
    return a


def hann(n):
    return [0.5 - 0.5 * math.cos(2.0 * math.pi * i / n) for i in range(n)]


def to_dbfs(amp):
    return 20.0 * math.log10(max(amp, 1e-9) / FULL_SCALE)


def welch(samples, rate, nfft=4096, hop=2048):
    """Averaged one-sided amplitude spectrum -> (freqs, dbfs per bin)."""
    win = hann(nfft)
    norm = sum(win) / 2.0                      # |X[k]| / norm = sine amplitude
    acc = [0.0] * (nfft // 2 + 1)
    count = 0
    for start in range(0, len(samples) - nfft + 1, hop):
        seg = [complex(samples[start + i] * win[i], 0.0) for i in range(nfft)]
        X = fft(seg)
        for k in range(nfft // 2 + 1):
            m = abs(X[k]) / norm
            acc[k] += m * m
        count += 1
    if count == 0:
        raise ValueError("recording shorter than one FFT window")
    amps = [math.sqrt(a / count) for a in acc]
    freqs = [k * rate / nfft for k in range(nfft // 2 + 1)]
    return freqs, [to_dbfs(a) for a in amps]


def spectrogram(samples, nfft=512, hop=256):
    win = hann(nfft)
    norm = sum(win) / 2.0
    cols = []
    for start in range(0, len(samples) - nfft + 1, hop):
        seg = [complex(samples[start + i] * win[i], 0.0) for i in range(nfft)]
        X = fft(seg)
        cols.append([to_dbfs(abs(X[k]) / norm) for k in range(nfft // 2 + 1)])
    return cols


# ------------------------------------------------------------ PNG drawing --

def write_png(path, width, height, canvas):
    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + bytes(canvas[r * width * 3:(r + 1) * width * 3]) for r in range(height))
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 6))
           + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


FONT = {  # 3x5 pixel glyphs
    "0": ["111", "101", "101", "101", "111"], "1": ["010", "110", "010", "010", "111"],
    "2": ["111", "001", "111", "100", "111"], "3": ["111", "001", "111", "001", "111"],
    "4": ["101", "101", "111", "001", "001"], "5": ["111", "100", "111", "001", "111"],
    "6": ["111", "100", "111", "101", "111"], "7": ["111", "001", "001", "001", "001"],
    "8": ["111", "101", "111", "101", "111"], "9": ["111", "101", "111", "001", "111"],
    "-": ["000", "000", "111", "000", "000"], ".": ["000", "000", "000", "000", "010"],
    "k": ["100", "101", "110", "101", "101"], "H": ["101", "101", "111", "101", "101"],
    "z": ["000", "111", "010", "100", "111"], "d": ["001", "001", "111", "101", "111"],
    "B": ["110", "101", "110", "101", "110"], "F": ["111", "100", "110", "100", "100"],
    "S": ["111", "100", "111", "001", "111"], "s": ["011", "100", "010", "001", "110"],
    " ": ["000", "000", "000", "000", "000"],
}


class Canvas:
    def __init__(self, width, height, bg):
        self.w, self.h = width, height
        self.buf = bytearray(bytes(bg) * (width * height))

    def put(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            i = (y * self.w + x) * 3
            self.buf[i:i + 3] = bytes(c)

    def hline(self, x0, x1, y, c):
        for x in range(min(x0, x1), max(x0, x1) + 1):
            self.put(x, y, c)

    def vline(self, x, y0, y1, c):
        for y in range(min(y0, y1), max(y0, y1) + 1):
            self.put(x, y, c)

    def text(self, x, y, s, c, scale=2):
        for ch in s:
            g = FONT.get(ch, FONT[" "])
            for r, row in enumerate(g):
                for k, bit in enumerate(row):
                    if bit == "1":
                        for dy in range(scale):
                            for dx in range(scale):
                                self.put(x + k * scale + dx, y + r * scale + dy, c)
            x += 4 * scale

    def save(self, path):
        write_png(path, self.w, self.h, self.buf)


def plot_spectrum(path, freqs, db, fmax, db_lo=-100.0, db_hi=0.0):
    W, H, L, R, T, B = 1600, 520, 70, 20, 20, 45
    cv = Canvas(W, H, (250, 250, 250))
    grid, axis, ink, fill, txt = (222, 222, 222), (90, 90, 90), (20, 90, 200), (170, 195, 240), (60, 60, 60)
    pw, ph = W - L - R, H - T - B

    def xf(f):
        return L + int(f / fmax * pw)

    def yd(d):
        return T + int((db_hi - max(min(d, db_hi), db_lo)) / (db_hi - db_lo) * ph)

    for f in range(0, int(fmax) + 1, 1000):
        cv.vline(xf(f), T, T + ph, grid)
        label = "0" if f == 0 else f"{f // 1000}k"
        cv.text(xf(f) - 4 * len(label), T + ph + 8, label, txt)
    cv.text(L + pw // 2 - 12, H - 12, "Hz", txt)
    for d in range(int(db_lo), int(db_hi) + 1, 20):
        cv.hline(L, L + pw, yd(d), grid)
        cv.text(4, yd(d) - 5, f"{d:>4d}", txt)
    cv.text(4, 4, "dBFS", txt)
    cv.hline(L, L + pw, T + ph, axis)
    cv.vline(L, T, T + ph, axis)

    prev = None
    for f, d in zip(freqs, db):
        if f > fmax:
            break
        x, y = xf(f), yd(d)
        cv.vline(x, y, T + ph - 1, fill)
        if prev is not None:
            px, py = prev
            for xx in range(px, x + 1):
                yy = py + (y - py) * (xx - px) // max(x - px, 1)
                cv.put(xx, yy, ink)
                cv.put(xx, yy + 1, ink)
        else:
            cv.put(x, y, ink)
        prev = (x, y)
    cv.save(path)


def colormap(t):
    """0..1 -> RGB, dark to bright (black, purple, orange, yellow, white)."""
    stops = [(0.0, (8, 8, 20)), (0.3, (90, 20, 120)), (0.6, (230, 90, 30)),
             (0.85, (250, 210, 60)), (1.0, (255, 255, 240))]
    t = max(0.0, min(1.0, t))
    for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
        if t <= t1:
            u = (t - t0) / (t1 - t0)
            return tuple(int(c0[i] + (c1[i] - c0[i]) * u) for i in range(3))
    return stops[-1][1]


def plot_spectrogram(path, cols, rate, hop, db_lo=-90.0, db_hi=-10.0):
    nbins = len(cols[0])
    cw, ch = 2, 2                                   # pixels per column / bin
    L, R, T, B = 60, 20, 20, 40
    W, H = L + len(cols) * cw + R, T + nbins * ch + B
    cv = Canvas(W, H, (250, 250, 250))
    txt, axis = (60, 60, 60), (90, 90, 90)
    for ci, col in enumerate(cols):
        for k, d in enumerate(col):
            c = colormap((d - db_lo) / (db_hi - db_lo))
            x0 = L + ci * cw
            y0 = T + (nbins - 1 - k) * ch
            for dy in range(ch):
                for dx in range(cw):
                    cv.put(x0 + dx, y0 + dy, c)
    # axes: seconds along the bottom, kHz on the left
    seconds = len(cols) * hop / rate
    for s in range(0, int(seconds) + 1):
        x = L + int(s * rate / hop) * cw
        cv.vline(x, T + nbins * ch, T + nbins * ch + 4, axis)
        cv.text(x - 3, T + nbins * ch + 8, f"{s}s", txt)
    for khz in range(0, int(rate / 2000) + 1):
        k = int(khz * 1000 / (rate / 2) * (nbins - 1))
        y = T + (nbins - 1 - k) * ch
        cv.hline(L - 4, L, y, axis)
        cv.text(4, y - 5, f"{khz}kHz", txt)
    cv.save(path)


# ------------------------------------------------------------------ main --

def main():
    ap = argparse.ArgumentParser(description="FFT / spectrogram of a WAV capture (stdlib only)")
    ap.add_argument("--wav", default=os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                                                  "captures", "mic_test.wav"))
    ap.add_argument("--nfft", type=int, default=4096)
    args = ap.parse_args()

    with wave.open(args.wav, "rb") as w:
        ch, sw, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        pcm = w.readframes(n)
    if ch != 1 or sw != 2:
        sys.exit(f"expected mono 16-bit WAV, got {ch} ch / {8 * sw}-bit")
    samples = list(struct.unpack("<%dh" % n, pcm))
    mean = sum(samples) / n
    samples = [s - mean for s in samples]          # remove DC before analysis

    base = os.path.splitext(args.wav)[0]
    fft_png, fft_csv, spec_png = base + "_fft.png", base + "_fft.csv", base + "_spectrogram.png"

    freqs, db = welch(samples, rate, args.nfft, args.nfft // 2)
    with open(fft_csv, "w") as f:
        f.write("frequency_hz,level_dbfs\n")
        for fr, d in zip(freqs, db):
            f.write(f"{fr:.3f},{d:.2f}\n")
    plot_spectrum(fft_png, freqs, db, rate / 2)

    cols = spectrogram(samples, 512, 256)
    plot_spectrogram(spec_png, cols, rate, 256)

    # ---- summary numbers ----
    floor = sorted(db)[len(db) // 2]
    binw = rate / args.nfft
    peaks = []
    for k in range(2, len(db) - 2):
        if db[k] > db[k - 1] and db[k] >= db[k + 1] and db[k] > floor + 20:
            peaks.append((db[k], freqs[k]))
    peaks.sort(reverse=True)
    picked = []
    for d, fr in peaks:
        if all(abs(fr - p[1]) >= 60 for p in picked):
            picked.append((d, fr))
        if len(picked) == 8:
            break

    def band_db(lo, hi):
        p = sum(10 ** (d / 10.0) for fr, d in zip(freqs, db) if lo <= fr < hi)
        return 10 * math.log10(max(p, 1e-30))

    centroid = (sum(fr * 10 ** (d / 10.0) for fr, d in zip(freqs, db))
                / max(sum(10 ** (d / 10.0) for d in db), 1e-30))

    windows = len(range(0, n - args.nfft + 1, args.nfft // 2))
    print(f"WAV: {args.wav}")
    print(f"samples={n}  rate={rate} Hz  duration={n / rate:.3f} s  DC removed={mean:.1f}")
    print(f"FFT: {args.nfft}-point Hann, {binw:.2f} Hz per bin, {windows} windows averaged")
    print(f"SPECTRUM PNG: {fft_png}")
    print(f"SPECTRUM CSV: {fft_csv}")
    print(f"SPECTROGRAM PNG: {spec_png}")
    print(f"noise floor (median bin): {floor:.1f} dBFS")
    print(f"spectral centroid: {centroid:.0f} Hz")
    print(f"band level    0-300 Hz: {band_db(0, 300):.1f} dBFS")
    print(f"band level 300-3400 Hz: {band_db(300, 3400):.1f} dBFS   (voice band)")
    print(f"band level 3400-8000 Hz: {band_db(3400, 8000):.1f} dBFS")
    print("strongest peaks (Hz, dBFS):")
    for d, fr in picked:
        print(f"   {fr:8.1f} Hz   {d:6.1f} dBFS")


if __name__ == "__main__":
    main()
