"""Reader for fc5mp capture files.

Framing (client/src/capture/record.cpp), little-endian:
    [u64 ts_ns][u8 kind][u32 len][len bytes]
Kind codes mirror client/src/capture/kinds.hpp; UDP record layout mirrors
client/src/capture/udp_frame.hpp.
"""
from __future__ import annotations

import ipaddress
import struct
from dataclasses import dataclass

KIND_NAMES = {
    1: "boot",
    10: "rdv_createsession", 11: "coop_leash", 12: "spawn_player",
    13: "ssl_read", 14: "ssl_write",
    20: "udp_send", 21: "udp_recv", 22: "tls_write", 23: "tls_read",
    30: "api_seen", 31: "recv_pending",
    250: "module_timeout", 251: "sig_missing", 252: "hook_failed",
}
ADDRESS_KINDS = {10: "rdv_createsession", 11: "create_coop_leash", 12: "do_spawn_player",
                 13: "ssl_read", 14: "ssl_write"}
# index order of client/src/client/targets.cpp table()
TARGET_NAMES = ["rdv_createsession", "create_coop_leash", "do_spawn_player", "ssl_read", "ssl_write"]
API_NAMES = ["sendto", "recvfrom", "WSASendTo", "WSARecvFrom"]
# HOOK_FAILED api ids beyond the ws2_32 table (API_NAMES, 0-3): 4-5 TLS
# (tls_hooks.hpp), 6 GetOverlappedResult (net_hooks.hpp, P1.5c).
TLS_API_NAMES = {4: "SSL_read", 5: "SSL_write", 6: "GetOverlappedResult"}
BOOT_STAGES = {0: "boot_start", 1: "version_lock", 2: "udp_hooks", 3: "tls_hooks"}

_HDR = struct.Struct("<QBI")


@dataclass(frozen=True)
class Frame:
    ts_ns: int
    kind: int
    payload: bytes


@dataclass(frozen=True)
class Udp:
    sock: int
    family: int
    port: int
    addr: str
    orig_len: int
    payload: bytes


def parse(data: bytes) -> tuple[list[Frame], int]:
    """All whole frames, plus the count of trailing bytes that don't form one
    (a torn final frame from a crash mid-write)."""
    frames, off = [], 0
    while off + _HDR.size <= len(data):
        ts, kind, ln = _HDR.unpack_from(data, off)
        end = off + _HDR.size + ln
        if end > len(data):
            break
        frames.append(Frame(ts, kind, data[off + _HDR.size:end]))
        off = end
    return frames, len(data) - off


class BadRecord(ValueError):
    """A frame payload that doesn't match its kind's layout."""


def decode_udp(p: bytes) -> Udp:
    if len(p) < 13 or p[12] > 16 or len(p) < 17 + p[12]:
        raise BadRecord(f"udp record of {len(p)} bytes")
    sock, family = struct.unpack_from("<QH", p, 0)
    port = int.from_bytes(p[10:12], "big")
    alen = p[12]
    raw = p[13:13 + alen]
    (orig,) = struct.unpack_from("<I", p, 13 + alen)
    addr = str(ipaddress.ip_address(raw)) if alen in (4, 16) else ""
    return Udp(sock, family, port, addr, orig, p[17 + alen:])


def _note_request(st: dict, body: bytes) -> None:
    """Record the first HTTP request line and Host header seen on a stream."""
    if st["request_line"] is not None or b"\r\n" not in body:
        return
    head = body.split(b"\r\n\r\n", 1)[0]
    lines = head.split(b"\r\n")
    first = lines[0].decode("latin-1", "replace")
    if " HTTP/" not in first:          # not the start of a request (mid-stream write)
        return
    st["request_line"] = first
    for ln in lines[1:]:
        if ln[:5].lower() == b"host:":
            st["host"] = ln[5:].strip().decode("latin-1", "replace")
            break


def _peer_key(u: Udp) -> str:
    if not u.addr:
        return f"sock{u.sock:#x}"
    return f"[{u.addr}]:{u.port}" if u.family == 23 else f"{u.addr}:{u.port}"


def summarize(frames: list[Frame]) -> dict:
    s: dict = {"counts": {}, "addresses": {}, "sig_missing": [], "hook_failed": [],
               "apis_seen": [], "module_timeout": False, "recv_pending": 0, "peers": {},
               "tls_streams": {}, "boot": {}, "_boot0_ns": None, "bad_records": 0}
    for f in frames:
        name = KIND_NAMES.get(f.kind, f"kind{f.kind}")
        s["counts"][name] = s["counts"].get(name, 0) + 1
        try:
            _apply(s, f)
        except (BadRecord, struct.error, IndexError):
            s["bad_records"] += 1
    # Phase A smoke test: harness injected, all markers (incl. SSL_read/SSL_write)
    # resolved in the mapped image, every hook installed.
    s["phase_a_ok"] = (len(s["addresses"]) == len(ADDRESS_KINDS) and not s["sig_missing"]
                       and not s["hook_failed"] and not s["module_timeout"])
    s.pop("_boot0_ns", None)
    return s


def _apply(s: dict, f: Frame) -> None:
    if f.kind in ADDRESS_KINDS:
        s["addresses"][ADDRESS_KINDS[f.kind]] = hex(struct.unpack("<Q", f.payload)[0])
    elif f.kind == 251:
        s["sig_missing"].append(TARGET_NAMES[f.payload[0]] if f.payload[0] < len(TARGET_NAMES) else f.payload[0])
    elif f.kind == 252:
        fid = f.payload[0]
        name = API_NAMES[fid] if fid < len(API_NAMES) else TLS_API_NAMES.get(fid, fid)
        s["hook_failed"].append(name)
    elif f.kind == 30:
        s["apis_seen"].append(API_NAMES[f.payload[0]] if f.payload[0] < len(API_NAMES) else f.payload[0])
    elif f.kind == 31:
        # markers carry a running count and are thinned after the first 64,
        # so the true total is the largest count seen
        (n,) = struct.unpack_from("<I", f.payload, 8)
        s["recv_pending"] = max(s["recv_pending"], n)
    elif f.kind == 250:
        s["module_timeout"] = True
    elif f.kind in (20, 21):
        u = decode_udp(f.payload)
        d = "send" if f.kind == 20 else "recv"
        p = s["peers"].setdefault(_peer_key(u), {"send_pkts": 0, "send_bytes": 0,
                                                 "recv_pkts": 0, "recv_bytes": 0})
        p[f"{d}_pkts"] += 1
        p[f"{d}_bytes"] += u.orig_len
    elif f.kind == 1:
        stage = f.payload[0]
        if stage == 0 and s["_boot0_ns"] is None:
            s["_boot0_ns"] = f.ts_ns
        base = s["_boot0_ns"]
        s["boot"][stage] = 0.0 if base is None else round((f.ts_ns - base) / 1e9, 3)
    elif f.kind in (22, 23):
        if len(f.payload) < 8:
            raise BadRecord(f"tls frame of {len(f.payload)} bytes")
        (ssl,) = struct.unpack_from("<Q", f.payload, 0)
        body = f.payload[8:]
        st = s["tls_streams"].setdefault(
            ssl, {"out_bytes": 0, "in_bytes": 0, "request_line": None, "host": None})
        if f.kind == 22:          # tls_write: client -> server
            st["out_bytes"] += len(body)
            _note_request(st, body)
        else:
            st["in_bytes"] += len(body)
