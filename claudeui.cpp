// Claude dashboard screens, drawn in the game's own look: the pet stands in its
// biome and reacts to what Claude is doing, the context window reads like an HP
// bar, the plan limits are a stat card and the session is a trainer-style card.
// Everything shown comes pre-formatted in a ClaudeFrame (claudemon.cpp); this
// file only lays it out. tools/claude_preview/ compiles it on a PC against the
// real Arduino_GFX to make the images in docs/claude-monitor/.
#include <Arduino.h>
#include "Arduino_GFX_Library.h"
#include "claudeui.h"
#include "scene.h"
#include "pmddraw.h"

// the game's palette (UI_* in species.h, which is not included here: it carries every fallback sprite)
static const uint16_t CC_BG_DAY = 0xF77C;     // UI_BG_DAY   #f2efe1
static const uint16_t CC_BG_NIGHT = 0x10C5;   // UI_BG_NIGHT #141828
static const uint16_t CC_INK = 0x2946;        // UI_INK      #2a2a36
static const uint16_t CC_INK_NIGHT = 0xDEFE;  // UI_INK_NIGHT
static const uint16_t CC_TRACK = 0xDE97;      // UI_TRACK    #d8d2bd
static const uint16_t CC_OK = 0x5DCD;         // UI_BAR_OK   #58b868
static const uint16_t CC_WARN = 0xED07;       // UI_BAR_WARN #e8a23c
static const uint16_t CC_BAD = 0xEA87;        // UI_BAR_BAD  #e8503a
static const uint16_t CC_WHITE = 0xFFFF;
static const uint16_t CC_INK_K = 0x18C4;      // sprite outline ink
static const uint16_t CC_DIM = C565(0x8a, 0x84, 0x76);       // secondary text on cream
static const uint16_t CC_DIM_NIGHT = C565(0x8a, 0x92, 0xb0);  // secondary text on the night panel
static const uint16_t CC_BLUE = C565(0x4f, 0x93, 0xc4);       // "working"

namespace {
// layout coordinates are those of the 466 px design (centre 233, 233)
struct Ctx {
  Arduino_GFX *g;
  int ox, oy;  // design -> screen offset (centre of this panel + anti burn-in orbit)
  int X(int v) const { return v + ox; }
  int Y(int v) const { return v + oy; }
};
}  // namespace

// --- text: the classic 5x7 font, 6 px per character at size 1 -------------

static int tw(const char *s, int size) { return (int)strlen(s) * 6 * size; }

static void txt(Arduino_GFX *g, const char *s, int x, int y, int size, uint16_t col) {
  g->setTextSize(size);
  g->setTextColor(col);
  g->setCursor(x, y);
  g->print(s);
}

static void txtC(Arduino_GFX *g, const char *s, int cx, int y, int size, uint16_t col) {
  txt(g, s, cx - tw(s, size) / 2, y, size, col);
}

// cuts `s` to `maxChars`, ending in ".." when it had to
static void fit(const char *s, int maxChars, char *out, size_t n) {
  snprintf(out, n, "%s", s);
  if ((int)strlen(out) > maxChars && maxChars >= 3 && (size_t)maxChars < n) {
    out[maxChars - 2] = '.';
    out[maxChars - 1] = '.';
    out[maxChars] = 0;
  }
}

// --- pieces ----------------------------------------------------------------

// a stat bar like the game's needs bars (rounded track, coloured fill)
static void bar(Arduino_GFX *g, int x, int y, int w, int h, int pct, uint16_t fill) {
  g->fillRoundRect(x, y, w, h, 4, CC_TRACK);
  if (pct < 0) return;
  if (pct > 100) pct = 100;
  int fw = (w - 4) * pct / 100;
  if (fw > 0) g->fillRoundRect(x + 2, y + 2, fw, h - 4, 3, fill);
}

// the game's text box: white, a double ink border, and the "more" arrow
static void dialog(Arduino_GFX *g, int x, int y, int w, int h, const char *l1, const char *l2, uint16_t col,
                   uint16_t arrow) {
  g->fillRoundRect(x, y, w, h, 8, CC_WHITE);
  g->drawRoundRect(x, y, w, h, 8, CC_INK);
  g->drawRoundRect(x + 1, y + 1, w - 2, h - 2, 7, CC_INK);
  g->drawRoundRect(x + 4, y + 4, w - 8, h - 8, 5, CC_TRACK);
  int inner = w - 26;
  bool two = l2 && l2[0];
  int s1 = tw(l1, 2) <= inner ? 2 : 1;
  int y1 = two ? y + 9 : y + (h - 8 * s1) / 2;
  txt(g, l1, x + 12, y1, s1, col);
  if (two) {
    int s2 = tw(l2, 2) <= inner ? 2 : 1;
    txt(g, l2, x + 12, y + 27 + (2 - s2) * 4, s2, col);
  }
  if (arrow) g->fillTriangle(x + w - 21, y + h - 15, x + w - 11, y + h - 15, x + w - 16, y + h - 9, arrow);
}

// page markers: a little pokeball per page, the current one in colour
static void pokeball(Arduino_GFX *g, int x, int y, int r, bool on) {
  uint16_t top = on ? CC_BAD : C565(0xb8, 0xb2, 0xa4), bottom = on ? CC_WHITE : C565(0xe6, 0xe1, 0xd4);
  g->fillCircle(x, y, r, CC_INK);
  g->fillCircle(x, y, r - 1, bottom);
  g->fillArc(x, y, r - 1, 1, 180, 360, top);
  g->fillRect(x - r, y - 1, 2 * r + 1, 2, CC_INK);
  g->fillCircle(x, y, r / 3 + 1, CC_INK);
  g->fillCircle(x, y, r / 3, CC_WHITE);
}

static void pageDots(const Ctx &c, int page) {
  for (int i = 0; i < CLAUDE_PAGES; i++)
    pokeball(c.g, c.X(233 + (i - 1) * 24), c.Y(440), i == page ? 7 : 6, i == page);
}

static uint16_t statusInk(uint8_t st) {
  return st == CS_NEEDS_YOU || st == CS_ERROR ? CC_BAD : CC_INK;
}

static uint16_t statusArrow(uint8_t st) {
  switch (st) {
    case CS_WORKING: return CC_BLUE;
    case CS_DONE: return CC_OK;
    case CS_NEEDS_YOU: return CC_BAD;
    case CS_ERROR: return CC_BAD;
    default: return CC_DIM;
  }
}

// the status text box every page ends with
static void statusBox(const Ctx &c, const ClaudeFrame &f, bool blink) {
  const char *l1, *l2;
  claudeStatusLines(f.status, &l1, &l2);
  dialog(c.g, c.X(95), c.Y(372), 276, 48, l1, l2, statusInk(f.status), blink ? 0 : statusArrow(f.status));
}

// gallery thumbnail (w, h, palCount, pal[], idx[]; 0xFF = transparent), cropped to
// its opaque pixels and scaled so the pet is about `px` tall, centred on (cx, cy)
static void drawThumb(Arduino_GFX *g, const uint8_t *b, int cx, int cy, int px) {
  uint8_t w = b[0], h = b[1], n = b[2];
  const uint8_t *pal = b + 3, *d = pal + n * 2;
  int x0 = w, x1 = -1, y0 = h, y1 = -1;
  for (int r = 0; r < h; r++)
    for (int col = 0; col < w; col++)
      if (d[r * w + col] != 0xFF) {
        if (col < x0) x0 = col;
        if (col > x1) x1 = col;
        if (r < y0) y0 = r;
        if (r > y1) y1 = r;
      }
  if (x1 < 0) return;
  int big = (x1 - x0 > y1 - y0 ? x1 - x0 : y1 - y0) + 1;
  int s = px / big;
  if (s < 1) s = 1;
  if (s > 5) s = 5;
  int ox = cx - (x1 - x0 + 1) * s / 2, oy = cy - (y1 - y0 + 1) * s / 2;
  for (int r = y0; r <= y1; r++) {
    const uint8_t *row = d + r * w;
    int col = x0;
    while (col <= x1) {
      uint8_t k = row[col];
      int c0 = col;
      while (col <= x1 && row[col] == k) col++;
      if (k != 0xFF && k < n)
        g->fillRect(ox + (c0 - x0) * s, oy + (r - y0) * s, (col - c0) * s, s,
                    (uint16_t)(pal[k * 2] | (pal[k * 2 + 1] << 8)));
    }
  }
}

// The pet on its field, acting out Claude's status. Returns the top of the sprite.
//   working: paces back and forth      your turn: poses now and then
//   needs you: nods at you, with "!"   error: hurt     idle / waiting: sleeps
static int fieldPet(Arduino_GFX *g, const ClaudeScene &sc, uint8_t st, uint32_t now, int x, int ground) {
  if (sc.pet && sc.pet->loaded) {
    const PmdMon &m = *sc.pet;
    const PmdAct &idle = m.acts[PMD_IDLE];
    int tall = idle.base ? idle.base : (idle.h ? idle.h : 40);
    int maxS = 140 / tall;  // about 140 px of sky between the numbers and the ground
    if (maxS < 1) maxS = 1;
    if (maxS > 3) maxS = 3;
    uint8_t act = PMD_IDLE;
    uint32_t t = now;
    if (sc.asleep || st == CS_IDLE) act = PMD_SLEEP;
    else if (st == CS_WORKING && m.has(PMD_WALKL) && m.has(PMD_WALKR)) {
      // 105 px each way at the game's walking speed (30 px/s)
      const uint32_t lap = 7000;
      uint32_t ph = now % lap;
      bool right = ph < lap / 2;
      x += (int)((right ? ph : lap - ph) * 105 / (lap / 2)) - 52;
      act = right ? PMD_WALKR : PMD_WALKL;
    }
    else if (st == CS_NEEDS_YOU) act = PMD_NOD;
    else if (st == CS_ERROR) act = PMD_HURT;
    else if (st == CS_DONE && m.has(PMD_POSE)) {
      uint32_t ph = now % 3000, pose = pmdActTotalMs(m.acts[PMD_POSE]);
      if (ph < pose) {  // a pose every 3 s, idle in between
        act = PMD_POSE;
        t = ph;
      }
    }
    if (!m.has(act)) act = PMD_IDLE;
    return pmdDraw(g, m, act, x, ground, t, true, false, (uint8_t)maxS, CC_INK_K);
  }
  if (sc.thumb) {
    drawThumb(g, sc.thumb, x, ground - 44, 88);
    return ground - 88;
  }
  return ground;
}

// --- pages -----------------------------------------------------------------

#define CC_GROUND 266  // the pet's feet on the field pages
#define CC_PANEL 276   // top of the cream panel under the field

static void fieldBase(const Ctx &c, const ClaudeScene &sc, uint32_t now, int W, int H, const char *title) {
  sceneDraw(c.g, W, H, sc.biome, now, sc.night, sc.hour);
  txtC(c.g, title, c.X(233), c.Y(30), 2, sc.night ? CC_INK_NIGHT : CC_INK);
}

static void panel(const Ctx &c, const ClaudeScene &sc, int W, int H) {
  c.g->fillRect(0, c.Y(CC_PANEL), W, H - c.Y(CC_PANEL), sc.night ? CC_BG_NIGHT : CC_BG_DAY);
}

// page 0: context window as the pet's HP
static void pageField(const Ctx &c, const ClaudeFrame &f, const ClaudeScene &sc, uint32_t now, int W, int H) {
  Arduino_GFX *g = c.g;
  uint16_t sky = sc.night ? CC_INK_NIGHT : CC_INK;
  fieldBase(c, sc, now, W, H, "CONTEXT");
  char big[8];
  snprintf(big, sizeof(big), f.ctxPct >= 0 ? "%s%%" : "%s", f.ctxBig);
  txtC(g, big, c.X(233), c.Y(52), 6, sky);

  int top = fieldPet(g, sc, f.status, now, c.X(233), c.Y(CC_GROUND));
  if (top < c.Y(118)) top = c.Y(118);
  if (f.status == CS_NEEDS_YOU && !sc.asleep) {  // speech bubble with "!"
    int bx = c.X(268), by = top - 4;
    g->fillRoundRect(bx, by, 30, 36, 8, CC_WHITE);
    g->drawRoundRect(bx, by, 30, 36, 8, CC_INK);
    g->fillTriangle(bx + 4, by + 30, bx + 12, by + 35, bx - 4, by + 44, CC_WHITE);
    txt(g, "!", bx + 8, by + 7, 3, CC_BAD);
  } else if (sc.asleep || f.status == CS_IDLE) {
    txt(g, "Zz", c.X(272), top + 6, 3, sky);
  }

  panel(c, sc, W, H);
  uint16_t ink = sc.night ? CC_INK_NIGHT : CC_INK, dim = sc.night ? CC_DIM_NIGHT : CC_DIM;
  // HP = what is left of the window: green above half, yellow, red under a fifth
  int hp = f.ctxPct >= 0 ? 100 - f.ctxPct : -1;
  txt(g, "HP", c.X(80), c.Y(288), 2, ink);
  bar(g, c.X(112), c.Y(288), 274, 16, hp, hp > 50 ? CC_OK : hp > 20 ? CC_WARN : CC_BAD);
  txtC(g, f.ctxHp[0] ? f.ctxHp : f.ctxUsed, c.X(233), c.Y(314), f.ctxHp[0] ? 3 : 2, ink);
  char line[40], model[16];
  fit(f.model, 12, model, sizeof(model));
  snprintf(line, sizeof(line), "%s  %s", model, f.dur);
  txtC(g, line, c.X(233), c.Y(346), 2, dim);
  statusBox(c, f, (now / 600) % 2);
  pageDots(c, 0);
}

static void cardBase(const Ctx &c, int cx0, int cy0, bool roundPanel, const char *title) {
  c.g->fillScreen(0x0000);
  if (roundPanel) c.g->fillCircle(cx0, cy0, 231, CC_BG_DAY);
  else c.g->fillRect(0, 0, 2 * cx0, 2 * cy0, CC_BG_DAY);
  if (title[0]) txtC(c.g, title, c.X(233), c.Y(40), 3, CC_INK);
}

static void drawThumb(Arduino_GFX *g, const uint8_t *b, int cx, int cy, int px);

static void limitBlock(const Ctx &c, int y, const char *label, int pct, const char *reset) {
  Arduino_GFX *g = c.g;
  txt(g, label, c.X(86), c.Y(y + 6), 2, CC_INK);
  char v[8];
  if (pct >= 0) snprintf(v, sizeof(v), "%d%%", pct);
  else snprintf(v, sizeof(v), "--");
  uint16_t col = pct < 0 ? CC_DIM : pct >= 90 ? CC_BAD : pct >= 70 ? CC_WARN : CC_INK;
  txt(g, v, c.X(380) - tw(v, 3), c.Y(y), 3, col);
  bar(g, c.X(86), c.Y(y + 30), 294, 16, pct, pct >= 90 ? CC_BAD : pct >= 70 ? CC_WARN : CC_OK);
  txt(g, reset[0] ? reset : (pct >= 0 ? "" : "not reported"), c.X(86), c.Y(y + 54), 2, CC_DIM);
}

// page 1: plan limits as a stat card
static void pageLimits(const Ctx &c, const ClaudeFrame &f, int cx0, int cy0, bool roundPanel) {
  Arduino_GFX *g = c.g;
  cardBase(c, cx0, cy0, roundPanel, "PLAN LIMITS");
  if (f.h5Pct >= 0 || f.w7Pct >= 0) {
    limitBlock(c, 92, "5 HOURS", f.h5Pct, f.h5Reset);
    limitBlock(c, 178, "WEEKLY", f.w7Pct, f.w7Reset);
    if (f.spend[0]) txtC(g, f.spend, c.X(233), c.Y(270), 2, CC_DIM);
    else if (f.petThumb) drawThumb(g, f.petThumb, c.X(233), c.Y(318), 72);  // the pet keeps an eye on it
  } else if (f.spPct >= 0) {
    limitBlock(c, 120, "SPEND", f.spPct, f.spend);
  } else {
    txtC(g, "No plan limits", c.X(233), c.Y(116), 2, CC_INK);
    txtC(g, "reported (API key).", c.X(233), c.Y(138), 2, CC_DIM);
    txtC(g, "SESSION COST", c.X(233), c.Y(196), 2, CC_DIM);
    txtC(g, f.cost, c.X(233), c.Y(222), 4, CC_INK);
  }
  statusBox(c, f, false);
  pageDots(c, 1);
}

static void cardRow(const Ctx &c, int y, const char *label, const char *value, uint16_t col, int maxChars) {
  char v[32];
  fit(value, maxChars, v, sizeof(v));
  txt(c.g, label, c.X(86), c.Y(y), 2, CC_DIM);
  txt(c.g, v, c.X(166), c.Y(y), 2, col);
}

// page 2: the session as a trainer-style card
static void pageSession(const Ctx &c, const ClaudeFrame &f, int cx0, int cy0, bool roundPanel) {
  Arduino_GFX *g = c.g;
  cardBase(c, cx0, cy0, roundPanel, "");
  int x = c.X(66), y = c.Y(46), w = 334, h = 304;
  g->fillRoundRect(x, y, w, h, 14, CC_WHITE);
  g->fillRoundRect(x, y, w, 34, 14, CC_BAD);  // red band across the top
  g->fillRect(x, y + 20, w, 14, CC_BAD);
  g->drawRoundRect(x, y, w, h, 14, CC_INK);
  g->drawRoundRect(x + 1, y + 1, w - 2, h - 2, 13, CC_INK);
  pokeball(g, x + 24, y + 17, 9, true);
  txtC(g, "SESSION CARD", c.X(233) + 8, y + 10, 2, CC_WHITE);

  char model[24];
  snprintf(model, sizeof(model), "%s%s%s", f.model, f.effort[0] ? " " : "", f.effort);
  cardRow(c, 94, "NAME", f.title, CC_INK, 18);
  cardRow(c, 120, "MODEL", model, CC_INK, 18);
  cardRow(c, 154, "MONEY", f.cost, CC_INK, 9);
  cardRow(c, 180, "TIME", f.dur, CC_INK, 9);
  cardRow(c, 206, "API", f.api, CC_INK, 9);
  // lines: added in green, removed in red
  txt(g, "LINES", c.X(86), c.Y(232), 2, CC_DIM);
  const char *sp = strchr(f.lines, ' ');
  if (sp) {
    char add[12];
    snprintf(add, sizeof(add), "%.*s", (int)(sp - f.lines), f.lines);
    txt(g, add, c.X(166), c.Y(232), 2, CC_OK);
    txt(g, sp + 1, c.X(166) + tw(add, 2) + 10, c.Y(232), 2, CC_BAD);
  } else {
    txt(g, f.lines, c.X(166), c.Y(232), 2, CC_INK);
  }
  char open[12];
  snprintf(open, sizeof(open), "%d", f.nSessions > 0 ? f.nSessions : 1);
  cardRow(c, 258, "OPEN", open, CC_INK, 9);
  if (f.petThumb) drawThumb(g, f.petThumb, c.X(334), c.Y(214), 84);
  if (f.petMsg[0]) {
    int size = tw(f.petMsg, 2) <= 300 ? 2 : 1;
    txtC(g, f.petMsg, c.X(233), c.Y(size == 2 ? 296 : 300), size, CC_WARN);
  }
  statusBox(c, f, false);
  pageDots(c, 2);
}

// no PC: the pet naps on its field until the bridge shows up
static void pageWait(const Ctx &c, const ClaudeFrame &f, const ClaudeScene &sc, uint32_t now, int W, int H) {
  Arduino_GFX *g = c.g;
  fieldBase(c, sc, now, W, H, "CLAUDE MONITOR");
  ClaudeScene nap = sc;
  nap.asleep = true;
  int top = fieldPet(g, nap, CS_IDLE, now, c.X(233), c.Y(CC_GROUND));
  if (top < c.Y(118)) top = c.Y(118);
  txt(g, "Zz", c.X(272), top + 6, 3, sc.night ? CC_INK_NIGHT : CC_INK);
  panel(c, sc, W, H);
  dialog(g, c.X(81), c.Y(296), 304, 50, f.wait1, f.wait2, CC_INK, (now / 600) % 2 ? 0 : CC_DIM);
  txtC(g, "Swipe up: your pet", c.X(233), c.Y(370), 2, sc.night ? CC_DIM_NIGHT : CC_DIM);
}

void claudeDraw(Arduino_GFX *g, const ClaudeFrame &f, const ClaudeScene &sc, uint32_t nowMs, int16_t cx0,
                int16_t cy0, bool roundPanel) {
  g->setFont();  // the game's 5x7 font, whatever the language font is
#if defined(U8G2_FONT_SUPPORT)
  g->setUTF8Print(false);
#endif
  Ctx c = { g, cx0 - 233 + f.ox, cy0 - 233 + f.oy };
  int W = 2 * cx0, H = 2 * cy0;
  switch (f.page) {
    case 0: pageField(c, f, sc, nowMs, W, H); break;
    case 1: pageLimits(c, f, cx0, cy0, roundPanel); break;
    case 2: pageSession(c, f, cx0, cy0, roundPanel); break;
    default: pageWait(c, f, sc, nowMs, W, H); break;
  }
  // a red rim around the whole screen while Claude waits on you: visible from across the room
  if (f.status == CS_NEEDS_YOU && f.page != CLAUDE_PAGE_WAIT) {
    if (roundPanel) g->fillArc(cx0, cy0, 232, 226, 0, 360, CC_BAD);
    else
      for (int i = 0; i < 6; i++) g->drawRect(i, i, W - 2 * i, H - 2 * i, CC_BAD);
  }
}
