"""TCP client for the node command protocol (PROTOCOL.md section 3)."""
import json
import socket
import zlib


class NodeError(Exception):
    pass


def _readline(f, what):
    try:
        raw = f.readline()
    except socket.timeout:
        raise NodeError(f"timeout while waiting for {what}")
    if not raw:
        raise NodeError(f"connection closed while waiting for {what}")
    return raw.decode("latin-1").rstrip("\r\n")


def _expect_ok(line):
    if line.startswith("ERR"):
        raise NodeError(line[3:].strip() or "node error")
    if not line.startswith("OK"):
        raise NodeError(f"unexpected reply: {line[:80]}")
    return line[2:].strip()


def _fields(text):
    out = {}
    for tok in text.split():
        k, _, v = tok.partition("=")
        if k:
            out[k] = v
    return out


def status(ip, port=5010, timeout=3.0):
    with socket.create_connection((ip, port), timeout=timeout) as s:
        s.settimeout(timeout)
        s.sendall(b"STATUS\n")
        f = s.makefile("rb")
        payload = _expect_ok(_readline(f, "STATUS reply"))
    return json.loads(payload)


def probe(ip, port=5010, timeout=5.0):
    with socket.create_connection((ip, port), timeout=timeout) as s:
        s.settimeout(timeout)
        s.sendall(b"PROBE\n")
        f = s.makefile("rb")
        payload = _expect_ok(_readline(f, "PROBE reply"))
    return _fields(payload)


def capture(ip, port, seconds, progress=None):
    """Run CAPTURE <seconds>. Returns (pcm_bytes, node_stats, meta).

    progress(phase, done, total) is called with phase 'recording' (ms) and
    'transferring' (bytes).
    """
    def report(phase, done, total):
        if progress:
            progress(phase, done, total)

    with socket.create_connection((ip, port), timeout=3.0) as s:
        s.settimeout(3.0)
        s.sendall(f"CAPTURE {int(seconds)}\n".encode())
        f = s.makefile("rb")
        head = _fields(_expect_ok(_readline(f, "capture acknowledgement")))
        samples = int(head.get("samples", 0))
        rate = int(head.get("rate", 16000))
        expected_bytes = int(head.get("bytes", samples * 2))
        report("recording", 0, seconds * 1000)

        stats = {}
        data_len = None
        while True:
            line = _readline(f, "recording progress")
            if line.startswith("PROG "):
                parts = line.split()
                if len(parts) >= 3:
                    report("recording", int(parts[1]), int(parts[2]))
            elif line.startswith("STAT "):
                k, _, v = line[5:].partition("=")
                stats[k.strip()] = v.strip()
            elif line.startswith("DATA "):
                data_len = int(line.split()[1])
                break
            elif line.startswith("ERR"):
                raise NodeError(line[3:].strip())
            # anything else is ignored

        if data_len != expected_bytes:
            raise NodeError(f"node announced {expected_bytes} bytes but sends {data_len}")

        s.settimeout(10.0)
        chunks = []
        got = 0
        report("transferring", 0, data_len)
        while got < data_len:
            try:
                chunk = f.read(min(65536, data_len - got))
            except socket.timeout:
                raise NodeError(f"timeout during transfer at {got}/{data_len} bytes")
            if not chunk:
                raise NodeError(f"connection closed during transfer at {got}/{data_len} bytes")
            chunks.append(chunk)
            got += len(chunk)
            report("transferring", got, data_len)
        pcm = b"".join(chunks)

        end = _readline(f, "END line")
        if not end.startswith("END"):
            raise NodeError(f"expected END, got: {end[:60]}")
        crc_txt = _fields(end[3:].strip()).get("crc32")
        if crc_txt is not None:
            crc = zlib.crc32(pcm) & 0xFFFFFFFF
            if crc != int(crc_txt, 16):
                raise NodeError(f"CRC32 mismatch (pc {crc:08x}, node {crc_txt})")

    meta = {"samples": samples, "rate": rate, "bytes": data_len, "crc32": crc_txt}
    return pcm, stats, meta
