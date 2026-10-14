"""Summarize an fc5mp capture file.

    uv run python capdump.py ~/.fc5mp/captures/session-host-<ts>.bin [--dump DIR]

Prints the Phase A checklist (markers resolved, hooks installed, which UDP
APIs the game used, boot timing), per-peer UDP packet/byte counts, and a
summary of each TLS stream (bytes in/out, first request line + Host).

--dump DIR writes the raw plaintext of every TLS stream to
<ssl>-out.bin / <ssl>-in.bin for offline decoding.

WARNING: TLS plaintext is sensitive. Credentials (Authorization, ticket,
sessionKey, password, 2FA tickets) are masked at capture time, but bodies
that were gzip-compressed or sent as XOR-masked websocket client frames are
NOT redactable — treat any dumped stream as secret.
"""
import struct
import sys
from pathlib import Path

from fc5analyze.capture import BOOT_STAGES, parse, summarize


def main(argv: list[str]) -> int:
    # Pull out --dump DIR by position (not by normalized-string match: str(Path)
    # drops a trailing slash, which tab-completion appends to directories).
    args: list[str] = []
    dump_dir = None
    it = iter(argv[1:])
    for a in it:
        if a == "--dump":
            val = next(it, None)
            if val is None:
                print(__doc__.strip())
                return 2
            dump_dir = Path(val)
        elif not a.startswith("--"):
            args.append(a)
    if len(args) != 1:
        print(__doc__.strip())
        return 2

    frames, trailing = parse(Path(args[0]).read_bytes())
    s = summarize(frames)
    print(f"frames: {len(frames)}" + (f"  (+{trailing} trailing bytes: torn final frame)" if trailing else ""))
    print(f"Phase A: {'OK' if s['phase_a_ok'] else 'PROBLEM'}")
    for name in ("rdv_createsession", "create_coop_leash", "do_spawn_player", "ssl_read", "ssl_write"):
        print(f"  {name:20} {s['addresses'].get(name, 'NOT RESOLVED')}")
    if s["module_timeout"]:
        print("  FC_m64.dll never mapped within 20s")
    for k in ("sig_missing", "hook_failed"):
        if s[k]:
            print(f"  {k}: {', '.join(map(str, s[k]))}")
    if s["boot"]:
        marks = ", ".join(f"{BOOT_STAGES.get(st, st)}=+{sec:g}s" for st, sec in sorted(s["boot"].items()))
        print(f"boot: {marks}")
    print(f"UDP APIs used: {', '.join(map(str, s['apis_seen'])) or 'none'}")
    if s["recv_pending"]:
        print(f"  overlapped WSARecvFrom issued: {s['recv_pending']} "
              f"(data now captured when the game reaps via GetOverlappedResult; "
              f"compare to each peer's recv pkts)")
    if s["bad_records"]:
        print(f"  malformed records skipped: {s['bad_records']}")

    print(f"UDP peers: {len(s['peers'])}")
    for peer, c in sorted(s["peers"].items(), key=lambda kv: -(kv[1]["send_bytes"] + kv[1]["recv_bytes"])):
        print(f"  {peer:45} send {c['send_pkts']:6} pkts {c['send_bytes']:9} B   "
              f"recv {c['recv_pkts']:6} pkts {c['recv_bytes']:9} B")

    print(f"TLS streams: {len(s['tls_streams'])}")
    if s["tls_streams"]:
        print("  (plaintext — creds masked at capture; gzip/wss bodies are NOT, treat as secret)")
    for ssl, st in sorted(s["tls_streams"].items(), key=lambda kv: -(kv[1]["out_bytes"] + kv[1]["in_bytes"])):
        req = st["request_line"] or "(no request line seen)"
        host = f"  Host: {st['host']}" if st["host"] else ""
        print(f"  {ssl:#018x}  out {st['out_bytes']:8} B  in {st['in_bytes']:8} B   {req}{host}")

    if dump_dir is not None:
        dump_dir.mkdir(parents=True, exist_ok=True)
        streams: dict[int, dict[str, bytearray]] = {}
        for f in frames:
            if f.kind in (22, 23) and len(f.payload) >= 8:
                (ssl,) = struct.unpack_from("<Q", f.payload, 0)
                side = streams.setdefault(ssl, {"out": bytearray(), "in": bytearray()})
                side["out" if f.kind == 22 else "in"].extend(f.payload[8:])
        for ssl, side in streams.items():
            for d in ("out", "in"):
                if side[d]:
                    (dump_dir / f"{ssl:#018x}-{d}.bin").write_bytes(bytes(side[d]))
        print(f"dumped {len(streams)} TLS stream(s) to {dump_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
