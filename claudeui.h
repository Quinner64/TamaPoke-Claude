#pragma once
#include "claudemon.h"

class Arduino_GFX;
struct PmdMon;

// The pet's side of the dashboard: where it lives and how it looks right now.
struct ClaudeScene {
  uint8_t biome;        // pet's biome (scene.h); 0 = meadow for an egg
  bool night;           // night palette (night hours, or the pet is asleep)
  int hour;             // 0-23, for dawn / day / dusk
  bool asleep;          // the pet sleeps in the game: it sleeps here too
  const PmdMon *pet;    // the pet's animated sprite, or null (not loaded / egg)
  const uint8_t *thumb; // fallback when there is no animated sprite: gallery thumbnail, or null
};

// Draws one dashboard frame (see claudeBuildFrame) onto the canvas, without
// flushing, in the game's own look: the pet's biome and animated sprite, the 5x7
// pixel font, cream cards and stat bars. (cx, cy) is the centre of the panel;
// the layout is designed for the 466 px round AMOLED and centres on 480 px boards.
//
// It selects the classic font; the caller restores the language font afterwards
// (applyLangFont() in the sketch).
void claudeDraw(Arduino_GFX *g, const ClaudeFrame &f, const ClaudeScene &sc, uint32_t nowMs, int16_t cx, int16_t cy,
                bool roundPanel);
