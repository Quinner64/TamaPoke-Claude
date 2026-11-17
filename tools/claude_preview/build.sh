#!/usr/bin/env bash
# Renders the Claude dashboard previews on the PC from the firmware's own code.
#
#   tools/claude_preview/build.sh [OUT_DIR]      (default: docs/claude-monitor)
#   tools/claude_preview/build.sh --live [OUT_DIR]
#                     your real data instead of the sample scenes: renders what
#                     `tools/claude_monitor.py status` would send right now
#                     (default OUT_DIR for --live: tools/build/claude_preview/live)
#
# Downloads (once, into tools/downloads/, git-ignored) the pinned Arduino_GFX
# source, builds claudemon.cpp + claudeui.cpp + scene.cpp + Arduino_GFX's
# drawing core with g++, and turns each scene into a PNG, plus a GIF for the
# animated ones (needs Pillow). The pets are the real PMD sprites in
# tools/sdcard/mons/. No board or arduino-cli needed.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
LIVE=0
if [[ "${1:-}" == "--live" ]]; then LIVE=1; shift; fi
if [[ $LIVE -eq 1 ]]; then OUT="${1:-$ROOT/tools/build/claude_preview/live}"; else OUT="${1:-$ROOT/docs/claude-monitor}"; fi
DL="$ROOT/tools/downloads"
BUILD="$ROOT/tools/build/claude_preview"
GFX_TAG="v1.6.9"    # GFX Library for Arduino, same major/minor the CI installs

mkdir -p "$DL" "$BUILD" "$OUT"
if [[ ! -d "$DL/Arduino_GFX-$GFX_TAG" ]]; then
  git clone -q --depth 1 --branch "$GFX_TAG" https://github.com/moononournation/Arduino_GFX.git "$DL/Arduino_GFX-$GFX_TAG"
fi

GFX="$DL/Arduino_GFX-$GFX_TAG/src"
# shim/ first (Arduino.h, Print.h, the trimmed Arduino_GFX_Library.h), then the library itself
INC=(-I "$HERE/shim" -I "$GFX")
g++ -std=c++17 -O2 -w "${INC[@]}" -o "$BUILD/preview" \
  "$HERE/preview.cpp" "$ROOT/claudemon.cpp" "$ROOT/claudeui.cpp" "$ROOT/scene.cpp" \
  "$GFX/Arduino_GFX.cpp" "$GFX/Arduino_G.cpp" "$GFX/canvas/Arduino_Canvas.cpp" -lm

# the pet's gallery thumbnail for the session card (Pikachu, from its PMD sprite)
MONS="$ROOT/tools/sdcard/mons"
python3 "$HERE/make_png.py" thumb "$MONS/p025.bin" "$BUILD/pet.bin"
rm -f "$BUILD"/*.rgb565
if [[ $LIVE -eq 1 ]]; then
  python3 "$ROOT/tools/claude_monitor.py" status | grep '^CLAUDE ' | "$BUILD/preview" "$BUILD" "$MONS" "$BUILD/pet.bin" live
else
  "$BUILD/preview" "$BUILD" "$MONS" "$BUILD/pet.bin"
fi
python3 "$HERE/make_png.py" png "$BUILD" "$OUT"
