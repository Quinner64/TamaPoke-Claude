---
name: bridge
description: Control the TamaPoke USB bridge - status, start, stop, demo, or autostart on/off.
argument-hint: "[status|start|stop|demo|autostart on|autostart off]"
disable-model-invocation: true
allowed-tools: Bash(bash "${CLAUDE_PLUGIN_ROOT}/scripts/run.sh" *)
---

Control the TamaPoke Claude monitor bridge. The request: `$ARGUMENTS`
(empty means `status`). Run the matching command and report the result in a
line or two.

- **status**: `bash "${CLAUDE_PLUGIN_ROOT}/scripts/run.sh" status`
- **start**: `bash "${CLAUDE_PLUGIN_ROOT}/scripts/run.sh" ensure`, then `status`.
  If autostart is off, `ensure` does nothing: run `autostart on` first and say so.
- **stop**: `bash "${CLAUDE_PLUGIN_ROOT}/scripts/run.sh" stop`. Use this before
  flashing firmware or running `tools/send_sd.py`, which need the same USB port.
  It starts again with the next Claude Code session unless autostart is off.
- **demo**: stop the bridge first, then run
  `bash "${CLAUDE_PLUGIN_ROOT}/scripts/run.sh" demo` in the background for about
  30 seconds so the user can see the screens, stop it, and start the bridge again.
- **autostart on** / **autostart off**: `bash "${CLAUDE_PLUGIN_ROOT}/scripts/run.sh" autostart on`
  (or `off`).

If a command prints nothing at all, no Python 3 was found: point the user to
`/tamapoke-monitor:setup`.
