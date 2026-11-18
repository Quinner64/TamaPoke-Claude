// Claude dashboard screens, laid out to be read from across a desk: a dark
// screen tinted by what Claude is doing, one big status word, and only a few
// numbers per page, drawn large. Page 0 wraps the screen in a ring that fills as
// the context window is used, with the percentage in the middle and your pet
// acting out the status below it. Everything shown comes pre-formatted in a
// ClaudeFrame (claudemon.cpp); this file only lays it out.
// tools/claude_preview/ compiles it on a PC against the real Arduino_GFX to make
// the images in docs/claude-monitor/.
#include <Arduino.h>
#include "Arduino_GFX_Library.h"
#include "claudeui.h"
#include "scene.h"
#include "pmddraw.h"

// the game's palette (UI_* in species.h, which is not included here: it carries every fallback sprite)
static const uint16_t CC_INK = 0x2946;    // UI_INK      #2a2a36
static const uint16_t CC_BAD = 0xEA87;    // UI_BAR_BAD  #e8503a
static const uint16_t CC_WHITE = 0xFFFF;
static const uint16_t CC_INK_K = 0x18C4;  // sprite outline ink

// the dashboard's own colours: bright on dark, so they carry across a room
static const uint16_t CD_TEXT = 0xFFFF;
static const uint16_t CD_DIM = C565(0xa0, 0xa6, 0xb8);
static const uint16_t CD_TRACK = C565(0x34, 0x38, 0x46);
static const uint16_t CD_GREEN = C565(0x5c, 0xe0, 0x70);
static const uint16_t CD_YELLOW = C565(0xff, 0xc8, 0x3c);
static const uint16_t CD_RED = C565(0xff, 0x5a, 0x48);
static const uint16_t CD_BLUE = C565(0x5a, 0xb0, 0xff);
static const uint16_t CD_ORANGE = C565(0xff, 0x96, 0x3c);
static const uint16_t CD_GREY = C565(0x9a, 0xa2, 0xc0);

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

// centred on cx (the font's trailing column is left out, so it is really centred)
static void txtC(Arduino_GFX *g, const char *s, int cx, int y, int size, uint16_t col) {
  txt(g, s, cx - (tw(s, size) - size) / 2, y, size, col);
}

// the largest size from `size` down to `minSize` at which `s` fits in `w` px
static int fitSize(const char *s, int w, int size, int minSize) {
  while (size > minSize && tw(s, size) - size > w) size--;
  return size;
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

// --- status: colour, background tint, word ---------------------------------

static uint16_t stColor(uint8_t st) {
  switch (st) {
    case CS_WORKING: return CD_BLUE;
    case CS_DONE: return CD_GREEN;
    case CS_NEEDS_YOU: return CD_RED;
    case CS_ERROR: return CD_ORANGE;
    default: return CD_GREY;
  }
}

// the whole screen is tinted by the status: the colour reads from far away
// even when the words don't. NEEDS YOU flashes brighter on the animated pages.
static uint16_t stBg(uint8_t st, bool flash) {
  switch (st) {
    case CS_WORKING: return C565(0x06, 0x16, 0x30);
    case CS_DONE: return C565(0x06, 0x24, 0x12);
    case CS_NEEDS_YOU: return flash ? C565(0x86, 0x14, 0x10) : C565(0x42, 0x08, 0x08);
    case CS_ERROR: return C565(0x3a, 0x16, 0x04);
    default: return C565(0x10, 0x12, 0x1a);
  }
}

// context used: green under half, yellow, red from 80 %
static uint16_t ctxColor(int pct) { return pct < 0 ? CD_DIM : pct < 50 ? CD_GREEN : pct < 80 ? CD_YELLOW : CD_RED; }

// plan limits: white while comfortable, yellow from 70 %, red from 90 %
static uint16_t limitText(int pct) { return pct < 0 ? CD_DIM : pct >= 90 ? CD_RED : pct >= 70 ? CD_YELLOW : CD_TEXT; }
static uint16_t limitFill(int pct) { return pct >= 90 ? CD_RED : pct >= 70 ? CD_YELLOW : CD_GREEN; }

// --- pieces ----------------------------------------------------------------

static void bar(Arduino_GFX *g, int x, int y, int w, int h, int pct, uint16_t fill) {
  g->fillRoundRect(x, y, w, h, h / 2, CD_TRACK);
  if (pct < 0) return;
  if (pct > 100) pct = 100;
  int fw = w * pct / 100;
  if (fw >= h) g->fillRoundRect(x, y, fw, h, h / 2, fill);
  else if (fw > 0) g->fillRect(x + 2, y + 2, fw, h - 4, fill);
}

// an arc clockwise from 12 o'clock over `pct` of the circle
static void ring(Arduino_GFX *g, int cx, int cy, int r1, int r2, int pct, uint16_t col) {
  if (pct <= 0) return;
  if (pct >= 100) {
    g->fillArc(cx, cy, r1, r2, 0, 360, col);
    return;
  }
  float end = 270.0f + 3.6f * pct;  // Arduino_GFX angles: 0 = 3 o'clock, clockwise
  if (end <= 360.0f) g->fillArc(cx, cy, r1, r2, 270, end, col);
  else {
    g->fillArc(cx, cy, r1, r2, 270, 360, col);
    g->fillArc(cx, cy, r1, r2, 0, end - 360.0f, col);
  }
}

// page markers: a little pokeball per page, the current one in colour
static void pokeball(Arduino_GFX *g, int x, int y, int r, bool on) {
  uint16_t top = on ? CC_BAD : C565(0x6a, 0x6e, 0x7c), bottom = on ? CC_WHITE : C565(0xa8, 0xac, 0xb8);
  g->fillCircle(x, y, r, CC_INK);
  g->fillCircle(x, y, r - 1, bottom);
  g->fillArc(x, y, r - 1, 1, 180, 360, top);
  g->fillRect(x - r, y - 1, 2 * r + 1, 2, CC_INK);
  g->fillCircle(x, y, r / 3 + 1, CC_INK);
  g->fillCircle(x, y, r / 3, CC_WHITE);
}

static void pageDots(const Ctx &c, int page) {
  for (int i = 0; i < CLAUDE_PAGES; i++)
    pokeball(c.g, c.X(233 + (i - 1) * 26), c.Y(398), i == page ? 8 : 6, i == page);
}

// the status word along the top of the card pages
static void cardHeader(const Ctx &c, const ClaudeFrame &f) {
  const char *w = claudeStatusText(f.status);
  txtC(c.g, w, c.X(233), c.Y(46), fitSize(w, 190, 3, 2), stColor(f.status));
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

// The pet acting out Claude's status, feet on `ground`. Returns the top of the sprite.
//   working: paces back and forth      your turn: poses now and then
//   needs you: nods at you             error: hurt     idle / waiting: sleeps
static int actingPet(Arduino_GFX *g, const ClaudeScene &sc, uint8_t st, uint32_t now, int x, int ground,
                     int maxPx, int pace) {
  if (sc.pet && sc.pet->loaded) {
    const PmdMon &m = *sc.pet;
    const PmdAct &idle = m.acts[PMD_IDLE];
    int tall = idle.base ? idle.base : (idle.h ? idle.h : 40);
    int maxS = maxPx / tall;
    if (maxS < 1) maxS = 1;
    if (maxS > 3) maxS = 3;
    uint8_t act = PMD_IDLE;
    uint32_t t = now;
    if (sc.asleep || st == CS_IDLE || st == CS_NONE) act = PMD_SLEEP;
    else if (st == CS_WORKING && pace > 0 && m.has(PMD_WALKL) && m.has(PMD_WALKR)) {
      const uint32_t lap = (uint32_t)pace * 2 * 1000 / 30;  // the game's walking speed, 30 px/s
      uint32_t ph = now % lap;
      bool right = ph < lap / 2;
      x += (int)((right ? ph : lap - ph) * (2 * pace) / (lap / 2)) - pace;
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
    drawThumb(g, sc.thumb, x, ground - maxPx / 2, maxPx);
    return ground - maxPx;
  }
  return ground;
}

// --- pages -----------------------------------------------------------------

#define CD_RING_OUT 229  // the context ring on page 0
#define CD_RING_IN 205

// page 0, the one to glance at: status, context window, 5-hour limit
static void pageGlance(const Ctx &c, const ClaudeFrame &f, const ClaudeScene &sc, uint32_t now) {
  Arduino_GFX *g = c.g;
  int cx = c.X(233);
  g->fillScreen(stBg(f.status, f.status == CS_NEEDS_YOU && (now / 500) % 2));

  g->fillArc(cx, c.Y(233), CD_RING_OUT, CD_RING_IN, 0, 360, CD_TRACK);
  ring(g, cx, c.Y(233), CD_RING_OUT, CD_RING_IN, f.ctxPct, ctxColor(f.ctxPct));

  const char *w = claudeStatusText(f.status);
  txtC(g, w, cx, c.Y(66), fitSize(w, 224, 4, 3), stColor(f.status));

  char big[8];
  snprintf(big, sizeof(big), f.ctxPct >= 0 ? "%s%%" : "%s", f.ctxBig);
  txtC(g, big, cx, c.Y(112), fitSize(big, 280, 11, 6), CD_TEXT);
  txtC(g, "CONTEXT USED", cx, c.Y(200), 2, CD_DIM);

  int ground = c.Y(318);
  int top = actingPet(g, sc, f.status, now, cx, ground, 84, 64);
  if (top < c.Y(226)) top = c.Y(226);
  if (f.status == CS_NEEDS_YOU && !sc.asleep) {  // "!" bubble
    int bx = cx + 34, by = top - 6;
    g->fillRoundRect(bx, by, 28, 34, 8, CC_WHITE);
    g->fillTriangle(bx + 4, by + 28, bx + 12, by + 33, bx - 4, by + 42, CC_WHITE);
    txt(g, "!", bx + 7, by + 6, 3, CD_RED);
  } else if (sc.asleep || f.status == CS_IDLE || f.status == CS_NONE) {
    txt(g, "Zz", cx + 34, top, 3, CD_DIM);
  }

  // the 5-hour limit is what runs out first; without plan limits, the session cost
  char line[24];
  uint16_t col = CD_TEXT;
  if (f.h5Pct >= 0) {
    snprintf(line, sizeof(line), "5H %d%%", f.h5Pct);
    col = limitText(f.h5Pct);
  } else if (f.spPct >= 0) {
    snprintf(line, sizeof(line), "SPEND %d%%", f.spPct);
    col = limitText(f.spPct);
  } else {
    snprintf(line, sizeof(line), "%s", f.cost);
  }
  txtC(g, line, cx, c.Y(332), fitSize(line, 250, 4, 3), col);
  pageDots(c, 0);
}

static void cardBase(const Ctx &c, const ClaudeFrame &f, int cx0, int cy0, bool roundPanel) {
  uint16_t bg = stBg(f.status, false);
  c.g->fillScreen(0x0000);
  if (roundPanel) c.g->fillCircle(cx0, cy0, 233, bg);
  else c.g->fillRect(0, 0, 2 * cx0, 2 * cy0, bg);
  cardHeader(c, f);
}

// one limit: label and big percentage on a line, a thick bar, the reset under it
static void limitBlock(const Ctx &c, int y, const char *label, int pct, const char *reset) {
  Arduino_GFX *g = c.g;
  char v[8];
  if (pct >= 0) snprintf(v, sizeof(v), "%d%%", pct);
  else snprintf(v, sizeof(v), "--");
  txt(g, label, c.X(92), c.Y(y + 24), 4, CD_DIM);
  txt(g, v, c.X(378) - (tw(v, 8) - 8), c.Y(y), 8, limitText(pct));
  bar(g, c.X(92), c.Y(y + 66), 286, 22, pct, limitFill(pct));
  const char *r = reset[0] ? reset : (pct >= 0 ? "" : "not reported");
  txtC(g, r, c.X(235), c.Y(y + 98), fitSize(r, 286, 3, 2), CD_DIM);
}

// page 1: plan limits
static void pageLimits(const Ctx &c, const ClaudeFrame &f, int cx0, int cy0, bool roundPanel) {
  Arduino_GFX *g = c.g;
  cardBase(c, f, cx0, cy0, roundPanel);
  if (f.h5Pct >= 0 || f.w7Pct >= 0) {
    limitBlock(c, 90, "5H", f.h5Pct, f.h5Reset);
    limitBlock(c, 230, "WEEK", f.w7Pct, f.w7Reset);
  } else if (f.spPct >= 0) {
    limitBlock(c, 150, "SPEND", f.spPct, f.spend);
  } else {  // API key: no plan limits, so the money is what counts
    txtC(g, "SESSION COST", c.X(233), c.Y(140), 3, CD_DIM);
    txtC(g, f.cost, c.X(233), c.Y(178), fitSize(f.cost, 300, 9, 5), CD_TEXT);
    txtC(g, "no plan limits (API key)", c.X(233), c.Y(270), 2, CD_DIM);
  }
  pageDots(c, 1);
}

// page 2: the session in three numbers
static void pageSession(const Ctx &c, const ClaudeFrame &f, int cx0, int cy0, bool roundPanel) {
  Arduino_GFX *g = c.g;
  cardBase(c, f, cx0, cy0, roundPanel);
  int cx = c.X(233);
  txtC(g, "COST", cx, c.Y(90), 2, CD_DIM);
  txtC(g, f.cost, cx, c.Y(110), fitSize(f.cost, 300, 8, 4), CD_TEXT);
  txtC(g, "TIME", cx, c.Y(184), 2, CD_DIM);
  txtC(g, f.dur, cx, c.Y(204), fitSize(f.dur, 300, 6, 3), CD_TEXT);

  // lines: added in green, removed in red, side by side
  const char *sp = strchr(f.lines, ' ');
  if (sp) {
    char add[12];
    snprintf(add, sizeof(add), "%.*s", (int)(sp - f.lines), f.lines);
    const char *del = sp + 1;
    int gap = 24, wa = tw(add, 4) - 4, wd = tw(del, 4) - 4, x = cx - (wa + gap + wd) / 2;
    txt(g, add, x, c.Y(266), 4, CD_GREEN);
    txt(g, del, x + wa + gap, c.Y(266), 4, CD_RED);
  } else if (f.lines[0]) {
    txtC(g, f.lines, cx, c.Y(266), 4, CD_TEXT);
  }

  char model[40], line[64];
  snprintf(model, sizeof(model), "%s%s%s", f.model, f.effort[0] ? " " : "", f.effort);
  if (f.nSessions > 1) {
    char m[sizeof(model)];
    fit(model, 16, m, sizeof(m));
    snprintf(line, sizeof(line), "%s - %d open", m, f.nSessions);
  } else {
    fit(model, 24, line, sizeof(line));
  }
  txtC(g, line, cx, c.Y(318), 2, CD_DIM);
  if (f.petMsg[0]) txtC(g, f.petMsg, cx, c.Y(344), fitSize(f.petMsg, 280, 2, 1), CD_YELLOW);
  else if (f.title[0]) {
    char t[sizeof(f.title)];
    fit(f.title, 22, t, sizeof(t));
    txtC(g, t, cx, c.Y(344), 2, CD_DIM);
  }
  pageDots(c, 2);
}

// no PC: the pet naps until the bridge shows up
static void pageWait(const Ctx &c, const ClaudeFrame &f, const ClaudeScene &sc, uint32_t now) {
  Arduino_GFX *g = c.g;
  int cx = c.X(233);
  g->fillScreen(stBg(CS_NONE, false));
  txtC(g, "CLAUDE", cx, c.Y(70), 4, CD_GREY);
  ClaudeScene nap = sc;
  nap.asleep = true;
  int top = actingPet(g, nap, CS_IDLE, now, cx, c.Y(250), 120, 0);
  if (top < c.Y(130)) top = c.Y(130);
  txt(g, "Zz", cx + 44, top, 3, CD_DIM);
  txtC(g, f.wait1, cx, c.Y(280), fitSize(f.wait1, 390, 3, 2), CD_TEXT);
  txtC(g, f.wait2, cx, c.Y(318), fitSize(f.wait2, 380, 2, 1), CD_DIM);
  txtC(g, "Swipe up: your pet", cx, c.Y(360), 2, CD_DIM);
}

void claudeDraw(Arduino_GFX *g, const ClaudeFrame &f, const ClaudeScene &sc, uint32_t nowMs, int16_t cx0,
                int16_t cy0, bool roundPanel) {
  g->setFont();  // the game's 5x7 font, whatever the language font is
#if defined(U8G2_FONT_SUPPORT)
  g->setUTF8Print(false);
#endif
  Ctx c = { g, cx0 - 233 + f.ox, cy0 - 233 + f.oy };
  switch (f.page) {
    case 0: pageGlance(c, f, sc, nowMs); break;
    case 1: pageLimits(c, f, cx0, cy0, roundPanel); break;
    case 2: pageSession(c, f, cx0, cy0, roundPanel); break;
    default: pageWait(c, f, sc, nowMs); break;
  }
  // the card pages are static, so NEEDS YOU can't flash there: a thick red rim instead
  if (f.status == CS_NEEDS_YOU && (f.page == 1 || f.page == 2)) {
    int W = 2 * cx0, H = 2 * cy0;
    if (roundPanel) g->fillArc(cx0, cy0, 233, 220, 0, 360, CD_RED);
    else
      for (int i = 0; i < 12; i++) g->drawRect(i, i, W - 2 * i, H - 2 * i, CD_RED);
  }
}
