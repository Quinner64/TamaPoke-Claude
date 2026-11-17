#pragma once
// Drawing of the animated PMD sprites (PmdMon, see sdmon.h), shared by the game
// (TamaPoke.ino) and the Claude dashboard (claudeui.cpp). Moved here unchanged
// from TamaPoke.ino; only the canvas and the silhouette ink became parameters.
#include "Arduino_GFX_Library.h"
#include "sdmon.h"
#include "blit.h"

inline uint32_t pmdActTotalMs(const PmdAct &a) {
  uint32_t t = 0;
  for (uint8_t i = 0; i < a.frames; i++) t += a.ms[i];
  return t ? t : 100;
}

inline uint8_t pmdFrameAt(const PmdAct &a, uint32_t t, bool loop) {
  uint32_t total = pmdActTotalMs(a);
  if (!loop && t >= total) return a.frames - 1;
  t %= total;
  uint8_t i = 0;
  // guarda: nunca mas vueltas que frames, por si un .bin corrupto trae ms[]=0
  // pese al saneo de la carga (defensa en profundidad)
  for (uint8_t guard = 0; guard < a.frames && t >= a.ms[i]; guard++) {
    t -= a.ms[i];
    i = (i + 1) % a.frames;
  }
  return i;
}

// dibuja una accion anclada por la base (centro-x, suelo); devuelve la y de arriba del frame
inline int pmdDraw(Arduino_GFX *g, const PmdMon &m, uint8_t actId, int cx, int groundY, uint32_t t, bool loop,
                   bool sil, uint8_t maxS, uint16_t silColor) {
  const PmdAct &a = m.acts[actId];
  if (!a.frames) return groundY;
  uint8_t sBase = m.acts[PMD_IDLE].h ? 170 / m.acts[PMD_IDLE].h : 5;
  if (sBase < 2) sBase = 2;
  if (sBase > maxS) sBase = maxS;
  uint8_t s = sBase;
  while (s > 2 && a.h * s > 250) s--;  // acciones con frame grande (ataque)
  uint8_t fi = pmdFrameAt(a, t, loop);
  const uint8_t *fr = a.data + (uint32_t)fi * a.w * a.h;
  // anclar por los pies (a.base), no por el alto del lienzo: asi las acciones
  // con padding distinto (Hurt, Eat...) quedan todas a la misma altura de suelo
  int x0 = cx - a.w * s / 2, y0 = groundY - (a.base ? a.base : a.h) * s;
  for (int r = 0; r < a.h; r++) {
    const uint8_t *row = fr + r * a.w;
    forRuns(a.w, [&](int c) { return indexedKey(row[c], sil); },
            [&](int c0, int len, int k) { g->fillRect(x0 + c0 * s, y0 + r * s, len * s, s, sil ? silColor : m.pal[k]); });
  }
  return y0;
}
