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
# and U8g2 sources, copies out the few U8g2 fonts claudeui.cpp uses, builds
# claudemon.cpp + claudeui.cpp + Arduino_GFX's drawing core with g++ and turns
# each scene into a PNG (needs Pillow). No board or arduino-cli needed.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
LIVE=0
if [[ "${1:-}" == "--live" ]]; then LIVE=1; shift; fi
if [[ $LIVE -eq 1 ]]; then OUT="${1:-$ROOT/tools/build/claude_preview/live}"; else OUT="${1:-$ROOT/docs/claude-monitor}"; fi
DL="$ROOT/tools/downloads"
BUILD="$ROOT/tools/build/claude_preview"
GFX_TAG="v1.6.9"    # GFX Library for Arduino, same major/minor the CI installs
U8G2_TAG="2.37.1"

mkdir -p "$DL" "$BUILD" "$OUT"
if [[ ! -d "$DL/Arduino_GFX-$GFX_TAG" ]]; then
  git clone -q --depth 1 --branch "$GFX_TAG" https://github.com/moononournation/Arduino_GFX.git "$DL/Arduino_GFX-$GFX_TAG"
fi
if [[ ! -f "$DL/u8g2-$U8G2_TAG/csrc/u8g2_fonts.c" ]]; then
  rm -rf "$DL/u8g2-$U8G2_TAG"
  git clone -q --depth 1 --branch "$U8G2_TAG" --filter=blob:none --sparse https://github.com/olikraus/u8g2.git "$DL/u8g2-$U8G2_TAG"
  git -C "$DL/u8g2-$U8G2_TAG" sparse-checkout set csrc
fi

python3 "$HERE/extract_fonts.py" "$DL/u8g2-$U8G2_TAG/csrc/u8g2_fonts.c" "$BUILD/gen" "$ROOT/claudeui.cpp"

GFX="$DL/Arduino_GFX-$GFX_TAG/src"
# shim/ first (Arduino.h, Print.h, the trimmed Arduino_GFX_Library.h), then the
# generated U8g2lib.h with just our fonts, then the library itself
INC=(-I "$HERE/shim" -I "$BUILD/gen" -I "$GFX")
g++ -std=c++17 -O2 -w "${INC[@]}" -o "$BUILD/preview" \
  "$HERE/preview.cpp" "$ROOT/claudemon.cpp" "$ROOT/claudeui.cpp" \
  "$GFX/Arduino_GFX.cpp" "$GFX/Arduino_G.cpp" "$GFX/canvas/Arduino_Canvas.cpp" \
  -x c "$BUILD/gen/fonts.c" -lm

# a real gallery thumbnail for the "waiting" screen (Pikachu, from the PMD sprite)
python3 "$HERE/make_png.py" thumb "$ROOT/tools/sdcard/mons/p025.bin" "$BUILD/pet.bin"
rm -f "$BUILD"/*.rgb565
if [[ $LIVE -eq 1 ]]; then
  python3 "$ROOT/tools/claude_monitor.py" status | grep '^CLAUDE ' | "$BUILD/preview" "$BUILD" "$BUILD/pet.bin" live
else
  "$BUILD/preview" "$BUILD" "$BUILD/pet.bin"
fi
python3 "$HERE/make_png.py" png "$BUILD" "$OUT"
