#!/usr/bin/env python3
"""Claude Code -> TamaPoke bridge: shows your Claude usage on the board.

Claude Code hands every status line command a JSON snapshot of the session
(context window, cost, session time, the 5-hour and weekly plan limits) and
runs hooks when Claude starts working, needs your permission or finishes. This
script records both and streams them to the board over USB serial, where the
"Claude dashboard" screens show them. Nothing leaves your computer and no API
key or token is needed: the data is what Claude Code already computed.

As a Claude Code plugin (plugins/tamapoke-monitor): the plugin brings the hooks
and starts `serve` with each session; `/tamapoke-monitor:setup` runs
`install --plugin` once per computer to add the status line, which a plugin
cannot set by itself.

By hand instead (adds a status line and a few hooks to ~/.claude/settings.json,
after backing it up; your own status line keeps working, it is chained):

    python3 claude_monitor.py install

Then, with the board plugged in over USB, leave this running:

    python3 claude_monitor.py serve        (needs: pip install pyserial)

Other commands:
    status      print what would be sent right now (no board needed)
    demo        send made-up data so you can see the screens without Claude Code
    uninstall   take the status line and hooks out of settings.json again
    stop        stop a running `serve` (to free the port for flashing / send_sd.py)
    autostart   on|off: whether the plugin starts `serve` with each session
    statusline  (run by Claude Code) record a snapshot, print a short status line
    hook        (run by Claude Code) record a hook event
    ensure      (run by the plugin on SessionStart) refresh the stable copy of
                this script and start `serve` in the background if it isn't running

Files live in ~/.claude/tamapoke/ (override with TAMAPOKE_CLAUDE_DIR).
Protocol: claudemon.h in the firmware.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time
import unicodedata
from pathlib import Path
from urllib.parse import quote

PROTO = 1
SEND_EVERY_S = 2.0       # the board drops the link after 15 s without a line
REFRESH_S = 15           # statusLine.refreshInterval set by `install`
STALE_S = 600            # a session not heard from for this long is gone (SessionEnd missed)
WORKING_STALE_S = 300    # "working" with nothing moving for this long: Esc'd turn, treat as idle
LIMITS_KEEP_S = 6 * 3600  # plan limits stay on screen this long after the last session closed
MARKER = "claude_monitor.py"  # how install/uninstall recognise their own entries

# board status codes (ClaudeStatus in claudemon.h)
ST_NONE, ST_WORKING, ST_DONE, ST_NEEDS_YOU, ST_ERROR, ST_IDLE = range(6)
STATUS_CODE = {"working": ST_WORKING, "done": ST_DONE, "needs_you": ST_NEEDS_YOU, "error": ST_ERROR, "idle": ST_IDLE}
STATUS_RANK = {"needs_you": 5, "error": 4, "working": 3, "done": 2, "idle": 1}  # which session the board shows

HOOK_EVENTS = ["SessionStart", "UserPromptSubmit", "PermissionRequest", "PostToolUse", "Notification",
               "Stop", "StopFailure", "SessionEnd"]
NEEDS_YOU_NOTIFICATIONS = {"permission_prompt", "elicitation_dialog", "elicitation_url_dialog", "agent_needs_input"}


# --- small file helpers ------------------------------------------------------

def state_dir():
    env = os.environ.get("TAMAPOKE_CLAUDE_DIR")
    return Path(env) if env else Path.home() / ".claude" / "tamapoke"


def sessions_dir():
    return state_dir() / "sessions"


def safe_name(sid):
    s = re.sub(r"[^A-Za-z0-9_-]", "_", str(sid or "unknown"))
    return s[:80] or "unknown"


def read_json(path):
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def write_json(path, obj):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(f"{path.name}.{os.getpid()}.tmp")
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(obj, f, indent=1)
    for _ in range(3):  # Windows refuses to replace a file another process is reading
        try:
            os.replace(tmp, path)
            return
        except PermissionError:
            time.sleep(0.05)
    try:
        os.remove(tmp)
    except OSError:
        pass


def load_config():
    return read_json(state_dir() / "config.json") or {}


# --- plugin support: stable copy, remembered Python, single `serve` -----------

def stable_script():
    """Where the status line runs us from. The plugin's own folder changes with
    every version (and old ones are deleted), so `ensure` keeps a copy here."""
    return state_dir() / "bin" / "claude_monitor.py"


def refresh_stable_copy():
    me = Path(__file__).resolve()
    dst = stable_script()
    try:
        if dst.resolve() == me:
            return dst
        body = me.read_bytes()
        if dst.exists() and dst.read_bytes() == body:
            return dst
        dst.parent.mkdir(parents=True, exist_ok=True)
        tmp = dst.with_name(f"{dst.name}.{os.getpid()}.tmp")
        tmp.write_bytes(body)
        os.replace(tmp, dst)
    except OSError:
        pass
    return dst


def remember_python():
    """run.sh reads this so hooks don't have to look for a Python each time."""
    try:
        p = state_dir() / "python"
        exe = Path(sys.executable).as_posix()
        if not p.exists() or p.read_text(encoding="utf-8").strip() != exe:
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(exe + "\n", encoding="utf-8")
    except OSError:
        pass


def lock_path():
    return state_dir() / "serve.lock"


def pid_path():
    return state_dir() / "serve.pid"


def try_lock():
    """An open file holding the `serve` lock, or None if another serve has it.
    The OS drops the lock when the process ends, however it ends."""
    path = lock_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    f = open(path, "a+b")
    try:
        if os.name == "nt":
            import msvcrt
            f.seek(0)
            msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(f.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        return f
    except OSError:
        f.close()
        return None


def release_lock(f):
    try:
        if os.name == "nt":
            import msvcrt
            f.seek(0)
            msvcrt.locking(f.fileno(), msvcrt.LK_UNLCK, 1)
    except OSError:
        pass
    f.close()


def serve_running():
    f = try_lock()
    if f is None:
        return True
    release_lock(f)
    return False


def num(v):
    """int/float from JSON, or None (bools and junk are not numbers)."""
    if isinstance(v, bool) or not isinstance(v, (int, float)):
        return None
    return v


# --- what Claude Code reports -----------------------------------------------

def snapshot_from_statusline(data):
    """The few status line fields the board shows (claudemon.h), sanitised."""
    data = data if isinstance(data, dict) else {}
    g = lambda d, k: d.get(k) if isinstance(d, dict) else None  # noqa: E731
    cw = g(data, "context_window") or {}
    snap = {}

    size = num(g(cw, "context_window_size"))
    pct = num(g(cw, "used_percentage"))
    used = num(g(cw, "total_input_tokens"))
    cur = g(cw, "current_usage")
    if (used is None or used == 0) and isinstance(cur, dict):
        parts = [num(cur.get(k)) for k in ("input_tokens", "cache_creation_input_tokens", "cache_read_input_tokens")]
        if any(p is not None for p in parts):
            used = sum(p or 0 for p in parts)
    if used == 0 and cur is None:
        used = None  # before the first reply, and right after /compact
    if used is None and pct is not None and size:
        used = pct * size / 100
    if size:
        snap["max"] = int(size)
    if used is not None:
        snap["ctx"] = int(used)
    if pct is not None:
        snap["pct"] = max(0, min(100, int(round(pct))))

    cost = g(data, "cost") or {}
    for key, src, scale in (("cost", "total_cost_usd", 100), ("dur", "total_duration_ms", 0.001),
                            ("api", "total_api_duration_ms", 0.001), ("la", "total_lines_added", 1),
                            ("lr", "total_lines_removed", 1)):
        v = num(g(cost, src))
        if v is not None:
            snap[key] = int(round(v * scale))

    rl = g(data, "rate_limits") or {}
    for key, win in (("h5", "five_hour"), ("w7", "seven_day"), ("sp", "spend_limit")):
        w = g(rl, win) or {}
        p, r = num(g(w, "used_percentage")), num(g(w, "resets_at"))
        if p is not None:
            snap[key] = max(0, int(round(p)))
            if r is not None:
                snap[key + "_at"] = int(r)
    sp = g(rl, "spend_limit") or {}
    for key, src in (("spu", "used_usd"), ("spl", "limit_usd")):
        v = num(g(sp, src))
        if v is not None:
            snap[key] = int(round(v * 100))

    model = g(data, "model") or {}
    snap["model"] = str(g(model, "display_name") or g(model, "id") or "")
    ws = g(data, "workspace") or {}
    repo = g(ws, "repo") or {}
    folder = str(g(ws, "project_dir") or g(data, "cwd") or "").rstrip("/\\")
    proj = g(repo, "name") or re.split(r"[\\/]", folder)[-1]  # either separator: the PC may be Windows
    snap["proj"] = str(proj or "")
    snap["name"] = str(g(data, "session_name") or "")
    snap["eff"] = str(g(g(data, "effort") or {}, "level") or "")
    return snap


def status_for_hook(data):
    """Board status a hook event puts the session in, or None to leave it as is."""
    ev = data.get("hook_event_name")
    if ev in ("UserPromptSubmit", "PostToolUse", "PostToolUseFailure"):
        return "working"
    if ev == "PermissionRequest":
        return "needs_you"
    if ev == "Notification":
        return "needs_you" if data.get("notification_type") in NEEDS_YOU_NOTIFICATIONS else None
    if ev == "Stop":
        return "done"
    if ev == "StopFailure":
        return "error"
    if ev == "SessionStart":
        return "idle"
    return None


# --- the snapshot for the board ---------------------------------------------

def load_sessions():
    sessions = {}
    d = sessions_dir()
    if not d.is_dir():
        return sessions
    for p in d.glob("*.json"):
        m = re.match(r"(.+)\.(status|hook)\.json$", p.name)
        obj = read_json(p)
        if not m or not isinstance(obj, dict):
            continue
        sessions.setdefault(m.group(1), {})[m.group(2)] = obj
    return sessions


def session_status(sess, now):
    st, hook = sess.get("status") or {}, sess.get("hook")
    changed_at = st.get("changed_at", 0)
    if hook:
        s = hook.get("status", "idle")
        if s == "working" and now - max(hook.get("at", 0), changed_at) > WORKING_STALE_S:
            s = "idle"  # Stop doesn't fire when you press Esc
        return s
    # hooks not installed: Claude is working while the numbers move
    return "working" if now - changed_at < 20 else "idle"


def session_seen(sess):
    return max((sess.get(k) or {}).get("at", 0) for k in ("status", "hook"))


def build_state(now=None, sessions=None):
    """Picks the session to show and merges the account-wide plan limits."""
    now = time.time() if now is None else now
    sessions = load_sessions() if sessions is None else sessions
    live = {sid: s for sid, s in sessions.items() if now - session_seen(s) < STALE_S}
    state = {"st": ST_NONE, "n": len(live)}

    if live:
        def rank(item):
            s = session_status(item[1], now)
            return (STATUS_RANK.get(s, 0), session_seen(item[1]))
        sid, sess = max(live.items(), key=rank)
        snap = dict((sess.get("status") or {}).get("snap") or {})
        state.update({k: v for k, v in snap.items()
                      if not k.endswith("_at") and k not in ("h5", "w7", "sp", "spu", "spl")})
        state["st"] = STATUS_CODE.get(session_status(sess, now), ST_IDLE)

    # plan limits are per account: the freshest snapshot that has them, from any session
    best = None
    for s in sessions.values():
        st = s.get("status") or {}
        if any(k in (st.get("snap") or {}) for k in ("h5", "w7", "sp")):
            if best is None or st.get("at", 0) > best.get("at", 0):
                best = st
    if best and (live or now - best.get("at", 0) < LIMITS_KEEP_S):
        snap = best.get("snap") or {}
        for key, rkey in (("h5", "r5"), ("w7", "r7"), ("sp", "rs")):
            if key not in snap:
                continue
            at = snap.get(key + "_at")
            if at is not None and at <= now:
                continue  # that window has reset: the old figure is wrong now
            state[key] = snap[key]
            if at is not None:
                state[rkey] = int(at - now)
        for key in ("spu", "spl"):
            if key in snap and "sp" in state:
                state[key] = snap[key]
    return state


def ascii_text(s, limit):
    s = unicodedata.normalize("NFKD", str(s)).encode("ascii", "ignore").decode("ascii")
    s = " ".join(s.split())[:limit]
    return quote(s, safe="-_.~()[]/:+,#@!&*'")


TEXT_KEYS = {"model": 23, "proj": 27, "name": 31, "eff": 7}
INT_KEYS = ["st", "ctx", "max", "pct", "cost", "dur", "api", "la", "lr", "h5", "r5", "w7", "r7", "sp", "rs",
            "spu", "spl", "n"]


def encode_line(state):
    parts = ["CLAUDE", f"v={PROTO}"]
    for k in INT_KEYS:
        v = state.get(k)
        if num(v) is not None:
            parts.append(f"{k}={int(v)}")
    for k, limit in TEXT_KEYS.items():
        v = state.get(k)
        if v:
            enc = ascii_text(v, limit)
            if enc:
                parts.append(f"{k}={enc}")
    return " ".join(parts) + "\n"


def tokens(t):
    if t is None:
        return "?"
    return f"{t / 1000:.0f}k" if t >= 10000 else f"{t / 1000:.1f}k" if t >= 1000 else str(t)


def describe(state):
    names = {ST_NONE: "no session", ST_WORKING: "working", ST_DONE: "your turn", ST_NEEDS_YOU: "NEEDS YOU",
             ST_ERROR: "error", ST_IDLE: "idle"}
    bits = [names.get(state.get("st"), "?")]
    if state.get("model"):
        bits.append(state["model"])
    if "pct" in state or "ctx" in state:
        left = ""
        if "ctx" in state and state.get("max"):
            left = f" ({tokens(max(0, state['max'] - state['ctx']))} left)"
        bits.append(f"ctx {state.get('pct', '?')}%{left}")
    if "h5" in state:
        bits.append(f"5h {state['h5']}%")
    if "w7" in state:
        bits.append(f"week {state['w7']}%")
    if "cost" in state:
        bits.append(f"${state['cost'] / 100:.2f}")
    if state.get("n", 0) > 1:
        bits.append(f"{state['n']} sessions")
    return " | ".join(bits)


# --- commands run by Claude Code ---------------------------------------------

def statusline_text(snap):
    bits = []
    if snap.get("model"):
        bits.append(snap["model"])
    if "pct" in snap:
        left = ""
        if "ctx" in snap and snap.get("max"):
            left = f" ({tokens(max(0, snap['max'] - snap['ctx']))} left)"
        bits.append(f"ctx {snap['pct']}%{left}")
    if "h5" in snap:
        bits.append(f"5h {snap['h5']}%")
    if "cost" in snap:
        bits.append(f"${snap['cost'] / 100:.2f}")
    return " | ".join(bits) or "TamaPoke monitor"


def cmd_statusline(args):
    raw = sys.stdin.read()
    out = "TamaPoke monitor"
    try:
        data = json.loads(raw) if raw.strip() else {}
        snap = snapshot_from_statusline(data)
        sid = data.get("session_id") if isinstance(data, dict) else None
        path = sessions_dir() / f"{safe_name(sid)}.status.json"
        prev = read_json(path) or {}
        now = time.time()
        psnap = prev.get("snap") or {}
        moved = (psnap.get("ctx"), psnap.get("cost")) != (snap.get("ctx"), snap.get("cost"))
        changed_at = now if (prev and moved) else prev.get("changed_at", 0)
        write_json(path, {"session_id": sid, "at": now, "changed_at": changed_at, "snap": snap})
        out = statusline_text(snap)
    except Exception as e:  # never break Claude Code's status line
        out = f"TamaPoke monitor: {e.__class__.__name__}"
    chain = load_config().get("chain")
    if chain:  # the status line you had before `install` keeps showing
        try:
            r = subprocess.run(chain, shell=True, input=raw, capture_output=True, text=True, timeout=5)
            out = r.stdout.rstrip("\n") or out
        except (OSError, subprocess.SubprocessError):
            pass
    print(out)
    return 0


def cmd_hook(args):
    try:
        data = json.loads(sys.stdin.read() or "{}")
        if not isinstance(data, dict):
            return 0
        sid = safe_name(data.get("session_id"))
        if data.get("hook_event_name") == "SessionEnd":
            for kind in ("status", "hook"):
                try:
                    os.remove(sessions_dir() / f"{sid}.{kind}.json")
                except OSError:
                    pass
            return 0
        status = status_for_hook(data)
        if status:
            write_json(sessions_dir() / f"{sid}.hook.json",
                       {"status": status, "at": time.time(), "event": data.get("hook_event_name")})
    except Exception:
        pass  # a hook must never get in Claude's way
    return 0


# --- install / uninstall ------------------------------------------------------

def script_command(sub, script=None):
    """Shell command Claude Code runs for us (forward slashes work in bash and on Windows)."""
    py = Path(sys.executable).as_posix()
    me = Path(script or __file__).resolve().as_posix()
    return f'"{py}" "{me}" {sub}'


def is_ours(cmd):
    return isinstance(cmd, str) and MARKER in cmd


def settings_path(args):
    return Path(args.settings).expanduser() if getattr(args, "settings", None) else Path.home() / ".claude" / "settings.json"


def install_into(settings, status_cmd, hook_cmd, config):
    """Adds our status line and hooks to a settings dict (in place). Idempotent.
    With hook_cmd None (the plugin brings its own hooks) only the status line is
    set, and hooks an earlier by-hand install left are taken out so nothing runs twice."""
    old = settings.get("statusLine")
    if isinstance(old, dict) and old.get("command") and not is_ours(old.get("command")):
        config["chain"] = old["command"]
    sl = {"type": "command", "command": status_cmd, "refreshInterval": REFRESH_S}
    if isinstance(old, dict) and "padding" in old:
        sl["padding"] = old["padding"]
    settings["statusLine"] = sl

    if hook_cmd is None:
        remove_hooks(settings)
        return settings
    hooks = settings.setdefault("hooks", {})
    for ev in HOOK_EVENTS:
        groups = hooks.setdefault(ev, [])
        present = any(is_ours(h.get("command")) for g in groups if isinstance(g, dict)
                      for h in g.get("hooks", []) if isinstance(h, dict))
        if not present:
            groups.append({"hooks": [{"type": "command", "command": hook_cmd, "async": True}]})
    return settings


def uninstall_from(settings, config):
    sl = settings.get("statusLine")
    if isinstance(sl, dict) and is_ours(sl.get("command")):
        chain = config.pop("chain", None)
        if chain:
            settings["statusLine"] = {"type": "command", "command": chain}
        else:
            del settings["statusLine"]
    return remove_hooks(settings)


def remove_hooks(settings):
    """Takes our hook entries out of a settings dict (in place), leaving the rest."""
    hooks = settings.get("hooks")
    if isinstance(hooks, dict):
        for ev in list(hooks):
            groups = hooks[ev]
            if not isinstance(groups, list):
                continue
            kept = []
            for g in groups:
                if isinstance(g, dict) and isinstance(g.get("hooks"), list):
                    g["hooks"] = [h for h in g["hooks"] if not (isinstance(h, dict) and is_ours(h.get("command")))]
                    if not g["hooks"]:
                        continue
                kept.append(g)
            if kept:
                hooks[ev] = kept
            else:
                del hooks[ev]
        if not hooks:
            del settings["hooks"]
    return settings


def load_settings(path):
    if not path.exists():
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8") or "{}")
    except ValueError as e:
        raise SystemExit(f"{path} is not valid JSON ({e}); fix it first, nothing was changed")
    if not isinstance(data, dict):
        raise SystemExit(f"{path} does not hold a JSON object; nothing was changed")
    return data


def save_settings(path, settings):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        backup = path.with_name(f"{path.name}.bak-tamapoke-{time.strftime('%Y%m%d-%H%M%S')}")
        backup.write_bytes(path.read_bytes())
        print(f"backup: {backup}")
    path.write_text(json.dumps(settings, indent=2) + "\n", encoding="utf-8")


def cmd_install(args):
    path = settings_path(args)
    settings = load_settings(path)
    config = load_config()
    if args.plugin:
        # the plugin brings the hooks and starts `serve`; only the status line is ours to set
        script = refresh_stable_copy()
        remember_python()
        install_into(settings, script_command("statusline", script), None, config)
    else:
        install_into(settings, script_command("statusline"), script_command("hook"), config)
    save_settings(path, settings)
    write_json(state_dir() / "config.json", config)
    print(f"updated {path}")
    if config.get("chain"):
        print(f"your status line still shows: it is chained ({config['chain']})")
    try:
        import serial  # noqa: F401
    except ImportError:
        print(f"pyserial is missing; install it with:  \"{Path(sys.executable).as_posix()}\" -m pip install pyserial")
    if args.plugin:
        print("Status line set. The plugin starts the bridge with each new Claude Code session.")
    else:
        print("Restart Claude Code (or open a new session), then plug in the board and run:")
        print(f"  {script_command('serve')}")
    return 0


def cmd_uninstall(args):
    path = settings_path(args)
    settings = load_settings(path)
    config = load_config()
    uninstall_from(settings, config)
    save_settings(path, settings)
    write_json(state_dir() / "config.json", config)
    print(f"removed the TamaPoke status line and hooks from {path}")
    return 0


# --- talking to the board ------------------------------------------------------

ESPRESSIF_VID = 0x303A               # native USB of the ESP32-S3 (the Waveshare boards)
OTHER_VIDS = {0x1A86, 0x10C4}        # CH34x / CP210x USB-UART bridges (Guition and others)


def need_pyserial():
    try:
        import serial  # noqa: F401
        import serial.tools.list_ports  # noqa: F401
    except ImportError:
        raise SystemExit("this needs pyserial:  pip install pyserial")
    return sys.modules["serial"]


def open_port(serial, port):
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = 115200
    ser.timeout = 0
    ser.write_timeout = 2
    ser.dtr = False  # DTR/RTS low: opening the port must not reset the board
    ser.rts = False
    if os.name == "posix":
        ser.exclusive = True  # never interleave with tools/send_sd.py on the same port
    ser.open()
    return ser


def probe(ser, wait=1.5):
    """True if the firmware answers 'CLAUDE?' (i.e. it has the dashboard)."""
    try:
        ser.reset_input_buffer()
        ser.write(b"CLAUDE?\n")
        end, buf = time.time() + wait, b""
        while time.time() < end:
            buf += ser.read(512)
            if b"CLAUDE ok" in buf:
                return True
            time.sleep(0.05)
    except Exception:
        pass
    return False


def find_board(serial, wanted=None, verbose=True):
    if wanted:
        return open_port(serial, wanted)
    from serial.tools import list_ports
    ports = sorted(list_ports.comports(), key=lambda p: (p.vid != ESPRESSIF_VID, p.device))
    cands = [p for p in ports if p.vid == ESPRESSIF_VID or p.vid in OTHER_VIDS]
    fallback = None
    for p in cands:
        try:
            ser = open_port(serial, p.device)
        except Exception as e:
            if verbose:
                print(f"  {p.device}: {e}")
            continue
        if probe(ser):
            return ser
        if p.vid == ESPRESSIF_VID and fallback is None:
            fallback = ser  # an ESP32-S3 that didn't answer: older firmware, or still booting
            continue
        ser.close()
    return fallback


def cmd_serve(args):
    serial = need_pyserial()
    lock = try_lock()
    if lock is None:
        print("the bridge is already running (stop it with: claude_monitor.py stop)")
        return 0
    try:
        pid_path().write_text(str(os.getpid()), encoding="utf-8")
    except OSError:
        pass
    try:
        return serve_loop(serial, args)
    finally:
        try:
            os.remove(pid_path())
        except OSError:
            pass
        release_lock(lock)


def serve_loop(serial, args):
    last_desc, ser, warned = None, None, False
    print("TamaPoke Claude monitor: Ctrl+C to stop", flush=True)
    try:
        while True:
            if ser is None:
                try:
                    ser = find_board(serial, args.port, verbose=not warned)
                except Exception as e:
                    ser = None
                    if not warned:
                        print(f"cannot open {args.port}: {e}")
                if ser is None:
                    if not warned:
                        print("waiting for the board on USB (is it plugged in? is another program using the port?)")
                        warned = True
                    time.sleep(3)
                    continue
                warned = False
                print(f"connected: {ser.port}")
            state = build_state()
            line = encode_line(state)
            try:
                ser.write(line.encode("ascii"))
                junk = ser.read(4096)  # the board's own log lines: read so its buffer never fills
                if args.verbose and junk:
                    sys.stdout.write(junk.decode("ascii", "replace"))
            except Exception as e:
                print(f"lost the board ({e.__class__.__name__}); reconnecting")
                try:
                    ser.close()
                except Exception:
                    pass
                ser = None
                continue
            desc = describe(state)
            if desc != last_desc or args.verbose:
                print(time.strftime("[%H:%M:%S] ") + (line.strip() if args.verbose else desc))
                last_desc = desc
            time.sleep(SEND_EVERY_S)
    except KeyboardInterrupt:
        print()
    finally:
        if ser is not None:
            ser.close()
    return 0


DEMO = [
    dict(st=ST_WORKING, ctx=48200, max=200000, pct=24, cost=95, dur=640, api=180, la=40, lr=6),
    dict(st=ST_WORKING, ctx=91500, max=200000, pct=46, cost=260, dur=1900, api=520, la=180, lr=31),
    dict(st=ST_NEEDS_YOU, ctx=118000, max=200000, pct=59, cost=380, dur=2600, api=760, la=260, lr=40),
    dict(st=ST_WORKING, ctx=146000, max=200000, pct=73, cost=530, dur=3700, api=1100, la=402, lr=77),
    dict(st=ST_DONE, ctx=171000, max=200000, pct=86, cost=690, dur=4800, api=1500, la=560, lr=120),
]


def cmd_demo(args):
    serial = need_pyserial()
    ser = find_board(serial, args.port)
    if ser is None:
        raise SystemExit("board not found; plug it in or pass --port")
    print(f"demo on {ser.port}: Ctrl+C to stop")
    base = time.time()
    try:
        i = 0
        while True:
            step = DEMO[(i // 4) % len(DEMO)]
            state = dict(step, h5=18 + (i // 4) % 5 * 15, r5=int(9000 - (time.time() - base)) % 18000,
                         w7=44, r7=273600, n=1, model="Opus 4.5", proj="TamaPoke",
                         name="Claude usage display", eff="high")
            ser.write(encode_line(state).encode("ascii"))
            ser.read(4096)
            print(time.strftime("[%H:%M:%S] ") + describe(state))
            i += 1
            time.sleep(SEND_EVERY_S)
    except KeyboardInterrupt:
        print()
    finally:
        ser.close()
    return 0


def cmd_status(args):
    state = build_state()
    print(describe(state))
    print(encode_line(state).strip())
    sessions = load_sessions()
    if not sessions:
        print(f"(nothing recorded yet in {sessions_dir()}: run `install`, then use Claude Code)")
    print("bridge: " + ("running" if serve_running() else "not running")
          + ("" if load_config().get("autostart", True) else " (autostart off)"))
    return 0


def spawn_serve():
    """Starts `serve` from the stable copy, detached, with no console window.
    It must outlive the hook that started it, and survive the plugin update
    that deletes the folder this file came from."""
    script = refresh_stable_copy()
    if not script.exists():
        script = Path(__file__).resolve()
    py = Path(sys.executable)
    kw = {"stdin": subprocess.DEVNULL, "close_fds": True, "cwd": str(state_dir())}
    if os.name == "nt":
        w = py.with_name("pythonw.exe")
        if w.exists():
            py = w
        kw["creationflags"] = (subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP
                               | getattr(subprocess, "CREATE_BREAKAWAY_FROM_JOB", 0))
    else:
        kw["start_new_session"] = True
    log = open(state_dir() / "serve.log", "ab")
    kw["stdout"] = kw["stderr"] = log
    cmd = [str(py), "-u", str(script), "serve"]
    try:
        try:
            subprocess.Popen(cmd, **kw)
        except OSError:
            if os.name != "nt":
                raise
            # the job Claude Code ran the hook in may not allow breaking away
            kw["creationflags"] &= ~getattr(subprocess, "CREATE_BREAKAWAY_FROM_JOB", 0)
            subprocess.Popen(cmd, **kw)
    finally:
        log.close()


def cmd_ensure(args):
    """SessionStart hook: quiet, never fails (anything printed would reach Claude)."""
    try:
        state_dir().mkdir(parents=True, exist_ok=True)
        refresh_stable_copy()
        remember_python()
        if not load_config().get("autostart", True) or serve_running():
            return 0
        try:
            import serial  # noqa: F401
        except ImportError:
            return 0  # /tamapoke-monitor:setup says how to install it
        spawn_serve()
    except Exception:
        pass
    return 0


def cmd_stop(args):
    if not serve_running():
        print("the bridge is not running")
        return 0
    try:
        pid = int(pid_path().read_text(encoding="utf-8").strip())
    except (OSError, ValueError):
        print(f"the bridge is running but {pid_path()} is missing; stop it from your task manager")
        return 1
    try:
        import signal
        os.kill(pid, signal.SIGTERM)
    except OSError as e:
        print(f"could not stop the bridge (pid {pid}): {e}")
        return 1
    for _ in range(30):
        if not serve_running():
            break
        time.sleep(0.1)
    print("stopped the bridge" + (" (it starts again with the next Claude Code session; "
                                  "`autostart off` to keep it off)" if load_config().get("autostart", True) else ""))
    return 0


def cmd_autostart(args):
    config = load_config()
    if args.state:
        config["autostart"] = args.state == "on"
        write_json(state_dir() / "config.json", config)
    print("autostart " + ("on" if config.get("autostart", True) else "off"))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description="Show Claude Code usage on a TamaPoke board.",
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("install", "uninstall"):
        p = sub.add_parser(name)
        p.add_argument("--settings", help="settings.json to edit (default ~/.claude/settings.json)")
        if name == "install":
            p.add_argument("--plugin", action="store_true",
                           help="status line only: the tamapoke-monitor plugin brings the hooks")
    for name in ("serve", "demo"):
        p = sub.add_parser(name)
        p.add_argument("--port", help="serial port (default: find the board)")
        p.add_argument("-v", "--verbose", action="store_true", help="print every line and the board's log")
    for name in ("status", "statusline", "hook", "ensure", "stop"):
        sub.add_parser(name)
    p = sub.add_parser("autostart")
    p.add_argument("state", nargs="?", choices=("on", "off"))
    args = ap.parse_args(argv)
    return {"install": cmd_install, "uninstall": cmd_uninstall, "serve": cmd_serve, "demo": cmd_demo,
            "status": cmd_status, "statusline": cmd_statusline, "hook": cmd_hook, "ensure": cmd_ensure,
            "stop": cmd_stop, "autostart": cmd_autostart}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
