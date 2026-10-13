import struct

import capdump


def _frame(k: int, p: bytes = b"", ts: int = 1) -> bytes:
    return struct.pack("<QBI", ts, k, len(p)) + p


def _tls(ssl: int, body: bytes) -> bytes:
    return struct.pack("<Q", ssl) + body


def _write_capture(path) -> None:
    req = b"GET /config HTTP/1.1\r\nHost: x\r\n\r\n"
    path.write_bytes(_frame(22, _tls(0xAA, req)) + _frame(23, _tls(0xAA, b"HTTP/1.1 200 OK\r\n\r\nhi")))


def test_dump_accepts_trailing_slash(tmp_path, capsys):
    cap = tmp_path / "cap.bin"
    _write_capture(cap)
    out = tmp_path / "streams"
    # a trailing slash is what shell tab-completion appends for a directory
    rc = capdump.main(["capdump.py", str(cap), "--dump", str(out) + "/"])
    assert rc == 0
    assert (out / "0x00000000000000aa-out.bin").exists()
    assert (out / "0x00000000000000aa-in.bin").exists()


def test_dump_plain_path(tmp_path):
    cap = tmp_path / "cap.bin"
    _write_capture(cap)
    out = tmp_path / "s2"
    assert capdump.main(["capdump.py", str(cap), "--dump", str(out)]) == 0
    assert sorted(p.name for p in out.iterdir()) == [
        "0x00000000000000aa-in.bin", "0x00000000000000aa-out.bin"]


def test_no_dump_still_summarizes(tmp_path):
    cap = tmp_path / "cap.bin"
    _write_capture(cap)
    assert capdump.main(["capdump.py", str(cap)]) == 0
