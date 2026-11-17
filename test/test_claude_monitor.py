#!/usr/bin/env python3
"""Tests for tools/claude_monitor.py (the Claude Code -> board bridge).

  python3 test/test_claude_monitor.py

No board, pyserial or Claude Code needed: the serial part is not exercised,
everything that decides what the board shows is.
"""
import io
import json
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock
from urllib.parse import unquote

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import claude_monitor as cm  # noqa: E402

# the example from Claude Code's status line documentation, trimmed
STATUSLINE = {
    "cwd": "/home/me/work/api-gateway",
    "session_id": "abc123",
    "session_name": "Fix the login bug",
    "model": {"id": "claude-opus-5-5", "display_name": "Opus"},
    "workspace": {"current_dir": "/home/me/work/api-gateway", "project_dir": "/home/me/work/api-gateway",
                  "repo": {"host": "github.com", "owner": "me", "name": "api-gateway"}},
    "cost": {"total_cost_usd": 0.01234, "total_duration_ms": 45000, "total_api_duration_ms": 2300,
             "total_lines_added": 156, "total_lines_removed": 23},
    "context_window": {"total_input_tokens": 15500, "total_output_tokens": 1200, "context_window_size": 200000,
                       "used_percentage": 8, "remaining_percentage": 92,
                       "current_usage": {"input_tokens": 8500, "output_tokens": 1200,
                                         "cache_creation_input_tokens": 5000, "cache_read_input_tokens": 2000}},
    "effort": {"level": "high"},
    "rate_limits": {"five_hour": {"used_percentage": 23.5, "resets_at": 1738425600},
                    "seven_day": {"used_percentage": 41.2, "resets_at": 1738857600}},
}


def decode_line(line):
    """Mirror of claudeParse() in claudemon.cpp, for round-trip checks."""
    assert line.startswith("CLAUDE ") and line.endswith("\n")
    out = {}
    for tok in line[7:].split():
        k, _, v = tok.partition("=")
        out[k] = unquote(v)
    return out


class TestSnapshot(unittest.TestCase):
    def test_reads_the_documented_fields(self):
        s = cm.snapshot_from_statusline(STATUSLINE)
        self.assertEqual(s["ctx"], 15500)
        self.assertEqual(s["max"], 200000)
        self.assertEqual(s["pct"], 8)
        self.assertEqual(s["cost"], 1)        # $0.01234 -> 1 cent
        self.assertEqual(s["dur"], 45)
        self.assertEqual(s["api"], 2)
        self.assertEqual((s["la"], s["lr"]), (156, 23))
        self.assertEqual((s["h5"], s["h5_at"]), (24, 1738425600))
        self.assertEqual((s["w7"], s["w7_at"]), (41, 1738857600))
        self.assertEqual(s["model"], "Opus")
        self.assertEqual(s["proj"], "api-gateway")
        self.assertEqual(s["name"], "Fix the login bug")
        self.assertEqual(s["eff"], "high")

    def test_before_the_first_reply_there_is_no_context_figure(self):
        d = {"context_window": {"total_input_tokens": 0, "context_window_size": 200000,
                                "used_percentage": None, "current_usage": None}}
        s = cm.snapshot_from_statusline(d)
        self.assertNotIn("ctx", s)
        self.assertNotIn("pct", s)
        self.assertEqual(s["max"], 200000)

    def test_tokens_from_current_usage_when_totals_are_missing(self):
        d = {"context_window": {"context_window_size": 1000000,
                                "current_usage": {"input_tokens": 10, "cache_creation_input_tokens": 20,
                                                  "cache_read_input_tokens": 30, "output_tokens": 999}}}
        self.assertEqual(cm.snapshot_from_statusline(d)["ctx"], 60)  # output tokens don't count

    def test_project_falls_back_to_the_folder_name(self):
        d = {"workspace": {"project_dir": "C:\\Users\\me\\proj\\"}, "cwd": "/x"}
        self.assertEqual(cm.snapshot_from_statusline(d)["proj"], "proj")
        self.assertEqual(cm.snapshot_from_statusline({"cwd": "/home/me/thing/"})["proj"], "thing")

    def test_junk_does_not_crash(self):
        for junk in (None, [], "x", {"context_window": "nope", "cost": [1], "rate_limits": {"five_hour": 3}},
                     {"cost": {"total_cost_usd": True}}):
            cm.snapshot_from_statusline(junk)

    def test_spend_limit(self):
        d = {"rate_limits": {"spend_limit": {"used_percentage": 62.8, "resets_at": 1740787200,
                                             "used_usd": 314.12, "limit_usd": 500}}}
        s = cm.snapshot_from_statusline(d)
        self.assertEqual((s["sp"], s["sp_at"], s["spu"], s["spl"]), (63, 1740787200, 31412, 50000))


class TestHooks(unittest.TestCase):
    def test_events_map_to_board_states(self):
        f = cm.status_for_hook
        self.assertEqual(f({"hook_event_name": "UserPromptSubmit"}), "working")
        self.assertEqual(f({"hook_event_name": "PostToolUse"}), "working")
        self.assertEqual(f({"hook_event_name": "PermissionRequest"}), "needs_you")
        self.assertEqual(f({"hook_event_name": "Notification", "notification_type": "permission_prompt"}), "needs_you")
        self.assertIsNone(f({"hook_event_name": "Notification", "notification_type": "auth_success"}))
        self.assertIsNone(f({"hook_event_name": "Notification", "notification_type": "idle_prompt"}))
        self.assertEqual(f({"hook_event_name": "Stop"}), "done")
        self.assertEqual(f({"hook_event_name": "StopFailure", "error": "rate_limit"}), "error")
        self.assertEqual(f({"hook_event_name": "SessionStart"}), "idle")
        self.assertIsNone(f({"hook_event_name": "PreToolUse"}))


def sess(at, snap=None, hook=None, hook_at=None, changed_at=0):
    s = {"status": {"at": at, "changed_at": changed_at, "snap": snap or {}}}
    if hook:
        s["hook"] = {"status": hook, "at": hook_at if hook_at is not None else at}
    return s


class TestBuildState(unittest.TestCase):
    NOW = 1_800_000_000

    def test_shows_the_session_that_needs_you_over_newer_ones(self):
        n = self.NOW
        state = cm.build_state(n, {
            "a": sess(n - 5, {"ctx": 1, "model": "A"}, "working"),
            "b": sess(n - 50, {"ctx": 2, "model": "B"}, "needs_you"),
            "c": sess(n - 1, {"ctx": 3, "model": "C"}, "idle"),
        })
        self.assertEqual(state["model"], "B")
        self.assertEqual(state["st"], cm.ST_NEEDS_YOU)
        self.assertEqual(state["n"], 3)

    def test_working_beats_done_and_newest_wins_a_tie(self):
        n = self.NOW
        state = cm.build_state(n, {"a": sess(n - 9, {"model": "A"}, "done"),
                                   "b": sess(n - 20, {"model": "B"}, "working"),
                                   "c": sess(n - 2, {"model": "C"}, "working")})
        self.assertEqual(state["model"], "C")

    def test_stale_sessions_are_dropped(self):
        n = self.NOW
        state = cm.build_state(n, {"old": sess(n - cm.STALE_S - 1, {"model": "Old"}, "working")})
        self.assertEqual(state["st"], cm.ST_NONE)
        self.assertEqual(state["n"], 0)
        self.assertNotIn("model", state)

    def test_an_escaped_turn_stops_looking_busy(self):
        n = self.NOW
        s = sess(n - 5, {"model": "A"}, "working", hook_at=n - cm.WORKING_STALE_S - 10,
                 changed_at=n - cm.WORKING_STALE_S - 10)
        self.assertEqual(cm.build_state(n, {"a": s})["st"], cm.ST_IDLE)

    def test_without_hooks_moving_numbers_mean_working(self):
        n = self.NOW
        self.assertEqual(cm.build_state(n, {"a": sess(n, {}, changed_at=n - 5)})["st"], cm.ST_WORKING)
        self.assertEqual(cm.build_state(n, {"a": sess(n, {}, changed_at=n - 60)})["st"], cm.ST_IDLE)

    def test_plan_limits_come_from_the_freshest_snapshot_and_count_down(self):
        n = self.NOW
        state = cm.build_state(n, {
            "a": sess(n - 1, {"model": "A"}, "working"),  # shown, but has no limits
            "b": sess(n - 30, {"h5": 40, "h5_at": n + 600, "w7": 10, "w7_at": n + 86400}),
            "c": sess(n - 300, {"h5": 20, "h5_at": n + 900}),
        })
        self.assertEqual((state["h5"], state["r5"]), (40, 600))
        self.assertEqual((state["w7"], state["r7"]), (10, 86400))

    def test_a_window_that_already_reset_is_not_shown(self):
        n = self.NOW
        state = cm.build_state(n, {"a": sess(n - 1, {"h5": 90, "h5_at": n - 1, "w7": 50, "w7_at": n + 50})})
        self.assertNotIn("h5", state)
        self.assertEqual(state["w7"], 50)

    def test_limits_stay_for_a_while_after_the_last_session_closed(self):
        n = self.NOW
        snap = {"h5": 30, "h5_at": n + 3600}
        state = cm.build_state(n, {"a": sess(n - cm.STALE_S - 5, snap)})
        self.assertEqual(state["st"], cm.ST_NONE)
        self.assertEqual(state["h5"], 30)
        state = cm.build_state(n, {"a": sess(n - cm.LIMITS_KEEP_S - 5, {"h5": 30, "h5_at": n + 99999})})
        self.assertNotIn("h5", state)


class TestLine(unittest.TestCase):
    def test_format_matches_the_firmware_protocol(self):
        state = {"st": 1, "ctx": 124300, "max": 200000, "pct": 62, "cost": 421, "h5": 23, "r5": 8040, "n": 2,
                 "model": "Opus 4.5", "proj": "TamaPoke", "name": "Fix login", "eff": "high"}
        line = cm.encode_line(state)
        self.assertEqual(line, "CLAUDE v=1 st=1 ctx=124300 max=200000 pct=62 cost=421 h5=23 r5=8040 n=2 "
                               "model=Opus%204.5 proj=TamaPoke name=Fix%20login eff=high\n")

    def test_text_is_ascii_bounded_and_round_trips(self):
        state = {"st": 2, "model": "Café ✓ model", "name": "x" * 80, "proj": "a=b c%d"}
        line = cm.encode_line(state)
        self.assertTrue(line.isascii())
        d = decode_line(line)
        self.assertEqual(d["model"], "Cafe model")
        self.assertEqual(len(d["name"]), cm.TEXT_KEYS["name"])
        self.assertEqual(d["proj"], "a=b c%d")
        self.assertEqual(line.count("\n"), 1)

    def test_unknowns_are_left_out(self):
        self.assertEqual(cm.encode_line({"st": 0, "model": "", "ctx": None}), "CLAUDE v=1 st=0\n")


class TestInstall(unittest.TestCase):
    def test_install_chains_an_existing_status_line_and_keeps_user_hooks(self):
        settings = {"statusLine": {"type": "command", "command": "~/.claude/mine.sh", "padding": 1},
                    "hooks": {"Stop": [{"hooks": [{"type": "command", "command": "say done"}]}]},
                    "model": "opus"}
        config = {}
        cm.install_into(settings, '"py" "/x/claude_monitor.py" statusline', '"py" "/x/claude_monitor.py" hook', config)
        self.assertEqual(config["chain"], "~/.claude/mine.sh")
        self.assertIn("claude_monitor.py", settings["statusLine"]["command"])
        self.assertEqual(settings["statusLine"]["padding"], 1)
        self.assertEqual(settings["statusLine"]["refreshInterval"], cm.REFRESH_S)
        self.assertEqual(settings["model"], "opus")
        stop = settings["hooks"]["Stop"]
        self.assertEqual(stop[0]["hooks"][0]["command"], "say done")
        self.assertTrue(stop[1]["hooks"][0]["async"])
        for ev in cm.HOOK_EVENTS:
            self.assertIn(ev, settings["hooks"])

    def test_install_twice_changes_nothing_more(self):
        settings, config = {}, {}
        cm.install_into(settings, "a claude_monitor.py statusline", "a claude_monitor.py hook", config)
        once = json.dumps(settings, sort_keys=True)
        cm.install_into(settings, "a claude_monitor.py statusline", "a claude_monitor.py hook", config)
        self.assertEqual(json.dumps(settings, sort_keys=True), once)
        self.assertNotIn("chain", config)  # our own status line is never chained to itself

    def test_uninstall_restores_what_was_there(self):
        original = {"statusLine": {"type": "command", "command": "~/.claude/mine.sh"},
                    "hooks": {"Stop": [{"hooks": [{"type": "command", "command": "say done"}]}]}}
        settings, config = json.loads(json.dumps(original)), {}
        cm.install_into(settings, "p claude_monitor.py statusline", "p claude_monitor.py hook", config)
        cm.uninstall_from(settings, config)
        self.assertEqual(settings, original)
        self.assertNotIn("chain", config)

    def test_uninstall_of_a_clean_install_leaves_empty_settings(self):
        settings, config = {}, {}
        cm.install_into(settings, "p claude_monitor.py statusline", "p claude_monitor.py hook", config)
        self.assertEqual(cm.uninstall_from(settings, config), {})

    def test_install_command_edits_a_settings_file_with_a_backup(self):
        with tempfile.TemporaryDirectory() as tmp, mock.patch.dict(os.environ, {"TAMAPOKE_CLAUDE_DIR": tmp}):
            path = Path(tmp) / "settings.json"
            path.write_text('{"theme": "dark"}', encoding="utf-8")
            with redirect_stdout(io.StringIO()):
                cm.main(["install", "--settings", str(path)])
            data = json.loads(path.read_text(encoding="utf-8"))
            self.assertEqual(data["theme"], "dark")
            self.assertIn("statusline", data["statusLine"]["command"])
            self.assertTrue(list(Path(tmp).glob("settings.json.bak-tamapoke-*")))

    def test_broken_settings_are_not_touched(self):
        with tempfile.TemporaryDirectory() as tmp, mock.patch.dict(os.environ, {"TAMAPOKE_CLAUDE_DIR": tmp}):
            path = Path(tmp) / "settings.json"
            path.write_text("{ not json", encoding="utf-8")
            with self.assertRaises(SystemExit):
                cm.main(["install", "--settings", str(path)])
            self.assertEqual(path.read_text(encoding="utf-8"), "{ not json")


class TestCommands(unittest.TestCase):
    def run_cmd(self, argv, stdin):
        out = io.StringIO()
        with mock.patch("sys.stdin", io.StringIO(stdin)), redirect_stdout(out):
            cm.main(argv)
        return out.getvalue()

    def test_statusline_records_a_snapshot_and_prints_a_line(self):
        with tempfile.TemporaryDirectory() as tmp, mock.patch.dict(os.environ, {"TAMAPOKE_CLAUDE_DIR": tmp}):
            out = self.run_cmd(["statusline"], json.dumps(STATUSLINE))
            self.assertIn("ctx 8%", out)
            rec = json.loads((Path(tmp) / "sessions" / "abc123.status.json").read_text(encoding="utf-8"))
            self.assertEqual(rec["snap"]["ctx"], 15500)
            self.assertEqual(rec["changed_at"], 0)  # first sight: not "working" yet
            more = json.loads(json.dumps(STATUSLINE))
            more["context_window"]["total_input_tokens"] = 30000
            self.run_cmd(["statusline"], json.dumps(more))
            rec = json.loads((Path(tmp) / "sessions" / "abc123.status.json").read_text(encoding="utf-8"))
            self.assertGreater(rec["changed_at"], 0)

    @unittest.skipIf(os.name == "nt", "uses a POSIX shell command")
    def test_statusline_chains_the_previous_command(self):
        with tempfile.TemporaryDirectory() as tmp, mock.patch.dict(os.environ, {"TAMAPOKE_CLAUDE_DIR": tmp}):
            cm.write_json(Path(tmp) / "config.json", {"chain": "echo mine"})
            self.assertEqual(self.run_cmd(["statusline"], json.dumps(STATUSLINE)).strip(), "mine")

    def test_statusline_survives_garbage(self):
        with tempfile.TemporaryDirectory() as tmp, mock.patch.dict(os.environ, {"TAMAPOKE_CLAUDE_DIR": tmp}):
            self.assertTrue(self.run_cmd(["statusline"], "not json").strip())

    def test_hooks_update_the_session_and_session_end_forgets_it(self):
        with tempfile.TemporaryDirectory() as tmp, mock.patch.dict(os.environ, {"TAMAPOKE_CLAUDE_DIR": tmp}):
            self.run_cmd(["statusline"], json.dumps(STATUSLINE))
            self.run_cmd(["hook"], json.dumps({"session_id": "abc123", "hook_event_name": "PermissionRequest"}))
            self.assertEqual(cm.build_state()["st"], cm.ST_NEEDS_YOU)
            self.run_cmd(["hook"], json.dumps({"session_id": "abc123", "hook_event_name": "Stop"}))
            self.assertEqual(cm.build_state()["st"], cm.ST_DONE)
            self.run_cmd(["hook"], json.dumps({"session_id": "abc123", "hook_event_name": "SessionEnd"}))
            self.assertEqual(cm.build_state()["n"], 0)
            self.assertEqual(list((Path(tmp) / "sessions").glob("*.json")), [])

    def test_hook_never_raises(self):
        with tempfile.TemporaryDirectory() as tmp, mock.patch.dict(os.environ, {"TAMAPOKE_CLAUDE_DIR": tmp}):
            for junk in ("", "[]", "{", '{"hook_event_name": 5}'):
                self.run_cmd(["hook"], junk)


if __name__ == "__main__":
    unittest.main(verbosity=2)
