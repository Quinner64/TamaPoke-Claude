#pragma once
#include <stdint.h>

// The pet's world: sky by the hour, sun/moon/stars and the biome's ground.
// Drawn by the game's main screen and minigame (TamaPoke.ino) and by the
// Claude dashboard (claudeui.cpp). Code in scene.cpp.

class Arduino_GFX;

#define C565(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

extern const uint16_t STARS[6][2];    // night sky
extern const uint16_t BIOME_SOIL[6];  // ground of each biome by day

uint16_t lerp565(uint16_t a, uint16_t b, int i, int n);
void drawClouds(Arduino_GFX *g, uint32_t now, uint16_t col);
// Covers the whole W x H canvas. h = hour of the day (0-23); night = dark palette.
void sceneDraw(Arduino_GFX *g, int W, int H, uint8_t biome, uint32_t now, bool night, int h);
