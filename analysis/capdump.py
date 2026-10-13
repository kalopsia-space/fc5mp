"""Summarize an fc5mp capture file.

    uv run python capdump.py ~/.fc5mp/captures/session-host-<ts>.bin

Prints the Phase A checklist (markers resolved, hooks installed, which UDP
APIs the game used) and per-peer UDP packet/byte counts.
"""
import sys
from pathlib import Path

from fc5analyze.capture import parse, summarize


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__.strip())
        return 2
    frames, trailing = parse(Path(argv[1]).read_bytes())
    s = summarize(frames)
    print(f"frames: {len(frames)}" + (f"  (+{trailing} trailing bytes: torn final frame)" if trailing else ""))
    print(f"Phase A: {'OK' if s['phase_a_ok'] else 'PROBLEM'}")
    for name in ("rdv_createsession", "create_coop_leash", "do_spawn_player"):
        print(f"  {name:20} {s['addresses'].get(name, 'NOT RESOLVED')}")
    if s["module_timeout"]:
        print("  FC_m64.dll never mapped within 20s")
    for k in ("sig_missing", "hook_failed"):
        if s[k]:
            print(f"  {k}: {', '.join(map(str, s[k]))}")
    print(f"UDP APIs used: {', '.join(map(str, s['apis_seen'])) or 'none'}")
    if s["recv_pending"]:
        print(f"  overlapped WSARecvFrom calls: {s['recv_pending']} (recv data NOT captured for these)")
    if s["bad_records"]:
        print(f"  malformed records skipped: {s['bad_records']}")
    print(f"UDP peers: {len(s['peers'])}")
    for peer, c in sorted(s["peers"].items(), key=lambda kv: -(kv[1]["send_bytes"] + kv[1]["recv_bytes"])):
        print(f"  {peer:45} send {c['send_pkts']:6} pkts {c['send_bytes']:9} B   "
              f"recv {c['recv_pkts']:6} pkts {c['recv_bytes']:9} B")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
