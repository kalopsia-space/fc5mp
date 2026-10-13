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
    10: "rdv_createsession", 11: "coop_leash", 12: "spawn_player",
    20: "udp_send", 21: "udp_recv",
    30: "api_seen", 31: "recv_pending",
    250: "module_timeout", 251: "sig_missing", 252: "hook_failed",
}
ADDRESS_KINDS = {10: "rdv_createsession", 11: "create_coop_leash", 12: "do_spawn_player"}
# index order of client/src/client/targets.cpp table()
TARGET_NAMES = ["rdv_createsession", "create_coop_leash", "do_spawn_player"]
API_NAMES = ["sendto", "recvfrom", "WSASendTo", "WSARecvFrom"]

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


def decode_udp(p: bytes) -> Udp:
    sock, family = struct.unpack_from("<QH", p, 0)
    port = int.from_bytes(p[10:12], "big")
    alen = p[12]
    raw = p[13:13 + alen]
    (orig,) = struct.unpack_from("<I", p, 13 + alen)
    addr = str(ipaddress.ip_address(raw)) if alen in (4, 16) else ""
    return Udp(sock, family, port, addr, orig, p[17 + alen:])


def _peer_key(u: Udp) -> str:
    if not u.addr:
        return f"sock{u.sock:#x}"
    return f"[{u.addr}]:{u.port}" if u.family == 23 else f"{u.addr}:{u.port}"


def summarize(frames: list[Frame]) -> dict:
    s: dict = {"counts": {}, "addresses": {}, "sig_missing": [], "hook_failed": [],
               "apis_seen": [], "module_timeout": False, "recv_pending": 0, "peers": {}}
    for f in frames:
        name = KIND_NAMES.get(f.kind, f"kind{f.kind}")
        s["counts"][name] = s["counts"].get(name, 0) + 1
        if f.kind in ADDRESS_KINDS:
            s["addresses"][ADDRESS_KINDS[f.kind]] = hex(struct.unpack("<Q", f.payload)[0])
        elif f.kind == 251:
            s["sig_missing"].append(TARGET_NAMES[f.payload[0]] if f.payload[0] < len(TARGET_NAMES) else f.payload[0])
        elif f.kind == 252:
            s["hook_failed"].append(API_NAMES[f.payload[0]] if f.payload[0] < len(API_NAMES) else f.payload[0])
        elif f.kind == 30:
            s["apis_seen"].append(API_NAMES[f.payload[0]] if f.payload[0] < len(API_NAMES) else f.payload[0])
        elif f.kind == 31:
            s["recv_pending"] += 1
        elif f.kind == 250:
            s["module_timeout"] = True
        elif f.kind in (20, 21):
            u = decode_udp(f.payload)
            d = "send" if f.kind == 20 else "recv"
            p = s["peers"].setdefault(_peer_key(u), {"send_pkts": 0, "send_bytes": 0,
                                                     "recv_pkts": 0, "recv_bytes": 0})
            p[f"{d}_pkts"] += 1
            p[f"{d}_bytes"] += u.orig_len
    # Phase A smoke test: harness injected, all markers resolved in the mapped
    # image, every hook installed.
    s["phase_a_ok"] = (len(s["addresses"]) == len(ADDRESS_KINDS) and not s["sig_missing"]
                       and not s["hook_failed"] and not s["module_timeout"])
    return s
