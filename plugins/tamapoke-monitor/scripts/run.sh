#!/usr/bin/env bash
# Runs claude_monitor.py with a working Python 3 (the plugin's hooks call this).
#
# The interpreter is remembered in ~/.claude/tamapoke/python (written by
# `ensure` and `install`), so hooks don't probe for one every time. Otherwise it
# tries python3, then python: on Windows `python3` is often only the Microsoft
# Store stub, which is why each one is actually run before it is used.
# With no Python at all it exits quietly: a hook must never get in Claude's way.
here="$(cd "$(dirname "$0")" && pwd)"
dir="${TAMAPOKE_CLAUDE_DIR:-$HOME/.claude/tamapoke}"

py=""
if [ -f "$dir/python" ]; then
  py="$(head -n 1 "$dir/python" | tr -d '\r')"
  [ -n "$py" ] && [ -e "$py" ] || py=""
fi
if [ -z "$py" ]; then
  for c in python3 python; do
    if "$c" -c 'import sys; sys.exit(sys.version_info < (3, 8))' >/dev/null 2>&1; then
      py="$c"
      break
    fi
  done
fi
[ -n "$py" ] || exit 0

exec "$py" "$here/claude_monitor.py" "$@"
