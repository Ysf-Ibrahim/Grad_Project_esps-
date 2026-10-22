"""Audio analysis with numpy: statistics, Welch spectrum, spectrogram, waveform envelope.

Same conventions as tools/mic_capture.py and tools/wav_fft.py:
  dBFS reference = full-scale sine (amplitude 32768) -> 0 dBFS at its bin.
"""
import math

import numpy as np

FULL_SCALE = 32768.0


def _dbfs(amp):
    return float(20.0 * math.log10(max(float(amp), 1e-9) / FULL_SCALE))


def pcm_to_array(pcm_bytes):
    return np.frombuffer(pcm_bytes, dtype="<i2").astype(np.float64)


def welch(x, rate, nfft=4096, hop=2048):
    if len(x) < nfft:
        x = np.pad(x, (0, nfft - len(x)))
    win = np.hanning(nfft)
    norm = win.sum() / 2.0
    nframes = 1 + (len(x) - nfft) // hop
    idx = np.arange(nfft)[None, :] + hop * np.arange(nframes)[:, None]
    frames = x[idx] * win
    spec = np.fft.rfft(frames, axis=1)
    amp = np.sqrt(((np.abs(spec) / norm) ** 2).mean(axis=0))
    db = 20.0 * np.log10(np.maximum(amp, 1e-9) / FULL_SCALE)
    freqs = np.arange(nfft // 2 + 1) * rate / nfft
    return freqs, db


def spectrogram(x, rate, nfft=512, hop=256, max_cols=240, max_rows=128):
    if len(x) < nfft:
        x = np.pad(x, (0, nfft - len(x)))
    win = np.hanning(nfft)
    norm = win.sum() / 2.0
    nframes = 1 + (len(x) - nfft) // hop
    idx = np.arange(nfft)[None, :] + hop * np.arange(nframes)[:, None]
    spec = np.fft.rfft(x[idx] * win, axis=1)
    db = 20.0 * np.log10(np.maximum(np.abs(spec) / norm, 1e-9) / FULL_SCALE)   # frames x bins
    db = db[:, :-1]                                                              # drop Nyquist -> 256 bins
    # max-pool columns (time) and rows (frequency) to keep the JSON small
    cf = max(1, math.ceil(db.shape[0] / max_cols))
    rf = max(1, math.ceil(db.shape[1] / max_rows))
    t = db.shape[0] // cf * cf
    fbins = db.shape[1] // rf * rf
    pooled = db[:t, :fbins].reshape(t // cf, cf, fbins // rf, rf).max(axis=(1, 3))
    pooled = np.clip(np.rint(pooled), -120, 0).astype(int)
    return {
        "cols": int(pooled.shape[0]),
        "rows": int(pooled.shape[1]),
        "seconds": float(len(x) / rate),
        "fmax": float(rate / 2),
        "db_min": -90,
        "db_max": -20,
        "data": pooled.T[::-1].tolist(),   # rows from high frequency (top) to low (bottom)
    }


def envelope(x, columns=800, rate=16000):
    n = len(x)
    columns = max(1, min(columns, n))
    edges = np.linspace(0, n, columns + 1).astype(int)
    mins = np.empty(columns)
    maxs = np.empty(columns)
    for i in range(columns):
        seg = x[edges[i]:max(edges[i] + 1, edges[i + 1])]
        mins[i] = seg.min()
        maxs[i] = seg.max()
    return {
        "columns": columns,
        "seconds": float(n / rate),
        "min": np.round(mins / FULL_SCALE, 4).tolist(),
        "max": np.round(maxs / FULL_SCALE, 4).tolist(),
    }


def summary(pcm_bytes, rate=16000, node_stats=None):
    """Statistics for the tiles / history (stored in the sidecar)."""
    x = pcm_to_array(pcm_bytes)
    n = len(x)
    if n == 0:
        raise ValueError("empty capture")
    mean = float(x.mean())
    xa = x - mean
    mn, mx = int(x.min()), int(x.max())
    rms = float(np.sqrt(np.mean(x * x)))
    rms_ac = float(np.sqrt(np.mean(xa * xa)))
    peak = int(max(abs(mn), abs(mx)))
    peak_ac = float(np.max(np.abs(xa)))
    distinct = int(np.unique(x).size)
    clip = int(np.count_nonzero(np.abs(x) >= 32700))
    constant = mn == mx
    all_zero = mn == 0 and mx == 0

    win = int(rate * 0.05)
    nwin = n // win
    if nwin >= 1:
        w = xa[: nwin * win].reshape(nwin, win)
        wrms = np.sqrt((w * w).mean(axis=1))
        max_win = float(wrms.max())
        p20_win = float(np.percentile(wrms, 20))
    else:
        max_win = p20_win = rms_ac

    freqs, db = welch(x, rate)
    floor_db = float(np.median(db))
    valid = freqs >= 50
    k = int(np.argmax(np.where(valid, db, -1e9)))
    if 1 <= k < len(db) - 1:
        a, b, c = db[k - 1], db[k], db[k + 1]
        denom = a - 2 * b + c
        delta = 0.5 * (a - c) / denom if denom != 0 else 0.0
    else:
        delta = 0.0
    dominant_hz = float((k + delta) * (freqs[1] - freqs[0]))
    dominant_db = float(db[k])
    power = 10 ** (db / 10.0)
    centroid = float((freqs * power).sum() / max(power.sum(), 1e-30))

    def band(lo, hi):
        sel = (freqs >= lo) & (freqs < hi)
        return float(10 * np.log10(max(power[sel].sum(), 1e-30)))

    capture_ms = _int(node_stats, "capture_ms")
    expected_ms = _int(node_stats, "expected_ms") or int(n / rate * 1000)
    timing_ok = capture_ms is None or (0.94 * expected_ms <= capture_ms <= 1.10 * expected_ms)
    alive = (not constant) and distinct >= 64 and abs(mean) <= 8000 and timing_ok
    sound_event = peak_ac >= 500 and (max_win >= 4.0 * max(p20_win, 1.0) or peak_ac >= 3000)
    mic_alive_probe = _int(node_stats, "mic_alive")

    if constant:
        result = "Mic flat"
    elif clip > 0:
        result = f"Clipped {clip}"
    elif not timing_ok:
        result = "Timing off"
    else:
        result = "OK"

    return {
        "samples": n,
        "rate": rate,
        "seconds": round(n / rate, 3),
        "min": mn, "max": mx, "mean": round(mean, 1),
        "peak": peak, "peak_dbfs": round(_dbfs(peak), 1),
        "peak_ac": round(peak_ac, 1), "peak_ac_dbfs": round(_dbfs(peak_ac), 1),
        "rms": round(rms, 1), "rms_dbfs": round(_dbfs(rms), 1),
        "rms_ac": round(rms_ac, 1), "rms_ac_dbfs": round(_dbfs(rms_ac), 1),
        "crest_db": round(20 * math.log10(max(peak_ac, 1e-9) / max(rms_ac, 1e-9)), 1),
        "distinct": distinct, "clip": clip, "constant": constant, "all_zero": all_zero,
        "noise_floor_dbfs": round(floor_db, 1),
        "dominant_hz": round(dominant_hz, 1), "dominant_dbfs": round(dominant_db, 1),
        "centroid_hz": round(centroid), "band_low_dbfs": round(band(0, 300), 1),
        "band_voice_dbfs": round(band(300, 3400), 1), "band_high_dbfs": round(band(3400, rate / 2), 1),
        "max_window_rms": round(max_win, 1), "quiet_window_rms": round(p20_win, 1),
        "capture_ms": capture_ms, "expected_ms": expected_ms, "timing_ok": timing_ok,
        "alive": bool(alive), "sound_event": bool(sound_event),
        "mic_alive_probe": mic_alive_probe,
        "result": result,
    }


def charts(pcm_bytes, rate=16000):
    x = pcm_to_array(pcm_bytes)
    x = x - x.mean()
    freqs, db = welch(x, rate)
    return {
        "waveform": envelope(pcm_to_array(pcm_bytes), rate=rate),
        "spectrum": {"freqs": np.round(freqs, 2).tolist(), "db": np.round(db, 1).tolist()},
        "spectrogram": spectrogram(x, rate),
    }


def _int(d, key):
    if not d or key not in d:
        return None
    try:
        return int(float(d[key]))
    except (TypeError, ValueError):
        return None
