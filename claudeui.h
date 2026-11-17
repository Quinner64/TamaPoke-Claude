#pragma once
#include "claudemon.h"

class Arduino_GFX;

// Draws one dashboard frame (see claudeBuildFrame) onto the canvas, without
// flushing. (cx, cy) is the centre of the panel; the layout is designed for the
// 466 px round AMOLED and simply centres on the 480 px boards.
//
// Uses U8g2 fonts (proportional, much easier to read across a desk than the
// 5x7 game font). It leaves one of them selected: the caller must restore the
// game's own font afterwards (applyLangFont() in the sketch).
void claudeDraw(Arduino_GFX *g, const ClaudeFrame &f, int16_t cx, int16_t cy, bool roundPanel);
