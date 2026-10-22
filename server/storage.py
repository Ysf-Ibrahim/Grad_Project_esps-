"""Capture storage: captures/<node_id>/<YYYY-MM-DD_HH-MM-SS>.wav + .json sidecar."""
import json
import os
import re
import threading
import time
import wave
from collections import OrderedDict

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CAPTURE_DIR = os.path.join(PROJECT, "captures")

_ID_RE = re.compile(r"^([A-Za-z0-9_-]{1,15})/(\d{4}-\d{2}-\d{2}_\d{2}-\d{2}-\d{2}(?:_\d+)?)$")
_lock = threading.Lock()
_chart_cache = OrderedDict()
_CACHE_MAX = 8


def _split(capture_id):
    m = _ID_RE.match(capture_id or "")
    if not m:
        raise ValueError("bad capture id")
    return m.group(1), m.group(2)


def wav_path(capture_id):
    node, stamp = _split(capture_id)
    return os.path.join(CAPTURE_DIR, node, stamp + ".wav")


def json_path(capture_id):
    node, stamp = _split(capture_id)
    return os.path.join(CAPTURE_DIR, node, stamp + ".json")


def save(node_id, pcm, rate, sidecar):
    if not re.match(r"^[A-Za-z0-9_-]{1,15}$", node_id):
        raise ValueError("bad node id")
    folder = os.path.join(CAPTURE_DIR, node_id)
    os.makedirs(folder, exist_ok=True)
    with _lock:
        stamp = time.strftime("%Y-%m-%d_%H-%M-%S")
        candidate = stamp
        k = 1
        while os.path.exists(os.path.join(folder, candidate + ".wav")):
            k += 1
            candidate = f"{stamp}_{k}"
        capture_id = f"{node_id}/{candidate}"
        with wave.open(os.path.join(folder, candidate + ".wav"), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(rate)
            w.writeframes(pcm)
        sidecar = dict(sidecar)
        sidecar["id"] = capture_id
        sidecar["node_id"] = node_id
        sidecar["time"] = time.strftime("%Y-%m-%d %H:%M:%S")
        with open(os.path.join(folder, candidate + ".json"), "w", encoding="utf-8") as f:
            json.dump(sidecar, f, indent=1)
    return capture_id


def load_sidecar(capture_id):
    with open(json_path(capture_id), encoding="utf-8") as f:
        return json.load(f)


def load_pcm(capture_id):
    with wave.open(wav_path(capture_id), "rb") as w:
        return w.readframes(w.getnframes()), w.getframerate()


def list_captures(node_id=None):
    out = []
    if not os.path.isdir(CAPTURE_DIR):
        return out
    nodes = [node_id] if node_id else sorted(os.listdir(CAPTURE_DIR))
    for node in nodes:
        folder = os.path.join(CAPTURE_DIR, node)
        if not os.path.isdir(folder):
            continue
        for name in sorted(os.listdir(folder), reverse=True):
            if not name.endswith(".json"):
                continue
            try:
                with open(os.path.join(folder, name), encoding="utf-8") as f:
                    side = json.load(f)
            except (OSError, ValueError):
                continue
            st = side.get("stats", {})
            out.append({
                "id": side.get("id", f"{node}/{name[:-5]}"),
                "node_id": node,
                "time": side.get("time"),
                "label": side.get("label", ""),
                "seconds": st.get("seconds"),
                "peak_dbfs": st.get("peak_dbfs"),
                "rms_dbfs": st.get("rms_ac_dbfs"),
                "dominant_hz": st.get("dominant_hz"),
                "result": st.get("result"),
                "clip": st.get("clip"),
            })
    out.sort(key=lambda c: c["time"] or "", reverse=True)
    return out


def cached_charts(capture_id, compute):
    with _lock:
        if capture_id in _chart_cache:
            _chart_cache.move_to_end(capture_id)
            return _chart_cache[capture_id]
    data = compute()
    with _lock:
        _chart_cache[capture_id] = data
        while len(_chart_cache) > _CACHE_MAX:
            _chart_cache.popitem(last=False)
    return data
