#!/usr/bin/env python3
"""Claude Code -> TamaPoke bridge (moved).

The bridge now lives in the Claude Code plugin, so a plugin install carries it:
plugins/tamapoke-monitor/scripts/claude_monitor.py. This wrapper keeps the old
commands working, for example:

    python3 tools/claude_monitor.py serve
"""
import os
import runpy
import sys

REAL = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir,
                                     "plugins", "tamapoke-monitor", "scripts", "claude_monitor.py"))

if __name__ == "__main__":
    sys.argv[0] = REAL
    runpy.run_path(REAL, run_name="__main__")
