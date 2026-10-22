"""Node discovery over UDP 5005 (PROTOCOL.md section 2).

One listener socket receives heartbeats; one sender socket per local IPv4 address
broadcasts the beacon, because on Windows a socket bound to 0.0.0.0 sends a
broadcast out of one arbitrary adapter only.
"""
import json
import re
import socket
import threading
import time

from node_client import status as tcp_status

PORT = 5005
BEACON = b"ACOUSTISCAN-SERVER 1"
BEACON_EVERY = 2.0
REFRESH_IPS_EVERY = 10.0
OFFLINE_AFTER = 8.0


def natural_key(node_id):
    return [int(p) if p.isdigit() else p.lower() for p in re.split(r"(\d+)", node_id)]


def local_ipv4s():
    ips = set()
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ips.add(info[4][0])
    except socket.gaierror:
        pass
    return sorted(ip for ip in ips if not ip.startswith("127."))


class Discovery:
    def __init__(self, log=print):
        self._nodes = {}
        self._lock = threading.Lock()
        self._senders = {}
        self._log = log
        self.local_ips = []
        self._thread = None

    # ------------------------------------------------------------ lifecycle --
    def start(self):
        self._thread = threading.Thread(target=self._run, name="discovery", daemon=True)
        self._thread.start()

    def _run(self):
        listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(("", PORT))
        listener.settimeout(0.2)
        last_beacon = 0.0
        last_refresh = 0.0
        while True:
            now = time.time()
            if now - last_refresh >= REFRESH_IPS_EVERY:
                last_refresh = now
                self._refresh_senders()
            if now - last_beacon >= BEACON_EVERY:
                last_beacon = now
                self._beacon()
            try:
                data, addr = listener.recvfrom(4096)
            except socket.timeout:
                continue
            except OSError:
                time.sleep(0.2)
                continue
            self._handle(data, addr)

    def _refresh_senders(self):
        ips = local_ipv4s()
        self.local_ips = ips
        for ip in ips:
            if ip in self._senders:
                continue
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            try:
                s.bind((ip, 0))
                self._senders[ip] = s
            except OSError:
                s.close()
        for ip in list(self._senders):
            if ip not in ips:
                self._senders.pop(ip).close()

    def _beacon(self):
        for ip, s in list(self._senders.items()):
            try:
                s.sendto(BEACON, ("255.255.255.255", PORT))
            except OSError:
                self._senders.pop(ip, None)

    # ------------------------------------------------------------- registry --
    def _handle(self, data, addr):
        if data.startswith(BEACON[:18]):
            return
        try:
            hb = json.loads(data.decode("latin-1"))
        except ValueError:
            return
        node_id = hb.get("id")
        if not isinstance(node_id, str) or not node_id:
            return
        self.register(hb, addr[0])

    def register(self, hb, ip):
        now = time.time()
        with self._lock:
            entry = self._nodes.get(hb["id"], {"first_seen": now})
            entry.update(hb)
            entry["ip"] = ip                      # trust the UDP/TCP source, not the payload
            entry["port"] = int(hb.get("port", 5010))
            entry["last_seen"] = now
            if entry.get("first_seen", now) == now and hb["id"] not in self._nodes:
                self._log(f"[discovery] node {hb['id']} ({hb.get('name', '')!r}) at {ip}")
            self._nodes[hb["id"]] = entry

    def nodes(self):
        now = time.time()
        with self._lock:
            out = []
            for node_id in sorted(self._nodes, key=natural_key):
                e = dict(self._nodes[node_id])
                age = now - e["last_seen"]
                e["online"] = age <= OFFLINE_AFTER
                e["last_seen_ago"] = round(age, 1)
                out.append(e)
            return out

    def get(self, node_id):
        with self._lock:
            e = self._nodes.get(node_id)
            if e is None:
                return None
            e = dict(e)
            e["online"] = time.time() - e["last_seen"] <= OFFLINE_AFTER
            return e

    def add_by_ip(self, ip, port=5010):
        """Manual registration: ask the node for STATUS over TCP."""
        hb = tcp_status(ip, port)
        self.register(hb, ip)
        return hb
