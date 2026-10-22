"""AcoustiScan dashboard server.

  .venv\\Scripts\\python server\\app.py            -> http://127.0.0.1:8080
  .venv\\Scripts\\python server\\app.py --host 0.0.0.0 --no-browser
"""
import argparse
import os
import socket
import sys
import threading
import time
import uuid
import webbrowser

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from flask import Flask, jsonify, request, send_file, send_from_directory  # noqa: E402

import analysis  # noqa: E402
import node_client  # noqa: E402
import storage  # noqa: E402
from discovery import Discovery  # noqa: E402

STATIC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "static")
app = Flask(__name__, static_folder=None)
discovery = Discovery(log=lambda m: print(m, flush=True))

_jobs = {}
_jobs_lock = threading.Lock()
_node_locks = {}
_node_locks_guard = threading.Lock()
_started = time.time()


def _node_lock(node_id):
    with _node_locks_guard:
        return _node_locks.setdefault(node_id, threading.Lock())


def _set_job(job_id, **fields):
    with _jobs_lock:
        job = _jobs.setdefault(job_id, {"id": job_id, "created": time.time()})
        job.update(fields)
        job["updated"] = time.time()
        return dict(job)


def _run_job(job_id, node, seconds, label):
    lock = _node_lock(node["id"])
    if not lock.acquire(blocking=False):
        _set_job(job_id, phase="error", message="this node is already recording", percent=0)
        return
    try:
        _set_job(job_id, phase="connecting", percent=0, message=f"Connecting to {node['ip']}")

        def progress(phase, done, total):
            pct = int(100 * done / total) if total else 0
            if phase == "recording":
                _set_job(job_id, phase="recording", percent=pct,
                         message=f"Recording {done / 1000:.1f} / {total / 1000:.1f} s")
            else:
                _set_job(job_id, phase="transferring", percent=pct,
                         message=f"Transferring {done // 1024} / {total // 1024} KB")

        t0 = time.time()
        pcm, node_stats, meta = node_client.capture(node["ip"], node["port"], seconds, progress)
        t_transfer = time.time() - t0
        _set_job(job_id, phase="analyzing", percent=100, message="Analyzing")
        stats = analysis.summary(pcm, meta["rate"], node_stats)
        sidecar = {
            "label": label,
            "seconds_requested": seconds,
            "node": {k: node.get(k) for k in ("id", "name", "ip", "rssi", "fw", "mic")},
            "node_stats": node_stats,
            "meta": meta,
            "stats": stats,
            "transfer_s": round(t_transfer - seconds, 2),
        }
        capture_id = storage.save(node["id"], pcm, meta["rate"], sidecar)
        _set_job(job_id, phase="done", percent=100, capture_id=capture_id,
                 message=f"Saved captures/{capture_id}.wav")
    except (node_client.NodeError, OSError, ValueError) as exc:
        _set_job(job_id, phase="error", message=str(exc))
    finally:
        lock.release()


# ------------------------------------------------------------------ routes --

@app.get("/")
def index():
    return send_from_directory(STATIC, "index.html")


@app.get("/static/<path:name>")
def static_file(name):
    return send_from_directory(STATIC, name)


@app.get("/api/info")
def api_info():
    return jsonify({
        "hostname": socket.gethostname(),
        "local_ips": discovery.local_ips,
        "uptime_s": int(time.time() - _started),
        "server_time": time.strftime("%Y-%m-%d %H:%M:%S"),
    })


@app.get("/api/nodes")
def api_nodes():
    return jsonify(discovery.nodes())


@app.post("/api/nodes/add")
def api_nodes_add():
    body = request.get_json(silent=True) or {}
    ip = (body.get("ip") or "").strip()
    if not ip:
        return jsonify({"error": "ip required"}), 400
    try:
        hb = discovery.add_by_ip(ip, int(body.get("port") or 5010))
    except (node_client.NodeError, OSError, ValueError) as exc:
        return jsonify({"error": f"no node answered at {ip}: {exc}"}), 502
    return jsonify(hb)


@app.post("/api/capture")
def api_capture():
    body = request.get_json(silent=True) or {}
    node_id = body.get("node_id")
    try:
        seconds = int(body.get("seconds", 10))
    except (TypeError, ValueError):
        return jsonify({"error": "seconds must be a number"}), 400
    if seconds not in (1, 5, 10, 30):
        return jsonify({"error": "seconds must be 1, 5, 10 or 30"}), 400
    label = str(body.get("label", ""))[:80]
    node = discovery.get(node_id) if node_id else None
    if node is None:
        return jsonify({"error": "unknown node"}), 404
    if not node["online"]:
        return jsonify({"error": f"{node_id} is offline"}), 409
    if _node_lock(node_id).locked():
        return jsonify({"error": f"{node_id} is already recording"}), 409
    job_id = uuid.uuid4().hex[:12]
    _set_job(job_id, node_id=node_id, seconds=seconds, label=label, phase="queued", percent=0, message="Starting")
    threading.Thread(target=_run_job, args=(job_id, node, seconds, label), daemon=True).start()
    return jsonify({"job_id": job_id})


@app.get("/api/jobs/<job_id>")
def api_job(job_id):
    with _jobs_lock:
        job = _jobs.get(job_id)
        # forget finished jobs after 10 minutes
        for k in [k for k, j in _jobs.items() if time.time() - j["updated"] > 600]:
            _jobs.pop(k, None)
    if job is None:
        return jsonify({"error": "unknown job"}), 404
    return jsonify(job)


@app.get("/api/captures")
def api_captures():
    node_id = request.args.get("node")
    return jsonify(storage.list_captures(node_id))


@app.get("/api/captures/<path:capture_id>/wav")
def api_capture_wav(capture_id):
    try:
        path = storage.wav_path(capture_id)
    except ValueError:
        return jsonify({"error": "bad id"}), 400
    if not os.path.exists(path):
        return jsonify({"error": "not found"}), 404
    return send_file(path, mimetype="audio/wav", as_attachment=True,
                     download_name=capture_id.replace("/", "_") + ".wav")


@app.get("/api/captures/<path:capture_id>")
def api_capture_get(capture_id):
    try:
        side = storage.load_sidecar(capture_id)
    except ValueError:
        return jsonify({"error": "bad id"}), 400
    except OSError:
        return jsonify({"error": "not found"}), 404

    def compute():
        pcm, rate = storage.load_pcm(capture_id)
        return analysis.charts(pcm, rate)

    side["charts"] = storage.cached_charts(capture_id, compute)
    return jsonify(side)


def main():
    ap = argparse.ArgumentParser(description="AcoustiScan dashboard server")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--no-browser", action="store_true")
    args = ap.parse_args()

    discovery.start()
    url = f"http://{'127.0.0.1' if args.host == '0.0.0.0' else args.host}:{args.port}"
    print(f"AcoustiScan dashboard: {url}   (captures in {storage.CAPTURE_DIR})", flush=True)
    if not args.no_browser:
        threading.Timer(1.0, lambda: webbrowser.open(url)).start()
    app.run(host=args.host, port=args.port, threaded=True, debug=False, use_reloader=False)


if __name__ == "__main__":
    main()
