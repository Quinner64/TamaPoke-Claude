// Background of the pet screen: sky by the hour (dawn, day, dusk, night), sun,
// moon and stars, and the ground of the pet's biome. Shared by the game
// (TamaPoke.ino) and the Claude dashboard (claudeui.cpp), so both draw the
// same world; moved here unchanged from TamaPoke.ino.
#include <Arduino.h>
#include "Arduino_GFX_Library.h"
#include "scene.h"

#define SCENE_WHITE 0xFFFF  // UI_WHITE in species.h

// estrellas del modo noche
const uint16_t STARS[6][2] = { {120,140},{330,120},{370,210},{95,230},{280,90},{160,95} };

// suelo de cada bioma de dia (de noche se mezcla hacia el azul nocturno)
const uint16_t BIOME_SOIL[6] = {
  C565(0x7e, 0xc0, 0x7f),  // 0 pradera
  C565(0xdc, 0xca, 0x94),  // 1 playa (arena)
  C565(0x4f, 0x8a, 0x55),  // 2 bosque
  C565(0x8a, 0x55, 0x44),  // 3 volcan
  C565(0xa8, 0x90, 0x6a),  // 4 montana
  C565(0xe6, 0xee, 0xf5),  // 5 nieve
};

uint16_t lerp565(uint16_t a, uint16_t b, int i, int n) {
  if (n <= 0) return a;
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  return (uint16_t)((((ar + (br - ar) * i / n) << 11)) |
                    (((ag + (bg - ag) * i / n) << 5)) | (ab + (bb - ab) * i / n));
}

void drawClouds(Arduino_GFX *g, uint32_t now, uint16_t col) {
  for (int k = 0; k < 2; k++) {
    int cx = (int)((now / 50 + k * 250) % 560) - 40;
    int cy = 70 + k * 34;
    g->fillCircle(cx, cy, 16, col);
    g->fillCircle(cx + 18, cy + 3, 13, col);
    g->fillCircle(cx - 15, cy + 4, 12, col);
  }
}

void sceneDraw(Arduino_GFX *g, int W, int H, uint8_t biome, uint32_t now, bool night, int h) {
  const int HZ = H / 2 - 1;  // the horizon: where the sky meets the ground
  uint16_t top, bot;
  if (night)            { top = C565(0x0c, 0x12, 0x24); bot = C565(0x1e, 0x26, 0x46); }
  else if (h < 8)       { top = C565(0xd1, 0x6a, 0x86); bot = C565(0xf3, 0xb8, 0x7c); }  // amanecer
  else if (h < 18)      { top = C565(0x8f, 0xc8, 0xea); bot = C565(0xdc, 0xee, 0xe6); }  // dia
  else                  { top = C565(0xc7, 0x5a, 0x4a); bot = C565(0xf0, 0xae, 0x64); }  // atardecer

  // cielo en bandas
  for (int y = 0; y < HZ; y += 8)
    g->fillRect(0, y, W, 8, lerp565(top, bot, y, HZ));

  // sol o luna
  if (night) {
    g->fillCircle(360, 78, 24, C565(0xe8, 0xee, 0xf5));
    g->fillCircle(370, 72, 22, lerp565(top, bot, 78, HZ));  // creciente
    for (auto &st : STARS) g->fillRect(st[0], st[1], 4, 4, SCENE_WHITE);
  } else if (h < 18) {
    g->fillCircle(360, 84, 26, h < 8 ? C565(0xff, 0xd9, 0x8a) : C565(0xff, 0xe7, 0x9f));
    drawClouds(g, now, C565(0xff, 0xff, 0xff));
  } else {
    g->fillCircle(W / 2, HZ - 6, 34, C565(0xff, 0xf1, 0xc8));  // sol poniente
  }

  // mar de la playa: una franja de agua sobre la arena
  uint16_t soil = BIOME_SOIL[biome < 6 ? biome : 0];
  if (night) soil = lerp565(soil, C565(0x16, 0x1c, 0x30), 9, 16);
  if (biome == 1) {
    uint16_t sea = night ? C565(0x1c, 0x34, 0x52) : C565(0x4f, 0x96, 0xc4);
    g->fillRect(0, HZ - 26, W, 26, sea);
    for (int i = 0; i < 3; i++) {
      int wy = HZ - 22 + i * 7;
      uint16_t fc = night ? C565(0x3a, 0x58, 0x78) : C565(0xbf, 0xe6, 0xf5);
      g->fillRect(60 + ((now / 60 + i * 30) % 60), wy, 26, 2, fc);
      g->fillRect(300 - ((now / 60 + i * 20) % 60), wy, 26, 2, fc);
    }
  }

  // suelo
  g->fillRect(0, HZ, W, H - HZ, soil);
  uint16_t hill = lerp565(soil, night ? C565(0x0c, 0x12, 0x24) : C565(0xff, 0xff, 0xff), 3, 16);
  g->fillRoundRect(-60, HZ - 14, 586, 60, 30, hill);

  // detalles del bioma
  uint16_t dk = lerp565(soil, C565(0x10, 0x18, 0x20), night ? 11 : 7, 16);
  if (biome == 2) {  // bosque: coniferas en silueta
    for (int tx : { 60, 150, 360, 416 }) {
      g->fillTriangle(tx, HZ - 46, tx - 16, HZ, tx + 16, HZ, dk);
      g->fillTriangle(tx, HZ - 60, tx - 12, HZ - 28, tx + 12, HZ - 28, dk);
    }
  } else if (biome == 3) {  // volcan: rocas y brasas
    g->fillTriangle(70, HZ, 40, HZ + 30, 100, HZ + 30, dk);
    g->fillTriangle(400, HZ + 4, 372, HZ + 30, 430, HZ + 30, dk);
    if (!night)
      for (int e = 0; e < 4; e++)
        g->fillRect(120 + e * 70, HZ + 8 + (e % 2) * 6, 4, 4, C565(0xff, 0x9b, 0x3a));
  } else if (biome == 4) {  // montana: cumbres al fondo
    g->fillTriangle(140, HZ - 50, 60, HZ, 220, HZ, dk);
    g->fillTriangle(330, HZ - 38, 250, HZ, 410, HZ, dk);
  } else if (biome == 5 && !night) {  // nieve: copos cayendo
    for (int f = 0; f < 10; f++) {
      int fx = (f * 53 + now / 40) % W;
      int fy = (f * 90 + now / 18) % HZ;
      g->fillRect(fx, fy, 3, 3, SCENE_WHITE);
    }
  } else if (biome == 0) {  // pradera: matas de hierba
    for (int gx : { 80, 175, 300, 395 })
      for (int b = -1; b <= 1; b++)
        g->fillRect(gx + b * 5, HZ + 6, 2, 8 + (b == 0 ? 4 : 0), dk);
  }
}
