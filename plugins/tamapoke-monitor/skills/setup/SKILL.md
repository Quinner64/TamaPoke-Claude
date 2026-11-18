---
name: setup
description: One-time setup of the TamaPoke Claude monitor on this computer - adds the status line that feeds the board and checks Python and pyserial.
disable-model-invocation: true
allowed-tools: Bash(bash "${CLAUDE_PLUGIN_ROOT}/scripts/run.sh" *)
---

Set up the TamaPoke Claude monitor on this computer. The plugin already brings
the hooks and starts the USB bridge with each session; what a plugin cannot do
by itself is set Claude Code's status line, which is where the context window,
cost and plan limits come from. Do these steps in order and report briefly.

1. Run:

   ```bash
   bash "${CLAUDE_PLUGIN_ROOT}/scripts/run.sh" install --plugin
   ```

   It backs up `~/.claude/settings.json`, sets the status line (an existing
   status line keeps showing: it is chained), and removes hooks a by-hand
   install of the bridge left in the settings, so nothing runs twice.

   If the command prints nothing at all, no Python 3 was found. Tell the user
   to install Python 3 (python.org, or their package manager), then run this
   setup again. Stop here.

2. If the output says pyserial is missing, ask the user whether to install it,
   and on a yes run the exact `-m pip install pyserial` command it printed.

3. Start the bridge now, then show its state:

   ```bash
   bash "${CLAUDE_PLUGIN_ROOT}/scripts/run.sh" ensure
   bash "${CLAUDE_PLUGIN_ROOT}/scripts/run.sh" status
   ```

   `ensure` prints nothing. If `status` says the bridge is not running, run
   `bash "${CLAUDE_PLUGIN_ROOT}/scripts/run.sh" serve -v` with a short timeout
   to see why (usually the board is unplugged or another program holds the port).

4. Tell the user: the board switches to the Claude dashboard within a few
   seconds once the bridge connects; the status line takes effect in new
   sessions; and `/tamapoke-monitor:bridge stop` frees the USB port before
   flashing firmware or sending sprites.
