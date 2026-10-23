#!/usr/bin/env python3
"""MCP server exposing FC5MP game/server automation tools.

Run via stdio:
    python3 /home/kalopsia/fc5mp/mcp/fc5mp_game.py

Tools wrap the existing scripts/game-interact.sh and fc5mp-launch.sh helpers plus
direct process/log management. All paths default to this user's install.
"""

import json
import os
import signal
import subprocess
import time
from pathlib import Path

from mcp.server.fastmcp import FastMCP
from mcp.server.fastmcp import Image as MCPImage

HOME = Path.home()
ROOT = HOME / "fc5mp"
SERVER_DIR = ROOT / "server"
GAME_BIN = HOME / ".local/share/Steam/steamapps/common/FarCry5/bin"
LAUNCH_SCRIPT = ROOT / "scripts" / "fc5mp-launch.sh"
SERVER_SCRIPT = ROOT / "scripts" / "fc5mp-server.sh"
INTERACT = ROOT / "scripts" / "game-interact.sh"
LAUNCH_ENV = HOME / ".fc5mp" / "launch.env"

PID_DIR = Path("/tmp")
SERVER_PID_FILE = PID_DIR / "fc5mp-server.pid"
GAME_PID_FILE = PID_DIR / "fc5mp-game.pid"
SERVER_LOG = PID_DIR / "fc5mp-server.log"
GAME_LOG = PID_DIR / "fc5mp-game.log"

MONITOR = os.environ.get("FC5MP_MONITOR", "DP-3")

mcp = FastMCP("fc5mp-game")


def _run(cmd, timeout=60, cwd=None, env=None):
    try:
        r = subprocess.run(
            cmd, shell=isinstance(cmd, str), capture_output=True, text=True,
            timeout=timeout, cwd=cwd, env=env,
        )
        return r.returncode, r.stdout, r.stderr
    except subprocess.TimeoutExpired as e:
        return 124, e.stdout or "", (e.stderr or "") + "\nTIMEOUT"


def _pids_matching(pattern):
    rc, out, _ = _run(f"pgrep -af {pattern}", timeout=10)
    lines = [l for l in out.splitlines() if l.strip()]
    return lines


@mcp.tool()
def status() -> str:
    """Return server/game process and listener status."""
    info = {}
    info["server_pid_file"] = SERVER_PID_FILE.read_text().strip() if SERVER_PID_FILE.exists() else None
    info["game_pid_file"] = GAME_PID_FILE.read_text().strip() if GAME_PID_FILE.exists() else None
    info["server_process"] = _pids_matching("fc5mp-server|fc5mp_server")[0] if _pids_matching("fc5mp-server|fc5mp_server") else None
    info["game_process"] = _pids_matching("FarCry5.exe")[0] if _pids_matching("FarCry5.exe") else None
    # port checks
    for port in (21000, 6571, 6570):
        rc, out, _ = _run(f"ss -ltnup 2>/dev/null | grep ':{port} '", timeout=5)
        info[f"port_{port}"] = bool(out.strip())
    return json.dumps(info, indent=2)


@mcp.tool()
def server_build(release: bool = False) -> str:
    """Build the Rust server (debug by default)."""
    args = ["cargo", "build"]
    if release:
        args.append("--release")
    rc, out, err = _run(args, timeout=600, cwd=str(SERVER_DIR))
    tail = (out + err).splitlines()[-40:]
    return f"rc={rc}\n" + "\n".join(tail)


@mcp.tool()
def server_test() -> str:
    """Run cargo test for the server."""
    rc, out, err = _run(["cargo", "test"], timeout=600, cwd=str(SERVER_DIR))
    tail = (out + err).splitlines()[-50:]
    return f"rc={rc}\n" + "\n".join(tail)


@mcp.tool()
def server_start(extra_args: str = "") -> str:
    """Start fc5mp-server in the background (native TLS). Builds first if binary missing."""
    binary = SERVER_DIR / "target" / "debug" / "fc5mp-server"
    if not binary.exists():
        rc, out, err = _run(["cargo", "build"], timeout=600, cwd=str(SERVER_DIR))
        if rc != 0:
            return f"build failed rc={rc}\n{(out+err)[-2000:]}"
    # kill any existing
    server_stop()
    time.sleep(0.5)
    args = [str(binary)] + (extra_args.split() if extra_args else [])
    with open(SERVER_LOG, "ab") as f:
        p = subprocess.Popen(args, cwd=str(SERVER_DIR), stdout=f, stderr=subprocess.STDOUT,
                             start_new_session=True)
    SERVER_PID_FILE.write_text(str(p.pid))
    time.sleep(1.0)
    alive = p.poll() is None
    return json.dumps({"pid": p.pid, "alive": alive, "log": str(SERVER_LOG)})


@mcp.tool()
def server_stop() -> str:
    """Stop the fc5mp-server process."""
    if SERVER_PID_FILE.exists():
        try:
            pid = int(SERVER_PID_FILE.read_text().strip())
            os.killpg(os.getpgid(pid), signal.SIGTERM)
        except Exception:
            pass
        SERVER_PID_FILE.unlink(missing_ok=True)
    # fallback
    _run("pkill -f fc5mp-server 2>/dev/null || true", timeout=10)
    _run("pkill -f 'target/debug/fc5mp-server' 2>/dev/null || true", timeout=10)
    return "stopped"


@mcp.tool()
def server_logs(tail_lines: int = 120) -> str:
    """Tail the server log."""
    if not SERVER_LOG.exists():
        return "(no log)"
    lines = SERVER_LOG.read_text(errors="replace").splitlines()
    return "\n".join(lines[-tail_lines:])


@mcp.tool()
def game_install(mode: str = "steam") -> str:
    """Install mod DLLs into the game dir. mode: steam or crack."""
    if mode not in ("steam", "crack", "install"):
        return "invalid mode"
    arg = "install" if mode == "install" else mode
    rc, out, err = _run([str(LAUNCH_SCRIPT), arg], timeout=120)
    return f"rc={rc}\n{out}\n{err}".strip()


@mcp.tool()
def game_uninstall() -> str:
    """Restore vanilla game files."""
    rc, out, err = _run([str(LAUNCH_SCRIPT), "uninstall"], timeout=120)
    return f"rc={rc}\n{out}\n{err}".strip()


@mcp.tool()
def game_launch(mode: str = "wine", extra_args: str = "") -> str:
    """Launch the game. mode: wine (direct, installs+runs+collects), steam (install only, you launch from Steam), full (server+game)."""
    if mode == "steam":
        rc, out, err = _run([str(LAUNCH_SCRIPT), "steam"], timeout=120)
        return f"rc={rc}\n{out}\n{err}\nLaunch from Steam with WINEDLLOVERRIDES set."
    if mode == "full":
        # run in background; full blocks until exit
        with open(GAME_LOG, "ab") as f:
            p = subprocess.Popen([str(LAUNCH_SCRIPT), "full"] + (extra_args.split() if extra_args else []),
                                 stdout=f, stderr=subprocess.STDOUT, start_new_session=True)
        GAME_PID_FILE.write_text(str(p.pid))
        return json.dumps({"pid": p.pid, "log": str(GAME_LOG), "note": "full mode runs server+game; blocks until game exits"})
    # wine mode: use 'run'
    with open(GAME_LOG, "ab") as f:
        p = subprocess.Popen([str(LAUNCH_SCRIPT), "run"] + (extra_args.split() if extra_args else []),
                             stdout=f, stderr=subprocess.STDOUT, start_new_session=True)
    GAME_PID_FILE.write_text(str(p.pid))
    time.sleep(2.0)
    return json.dumps({"pid": p.pid, "alive": p.poll() is None, "log": str(GAME_LOG)})


@mcp.tool()
def game_kill() -> str:
    """Kill the running game process(es)."""
    if GAME_PID_FILE.exists():
        try:
            pid = int(GAME_PID_FILE.read_text().strip())
            os.killpg(os.getpgid(pid), signal.SIGTERM)
        except Exception:
            pass
        GAME_PID_FILE.unlink(missing_ok=True)
    _run("pkill -f FarCry5.exe 2>/dev/null || true", timeout=10)
    _run("pkill -f 'fc5mp-launch.sh' 2>/dev/null || true", timeout=10)
    return "killed"


@mcp.tool()
def game_wait_exit(timeout_sec: int = 300) -> str:
    """Wait for game process to exit (or timeout)."""
    deadline = time.time() + timeout_sec
    while time.time() < deadline:
        if not _pids_matching("FarCry5.exe"):
            return "exited"
        time.sleep(2)
    return "timeout"


@mcp.tool()
def screenshot(name: str = "screen") -> str:
    """Take a screenshot of the game monitor; returns file path."""
    rc, out, err = _run([str(INTERACT), "screenshot", name], timeout=30)
    path = out.strip().splitlines()[-1] if out.strip() else ""
    return path or f"rc={rc} {err}"


@mcp.tool()
def click(x: int, y: int) -> str:
    """Click at monitor-relative coordinates."""
    rc, out, err = _run([str(INTERACT), "click", str(x), str(y)], timeout=10)
    return (out or err).strip()


@mcp.tool()
def double_click(x: int, y: int) -> str:
    """Double-click at monitor-relative coordinates."""
    rc, out, err = _run([str(INTERACT), "doubleclick", str(x), str(y)], timeout=10)
    return (out or err).strip()


@mcp.tool()
def move_mouse(x: int, y: int) -> str:
    """Move mouse to monitor-relative coordinates."""
    rc, out, err = _run([str(INTERACT), "move", str(x), str(y)], timeout=10)
    return (out or err).strip()


@mcp.tool()
def press_key(key: str) -> str:
    """Press a named key or raw ydotool keycode sequence.

    Named: enter, escape, space, tab, backspace. Otherwise pass through to
    'game-interact key ...' (e.g. '28:1 28:0').
    """
    named = {
        "enter": ["enter"],
        "escape": ["escape"],
        "esc": ["escape"],
        "space": ["space"],
        "tab": None,
    }
    if key.lower() in ("enter",):
        rc, out, err = _run([str(INTERACT), "enter"], timeout=10)
        return (out or err).strip()
    if key.lower() in ("escape", "esc"):
        rc, out, err = _run([str(INTERACT), "escape"], timeout=10)
        return (out or err).strip()
    if key.lower() == "space":
        rc, out, err = _run([str(INTERACT), "space"], timeout=10)
        return (out or err).strip()
    if key.lower() == "tab":
        rc, out, err = _run([str(INTERACT), "key", "15:1", "15:0"], timeout=10)
        return (out or err).strip()
    # raw passthrough
    parts = key.split()
    rc, out, err = _run([str(INTERACT), "key"] + parts, timeout=10)
    return (out or err).strip()


@mcp.tool()
def type_text(text: str) -> str:
    """Type a text string into the game."""
    rc, out, err = _run([str(INTERACT), "type", text], timeout=10)
    return (out or err).strip()


@mcp.tool()
def wait_stable(interval_sec: float = 2.0, timeout_sec: int = 60) -> str:
    """Wait until the screen stops changing (or timeout)."""
    try:
        r = subprocess.run([str(INTERACT), "wait-stable", str(interval_sec)],
                           capture_output=True, text=True, timeout=timeout_sec)
        return (r.stdout or r.stderr).strip() or f"rc={r.returncode}"
    except subprocess.TimeoutExpired:
        return "timeout (screen not stable)"


@mcp.tool()
def collect_logs() -> str:
    """Collect game/server logs into a timestamped dir; returns path."""
    rc, out, err = _run([str(LAUNCH_SCRIPT), "logs"], timeout=120)
    # parse path from output
    for line in (out + err).splitlines():
        if "Logs collected" in line:
            return line
    return (out or err).strip()


@mcp.tool()
def list_game_logs() -> str:
    """List available log files in game dir and /tmp."""
    files = []
    if GAME_BIN.exists():
        for p in GAME_BIN.glob("*.log"):
            files.append(str(p))
        for p in GAME_BIN.glob("fc5mp-*.txt"):
            files.append(str(p))
    for p in Path("/tmp").glob("fc5mp-*.log"):
        files.append(str(p))
    return "\n".join(sorted(files))


@mcp.tool()
def read_log(name: str, tail_lines: int = 200) -> str:
    """Read a log file by basename (searches game dir then /tmp)."""
    candidates = [GAME_BIN / name, Path("/tmp") / name, Path(name)]
    target = None
    for c in candidates:
        if c.exists():
            target = c
            break
    if target is None:
        return f"not found: {name}"
    lines = target.read_text(errors="replace").splitlines()
    return "\n".join(lines[-tail_lines:])


@mcp.tool()
def process_list(filter: str = "FarCry|fc5mp|wine") -> str:
    """List processes matching filter."""
    rc, out, err = _run(f"pgrep -af '{filter}' || true", timeout=10)
    return out.strip() or "(none)"


@mcp.tool()
def get_launch_env() -> str:
    """Read ~/.fc5mp/launch.env."""
    if not LAUNCH_ENV.exists():
        return "(missing)"
    return LAUNCH_ENV.read_text()


@mcp.tool()
def set_launch_env(key: str, value: str) -> str:
    """Set a KEY=value line in launch.env (creates/updates)."""
    LAUNCH_ENV.parent.mkdir(parents=True, exist_ok=True)
    lines = LAUNCH_ENV.read_text().splitlines() if LAUNCH_ENV.exists() else []
    out = []
    found = False
    for line in lines:
        if line.startswith(key + "="):
            out.append(f"{key}={value}")
            found = True
        else:
            out.append(line)
    if not found:
        out.append(f"{key}={value}")
    LAUNCH_ENV.write_text("\n".join(out) + "\n")
    return f"set {key}={value}"


@mcp.tool()
def read_file(path: str, tail_lines: int = 0) -> str:
    """Read a file (optionally tail N lines)."""
    p = Path(path)
    if not p.exists():
        return f"not found: {path}"
    data = p.read_text(errors="replace")
    if tail_lines:
        lines = data.splitlines()
        data = "\n".join(lines[-tail_lines:])
    # cap size
    if len(data) > 20000:
        data = data[-20000:]
    return data



@mcp.tool()
def observe(tail_lines: int = 40, ocr: bool = False) -> str:
    """One-shot observation: screenshot + process state + server/game logs (+ optional OCR).

    Designed for agents: returns everything needed to decide the next action in a
    single round-trip.
    """
    shot = screenshot("observe")
    info = json.loads(status())
    logs = {}
    try:
        logs["server"] = server_logs(tail_lines)
    except Exception as e:
        logs["server"] = str(e)
    try:
        logs["game_diag"] = read_log("fc5mp-hooks-diag.txt", tail_lines)
    except Exception as e:
        logs["game_diag"] = str(e)
    result = {"screenshot": shot, "status": info, "logs": logs}
    if ocr:
        result["ocr"] = ocr_screenshot(shot) if isinstance(shot, str) else ""
    return json.dumps(result)


@mcp.tool()
def ocr_screenshot(path: str = "") -> str:
    """Run tesseract OCR on a screenshot (or take a new one) and return text.

    Useful for reading menus/dialogs without vision. Pass an existing screenshot
    path, or leave empty to capture a fresh one.
    """
    if not path:
        path = screenshot("ocr")
    if not path or not Path(path).exists():
        return f"no image: {path}"
    rc, out, err = _run(["tesseract", path, "stdout", "--psm", "6"], timeout=60)
    text = out.strip()
    if not text:
        text = f"(no text) rc={rc} {err.strip()}"
    return text


@mcp.tool()
def ensure_server(timeout_sec: int = 30) -> str:
    """Start the server if not already listening on :21000; return status."""
    rc, out, _ = _run("ss -ltn 2>/dev/null | grep ':21000 '", timeout=5)
    if out.strip():
        return json.dumps({"already_running": True, "port": 21000})
    server_start()
    deadline = time.time() + timeout_sec
    while time.time() < deadline:
        rc, out, _ = _run("ss -ltn 2>/dev/null | grep ':21000 '", timeout=5)
        if out.strip():
            return json.dumps({"started": True, "port": 21000})
        time.sleep(1)
    return json.dumps({"started": False, "error": "timeout waiting for :21000"})


@mcp.tool()
def grep_log(name: str, pattern: str, tail_lines: int = 200) -> str:
    """Search a log file for lines matching pattern (case-insensitive)."""
    candidates = [GAME_BIN / name, Path("/tmp") / name, Path(name)]
    target = next((c for c in candidates if c.exists()), None)
    if target is None:
        return f"not found: {name}"
    lines = target.read_text(errors="replace").splitlines()
    import re as _re
    rx = _re.compile(pattern, _re.IGNORECASE)
    matches = [l for l in lines[-tail_lines:] if rx.search(l)]
    return "\n".join(matches) if matches else "(no matches)"


@mcp.tool()
def run_smoke_test(keep_running: bool = False, settle_sec: int = 25) -> str:
    """Automated smoke test: ensure server -> launch game -> wait -> observe -> collect logs -> (optionally kill).

    Returns a JSON summary with screenshot path, key log excerpts, and whether the
    game process survived. Default kills the game at the end for a clean run.
    """
    summary = {}
    summary["server"] = json.loads(ensure_server())
    summary["launch"] = json.loads(game_launch("wine"))
    time.sleep(settle_sec)
    shot = screenshot("smoke")
    summary["screenshot"] = shot
    summary["ocr"] = ocr_screenshot(shot) if isinstance(shot, str) else ""
    summary["status"] = json.loads(status())
    summary["log_excerpts"] = {
        "server": server_logs(30),
        "hooks": read_log("fc5mp-hooks-diag.txt", 40) if (GAME_BIN / "fc5mp-hooks-diag.txt").exists() else "",
        "tls": read_log("fc5mp-tls-trace.txt", 40) if (GAME_BIN / "fc5mp-tls-trace.txt").exists() else "",
    }
    if not keep_running:
        game_kill()
        summary["collected"] = collect_logs()
    return json.dumps(summary)




# ---------- State classification ----------

_STATES_FILE = ROOT / "mcp" / "states.json"


def _load_states():
    try:
        return json.loads(_STATES_FILE.read_text())
    except Exception:
        return {"states": {}, "error_patterns": []}


def _ocr_text(path=""):
    if not path:
        path = screenshot("classify")
    if not isinstance(path, str) or not Path(path).exists():
        return "", path
    rc, out, err = _run(["tesseract", path, "stdout", "--psm", "6"], timeout=60)
    return out.strip(), path


@mcp.tool()
def classify_screen(path: str = "") -> str:
    """Classify the current (or given) screenshot into a game state.

    Returns JSON: {state, scores, matched_keywords, error_matches, text, screenshot}.
    States and keywords are defined in mcp/states.json (editable).
    """
    cfg = _load_states()
    text, shot = _ocr_text(path)
    low = text.lower()
    scores = {}
    matched = {}
    for state, kws in cfg.get("states", {}).items():
        hits = [kw for kw in kws if kw.lower() in low]
        scores[state] = len(hits)
        matched[state] = hits
    # pick best
    best = max(scores, key=lambda k: scores[k]) if scores else "unknown"
    if scores.get(best, 0) == 0:
        best = "unknown"
    import re as _re
    err_matches = []
    for pat in cfg.get("error_patterns", []):
        err_matches += _re.findall(pat, low)
    return json.dumps({
        "state": best,
        "scores": scores,
        "matched_keywords": matched.get(best, []),
        "error_matches": err_matches,
        "screenshot": shot,
        "text_excerpt": text[:500],
    })


@mcp.tool()
def wait_for_state(expected: str, timeout_sec: int = 60, interval_sec: float = 2.0) -> str:
    """Poll classification until the screen reaches `expected` state (or timeout)."""
    deadline = time.time() + timeout_sec
    last = {}
    while time.time() < deadline:
        last = json.loads(classify_screen())
        if last.get("state") == expected:
            last["reached"] = True
            return json.dumps(last)
        time.sleep(interval_sec)
    last["reached"] = False
    last["timeout"] = True
    return json.dumps(last)


@mcp.tool()
def wait_for_text(substring: str, timeout_sec: int = 60, interval_sec: float = 2.0, path: str = "") -> str:
    """Poll OCR until the screen contains `substring` (case-insensitive) or timeout."""
    deadline = time.time() + timeout_sec
    last_text = ""
    while time.time() < deadline:
        text, shot = _ocr_text(path)
        last_text = text
        if substring.lower() in text.lower():
            return json.dumps({"found": True, "substring": substring, "screenshot": shot})
        time.sleep(interval_sec)
    return json.dumps({"found": False, "substring": substring, "last_text_excerpt": last_text[:500]})


@mcp.tool()
def wait_for_process(name: str, running: bool = True, timeout_sec: int = 60, interval_sec: float = 2.0) -> str:
    """Wait until a process matching `name` is running (or stopped)."""
    deadline = time.time() + timeout_sec
    while time.time() < deadline:
        lines = _pids_matching(name)
        is_running = bool(lines)
        if is_running == running:
            return json.dumps({"name": name, "running": is_running, "matched": lines})
        time.sleep(interval_sec)
    lines = _pids_matching(name)
    return json.dumps({"name": name, "running": bool(lines), "timeout": True, "matched": lines})


# ---------- Inline image viewing ----------

@mcp.tool()
def view_screenshot(path: str) -> MCPImage:
    """Return a screenshot as inline image content (use sparingly; large)."""
    from mcp.server.fastmcp import Image as _Img
    if not path or not Path(path).exists():
        # take a fresh one
        path = screenshot("view")
    return _Img(path=path)


# ---------- Scenario runner ----------

_SCENARIO_DIR = ROOT / "mcp" / "scenarios"


def _scenario_path(name):
    if not name.endswith((".yaml", ".yml")):
        name = name + ".yaml"
    return _SCENARIO_DIR / name


@mcp.tool()
def list_scenarios() -> str:
    """List available scenario YAML files."""
    if not _SCENARIO_DIR.exists():
        return "[]"
    return json.dumps(sorted(p.stem for p in _SCENARIO_DIR.glob("*.yaml")) + sorted(p.stem for p in _SCENARIO_DIR.glob("*.yml")))


@mcp.tool()
def get_scenario(name: str) -> str:
    """Read a scenario YAML file."""
    p = _scenario_path(name)
    if not p.exists():
        return f"not found: {p}"
    return p.read_text()


@mcp.tool()
def save_scenario(name: str, yaml_content: str) -> str:
    """Save a scenario YAML file."""
    _SCENARIO_DIR.mkdir(parents=True, exist_ok=True)
    p = _scenario_path(name)
    p.write_text(yaml_content)
    return f"saved: {p}"


def _run_step(step):
    action = step.get("action")
    res = {"action": action, "ok": True}
    try:
        if action == "ensure_server":
            res["result"] = json.loads(ensure_server(step.get("timeout_sec", 30)))
        elif action == "launch":
            res["result"] = json.loads(game_launch(step.get("mode", "wine"), step.get("extra_args", "")))
        elif action == "kill_game":
            res["result"] = game_kill()
        elif action == "wait":
            time.sleep(step.get("seconds", 1))
            res["result"] = f"slept {step.get('seconds',1)}s"
        elif action == "wait_for_state":
            out = wait_for_state(step["state"], step.get("timeout_sec", 60), step.get("interval_sec", 2.0))
            r = json.loads(out)
            res["result"] = r
            if not r.get("reached", False):
                res["ok"] = False
        elif action == "wait_for_text":
            out = wait_for_text(step["contains"], step.get("timeout_sec", 60), step.get("interval_sec", 2.0))
            r = json.loads(out)
            res["result"] = r
            if not r.get("found", False):
                res["ok"] = False
        elif action == "wait_for_process":
            out = wait_for_process(step["name"], step.get("running", True), step.get("timeout_sec", 60), step.get("interval_sec", 2.0))
            res["result"] = json.loads(out)
        elif action == "screenshot":
            shot = screenshot(step.get("name", "step"))
            res["result"] = shot
        elif action == "click":
            res["result"] = click(step["x"], step["y"])
        elif action == "double_click":
            res["result"] = double_click(step["x"], step["y"])
        elif action == "move_mouse":
            res["result"] = move_mouse(step["x"], step["y"])
        elif action == "key":
            res["result"] = press_key(step.get("key", "enter"))
        elif action == "type":
            res["result"] = type_text(step.get("text", ""))
        elif action == "ocr":
            path = step.get("path", "")
            res["result"] = ocr_screenshot(path)
        elif action == "classify":
            res["result"] = json.loads(classify_screen(step.get("path", "")))
        elif action == "assert_state":
            cls = json.loads(classify_screen(step.get("path", "")))
            res["result"] = cls
            if cls.get("state") != step.get("state"):
                res["ok"] = False
                res["error"] = f"expected state {step.get('state')}, got {cls.get('state')}"
        elif action == "assert_text":
            text, _ = _ocr_text(step.get("path", ""))
            contains = step.get("contains", "")
            present = contains.lower() in text.lower()
            res["result"] = {"contains": contains, "present": present}
            # fail_if: if true, presence is a failure (for error detection)
            if step.get("fail_if", False):
                if present:
                    res["ok"] = False
                    res["error"] = f"unexpected text found: {contains}"
            else:
                if not present:
                    res["ok"] = False
                    res["error"] = f"expected text not found: {contains}"
        elif action == "assert_no_text":
            text, _ = _ocr_text(step.get("path", ""))
            contains = step.get("contains", "")
            if contains.lower() in text.lower():
                res["ok"] = False
                res["error"] = f"unexpected text found: {contains}"
            res["result"] = {"contains": contains, "present": contains.lower() in text.lower()}
        elif action == "assert_log_contains":
            name = step.get("file", "fc5mp-hooks-diag.txt")
            pat = step.get("pattern", "")
            content = read_log(name, step.get("tail_lines", 500))
            present = pat.lower() in content.lower()
            res["result"] = {"file": name, "pattern": pat, "present": present}
            if not present:
                res["ok"] = False
                res["error"] = f"expected log text not found: {pat}"
        elif action == "assert_log_not_contains":
            name = step.get("file", "fc5mp-hooks-diag.txt")
            pat = step.get("pattern", "")
            content = read_log(name, step.get("tail_lines", 500))
            present = pat.lower() in content.lower()
            res["result"] = {"file": name, "pattern": pat, "present": present}
            if present:
                res["ok"] = False
                res["error"] = f"unexpected log text found: {pat}"
        elif action == "collect_logs":
            res["result"] = collect_logs()
        elif action == "server_stop":
            res["result"] = server_stop()
        else:
            res["ok"] = False
            res["error"] = f"unknown action: {action}"
    except Exception as e:
        res["ok"] = False
        res["error"] = f"{type(e).__name__}: {e}"
    return res


@mcp.tool()
def run_scenario(name: str, stop_on_failure: bool = True) -> str:
    """Execute a scenario YAML file step by step. Returns JSON with per-step results."""
    p = _scenario_path(name)
    if not p.exists():
        return json.dumps({"error": f"scenario not found: {p}"})
    import yaml
    data = yaml.safe_load(p.read_text())
    steps = data.get("steps", [])
    results = []
    overall_ok = True
    for i, step in enumerate(steps):
        r = _run_step(step)
        r["index"] = i
        results.append(r)
        if not r.get("ok", True):
            overall_ok = False
            if stop_on_failure:
                break
    return json.dumps({
        "scenario": data.get("name", name),
        "description": data.get("description", ""),
        "overall_ok": overall_ok,
        "steps_run": len(results),
        "steps_total": len(steps),
        "results": results,
    }, indent=2)


# ---------- Scenario templates ----------

@mcp.tool()
def create_example_scenario() -> str:
    """Write a starter scenario file for the login smoke test."""
    content = """name: login_smoke
description: Launch game, wait for main menu, screenshot, collect logs.
steps:
  - action: ensure_server
    timeout_sec: 30
  - action: launch
    mode: wine
  - action: wait
    seconds: 20
  - action: wait_for_state
    state: main_menu
    timeout_sec: 90
  - action: screenshot
    name: main_menu
  - action: classify
  - action: collect_logs
  - action: kill_game
"""
    return save_scenario("login_smoke", content)



# ---------- Capture analysis ----------

@mcp.tool()
def capture_summary(path: str) -> str:
    """Run analysis/capdump.py on a capture file and return its summary text."""
    if not Path(path).exists():
        return f"not found: {path}"
    rc, out, err = _run(["uv", "run", "python", "capdump.py", path], timeout=120, cwd=str(ROOT / "analysis"))
    if rc != 0:
        # fallback to system python
        rc, out, err = _run(["python3", "capdump.py", path], timeout=120, cwd=str(ROOT / "analysis"))
    return (out or err).strip()


@mcp.tool()
def capture_dump(path: str, out_dir: str = "") -> str:
    """Dump TLS plaintext streams from a capture into a directory."""
    if not Path(path).exists():
        return f"not found: {path}"
    if not out_dir:
        out_dir = str(Path("/tmp") / ("capdump-" + Path(path).stem))
    Path(out_dir).mkdir(parents=True, exist_ok=True)
    rc, out, err = _run(["uv", "run", "python", "capdump.py", path, "--dump", out_dir], timeout=300, cwd=str(ROOT / "analysis"))
    if rc != 0:
        rc, out, err = _run(["python3", "capdump.py", path, "--dump", out_dir], timeout=300, cwd=str(ROOT / "analysis"))
    return f"rc={rc}\n{(out or err).strip()}\nout_dir={out_dir}"


# ---------- Diagnosis / playbook ----------

@mcp.tool()
def diagnose() -> str:
    """Inspect current screen/logs and return matching playbook guidance.

    Combines classify_screen + observe + playbook.json rules to suggest next
    steps and files, minimizing human input.
    """
    playbook = json.loads((ROOT / "mcp" / "playbook.json").read_text())
    cls = json.loads(classify_screen())
    text = cls.get("text_excerpt", "")
    logs = ""
    try:
        logs += server_logs(60) + "\n"
    except Exception:
        pass
    try:
        logs += read_log("fc5mp-hooks-diag.txt", 60) + "\n"
    except Exception:
        pass
    haystack = (text + "\n" + logs).lower()
    matches = []
    for rule in playbook.get("rules", []):
        for m in rule.get("match", []):
            if m.lower() in haystack:
                matches.append(rule)
                break
    if not matches:
        matches = [playbook.get("default", {})]
    # de-dup by diagnosis
    seen = set()
    unique = []
    for m in matches:
        key = m.get("diagnosis", "")
        if key in seen:
            continue
        seen.add(key)
        unique.append(m)
    return json.dumps({
        "classification": cls,
        "matched_rules": unique,
        "suggested_files": sorted({f for r in unique for f in r.get("suggested_files", [])}),
        "next_steps": [s for r in unique for s in r.get("next_steps", [])],
    }, indent=2)


@mcp.tool()
def rebuild_and_smoke(scenario: str = "maine118_repro", keep_game_running: bool = False) -> str:
    """Build server, run a scenario, and return a consolidated report.

    Intended as the single call an agent uses to test a code change end-to-end.
    """
    build = server_build()
    if "error" in build.lower() or "failed" in build.lower():
        # still attempt; build output may contain warnings
        pass
    test = server_test()
    scen = run_scenario(scenario)
    report = {
        "build": build.splitlines()[-15:],
        "tests": test.splitlines()[-15:],
        "scenario": json.loads(scen),
    }
    if not keep_game_running:
        game_kill()
    return json.dumps(report, indent=2)



if __name__ == "__main__":
    mcp.run(transport="stdio")
