import struct

from fc5analyze.capture import decode_udp, parse, summarize


def frame(kind: int, payload: bytes = b"", ts: int = 1) -> bytes:
    return struct.pack("<QBI", ts, kind, len(payload)) + payload


def udp(sock: int, family: int, port: int, addr: bytes, data: bytes, orig: int | None = None) -> bytes:
    return (struct.pack("<QH", sock, family) + port.to_bytes(2, "big") + bytes([len(addr)]) + addr
            + struct.pack("<I", len(data) if orig is None else orig) + data)


def test_frames_round_trip():
    data = frame(10, struct.pack("<Q", 0x18A000000), ts=5) + frame(250)
    frames, trailing = parse(data)
    assert trailing == 0
    assert [(f.ts_ns, f.kind) for f in frames] == [(5, 10), (1, 250)]
    assert frames[0].payload == struct.pack("<Q", 0x18A000000)


def test_torn_tail():
    data = frame(20, b"x" * 10) + frame(21, b"y" * 10)
    frames, trailing = parse(data[:-4])
    assert len(frames) == 1 and trailing == 13 + 6
    frames, trailing = parse(data + b"\x00\x01")      # partial header
    assert len(frames) == 2 and trailing == 2


def test_decode_udp_v4_and_v6():
    u = decode_udp(udp(7, 2, 3074, bytes([192, 168, 1, 7]), b"abc"))
    assert (u.sock, u.family, u.port, u.addr, u.orig_len, u.payload) == (7, 2, 3074, "192.168.1.7", 3, b"abc")
    u6 = decode_udp(udp(8, 23, 9000, bytes(range(0xA0, 0xB0)), b""))
    assert u6.family == 23 and u6.addr.startswith("a0a1:") and u6.port == 9000
    unk = decode_udp(udp(9, 0, 0, b"", b"zz", orig=70000))
    assert unk.addr == "" and unk.orig_len == 70000 and unk.payload == b"zz"


def test_summarize_peers_and_phase_a_verdict():
    peer = bytes([10, 0, 0, 2])
    frames, _ = parse(
        frame(10, struct.pack("<Q", 1)) + frame(11, struct.pack("<Q", 2)) + frame(12, struct.pack("<Q", 3))
        + frame(30, b"\x02")
        + frame(20, udp(1, 2, 3074, peer, b"a" * 4))
        + frame(20, udp(1, 2, 3074, peer, b"b" * 6))
        + frame(21, udp(1, 2, 3074, peer, b"c" * 5))
    )
    s = summarize(frames)
    assert s["phase_a_ok"] is True
    assert s["apis_seen"] == ["WSASendTo"]
    assert s["peers"]["10.0.0.2:3074"] == {"send_pkts": 2, "send_bytes": 10, "recv_pkts": 1, "recv_bytes": 5}

    bad, _ = parse(frame(10, struct.pack("<Q", 1)) + frame(251, b"\x01") + frame(252, b"\x03"))
    s = summarize(bad)
    assert s["phase_a_ok"] is False
    assert s["sig_missing"] == ["create_coop_leash"] and s["hook_failed"] == ["WSARecvFrom"]
    assert s["addresses"] == {"rdv_createsession": "0x1"}


def test_overlapped_marker_true_count():
    marks = b"".join(frame(31, struct.pack("<QI", 5, n)) for n in (1, 2, 1024, 2048))
    frames, _ = parse(marks)
    assert summarize(frames)["recv_pending"] == 2048


def test_corrupt_records_counted_not_fatal():
    good = udp(1, 2, 80, bytes([1, 2, 3, 4]), b"ok")
    frames, _ = parse(
        frame(20, good)
        + frame(20, b"\x00" * 5)                          # too short
        + frame(21, good[:12] + b"\x11" + b"\x00" * 30)   # addr_len 17
        + frame(10, b"\x01\x02")                          # address frame not 8 bytes
    )
    s = summarize(frames)
    assert s["bad_records"] == 3
    assert s["peers"]["1.2.3.4:80"]["send_pkts"] == 1
