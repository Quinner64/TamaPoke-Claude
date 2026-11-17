// Claude dashboard screens. Everything shown comes pre-formatted in a ClaudeFrame
// (claudemon.cpp); this file only lays it out. It is also compiled on the PC by
// tools/claude_preview/ against the real Arduino_GFX, which is how the preview
// images in docs/claude-monitor/ are made: they are these exact pixels.
#include <Arduino.h>
#include "Arduino_GFX_Library.h"
#include <U8g2lib.h>
#include "claudeui.h"

#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

// Dark theme: black is "off" on an AMOLED, which saves power and burn-in.
static const uint16_t C_BG = RGB(0, 0, 0);
static const uint16_t C_PANEL = RGB(0x1c, 0x1b, 0x19);
static const uint16_t C_TRACK = RGB(0x2e, 0x2b, 0x28);
static const uint16_t C_TEXT = RGB(0xf2, 0xef, 0xe1);
static const uint16_t C_DIM = RGB(0x9a, 0x95, 0x8a);
static const uint16_t C_FAINT = RGB(0x5c, 0x58, 0x52);
static const uint16_t C_OK = RGB(0x58, 0xb8, 0x68);
static const uint16_t C_WARN = RGB(0xe8, 0xa2, 0x3c);
static const uint16_t C_BAD = RGB(0xe8, 0x50, 0x3a);
static const uint16_t C_BLUE = RGB(0x5b, 0x9b, 0xf0);
static const uint16_t C_WEEK = RGB(0xb0, 0x8c, 0xf0);  // second ring on the limits page

static const uint8_t *const F_HUGE = u8g2_font_fub49_tn;  // digits only
static const uint8_t *const F_BIG = u8g2_font_fub30_tr;
static const uint8_t *const F_MID = u8g2_font_fub20_tr;
static const uint8_t *const F_SUB = u8g2_font_fub14_tr;
static const uint8_t *const F_TXT = u8g2_font_fur14_tr;
static const uint8_t *const F_SMALL = u8g2_font_fur11_tr;
static const uint8_t *const F_LABEL = u8g2_font_fub11_tr;

// 270 degree gauges, open at the bottom: 135 deg (lower left) clockwise to 405 deg
#define GAUGE_START 135.0f
#define GAUGE_SWEEP 270.0f

// --- small drawing helpers -------------------------------------------------

static int16_t textWidth(Arduino_GFX *g, const uint8_t *font, const char *s) {
  int16_t x1, y1;
  uint16_t w, h;
  g->setFont(font);
  g->getTextBounds(s, 0, 100, &x1, &y1, &w, &h);
  return (int16_t)w;
}

// y = baseline
static void text(Arduino_GFX *g, const uint8_t *font, const char *s, int x, int y, uint16_t col) {
  g->setFont(font);
  g->setTextColor(col);
  g->setCursor(x, y);
  g->print(s);
}

static void textC(Arduino_GFX *g, const uint8_t *font, const char *s, int cx, int y, uint16_t col) {
  text(g, font, s, cx - textWidth(g, font, s) / 2, y, col);
}

// trims `s` with "..." until it fits in maxW px
static void fitText(Arduino_GFX *g, const uint8_t *font, const char *s, int maxW, char *out, size_t n) {
  snprintf(out, n, "%s", s);
  size_t len = strlen(out);
  if (textWidth(g, font, out) <= maxW) return;
  while (len > 1) {
    len--;
    while (len > 0 && out[len - 1] == ' ') len--;
    if (len + 4 > n) continue;
    memcpy(out + len, "...", 4);
    if (textWidth(g, font, out) <= maxW) return;
  }
}

// "A  .  B" centred as one group, with a small dot between the two parts
static void pairC(Arduino_GFX *g, const uint8_t *font, const char *a, const char *b, int cx, int y,
                  uint16_t col) {
  if (!b || !b[0]) { textC(g, font, a, cx, y, col); return; }
  int wa = textWidth(g, font, a), wb = textWidth(g, font, b), gap = 18;
  int x = cx - (wa + gap + wb) / 2;
  text(g, font, a, x, y, col);
  g->fillCircle(x + wa + gap / 2, y - 5, 2, C_FAINT);
  text(g, font, b, x + wa + gap, y, col);
}

static uint16_t levelColor(int pct, int warn, int bad) {
  return pct >= bad ? C_BAD : pct >= warn ? C_WARN : C_OK;
}

static void gauge(Arduino_GFX *g, int cx, int cy, int rOut, int rIn, int pct, uint16_t col) {
  g->fillArc(cx, cy, rOut, rIn, GAUGE_START, GAUGE_START + GAUGE_SWEEP, C_TRACK);
  if (pct < 0) return;
  if (pct > 100) pct = 100;
  float end = GAUGE_START + GAUGE_SWEEP * pct / 100.0f;
  int capR = (rOut - rIn) / 2, mid = (rOut + rIn) / 2;
  // rounded start cap, the arc, rounded end cap
  g->fillCircle(cx + (int)lroundf(mid * cosf(GAUGE_START * DEGTORAD)),
                cy + (int)lroundf(mid * sinf(GAUGE_START * DEGTORAD)), capR, col);
  if (pct > 0) {
    g->fillArc(cx, cy, rOut, rIn, GAUGE_START, end, col);
    g->fillCircle(cx + (int)lroundf(mid * cosf(end * DEGTORAD)), cy + (int)lroundf(mid * sinf(end * DEGTORAD)),
                  capR, col);
  }
}

static uint16_t statusColor(uint8_t st) {
  switch (st) {
    case CS_WORKING: return C_BLUE;
    case CS_DONE: return C_OK;
    case CS_NEEDS_YOU: return C_BAD;
    case CS_ERROR: return C_BAD;
    default: return C_DIM;
  }
}

// status pill + page dots, in the opening at the bottom of the gauges
static void footer(Arduino_GFX *g, const ClaudeFrame &f, int cx, int cy) {
  const char *s = claudeStatusText(f.status);
  uint16_t col = statusColor(f.status);
  bool loud = f.status == CS_NEEDS_YOU || f.status == CS_ERROR;
  int tw = textWidth(g, F_LABEL, s);
  int w = tw + 46, h = 30, x = cx - w / 2, y = cy + 160;
  g->fillRoundRect(x, y, w, h, h / 2, loud ? col : C_PANEL);
  g->fillCircle(x + 18, y + h / 2, 5, loud ? C_BG : col);
  text(g, F_LABEL, s, x + 30, y + 20, loud ? C_BG : col);
  for (int i = 0; i < CLAUDE_PAGES; i++) {
    int dx = cx + (i - (CLAUDE_PAGES - 1) / 2) * 16;
    if (i == f.page) g->fillCircle(dx, cy + 210, 4, C_TEXT);
    else g->fillCircle(dx, cy + 210, 3, C_FAINT);
  }
}

// Gallery thumbnail (w, h, palCount, pal[], idx[]; 0xFF = transparent), cropped to
// the opaque pixels and scaled so the pet is about `px` tall, centred on (cx, cy)
static void drawThumb(Arduino_GFX *g, const uint8_t *b, int cx, int cy, int px) {
  uint8_t w = b[0], h = b[1], n = b[2];
  const uint8_t *pal = b + 3, *d = pal + n * 2;
  int x0 = w, x1 = -1, y0 = h, y1 = -1;
  for (int r = 0; r < h; r++)
    for (int c = 0; c < w; c++)
      if (d[r * w + c] != 0xFF) {
        if (c < x0) x0 = c;
        if (c > x1) x1 = c;
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
    int c = x0;
    while (c <= x1) {
      uint8_t k = row[c];
      int c0 = c;
      while (c <= x1 && row[c] == k) c++;
      if (k != 0xFF && k < n)
        g->fillRect(ox + (c0 - x0) * s, oy + (r - y0) * s, (c - c0) * s, s,
                    (uint16_t)(pal[k * 2] | (pal[k * 2 + 1] << 8)));
    }
  }
}

// --- pages -----------------------------------------------------------------

static void pageContext(Arduino_GFX *g, const ClaudeFrame &f, int cx, int cy) {
  uint16_t col = f.ctxPct >= 0 ? levelColor(f.ctxPct, 60, 80) : C_DIM;
  gauge(g, cx, cy, 214, 190, f.ctxPct, col);

  textC(g, F_LABEL, "CONTEXT WINDOW", cx, cy - 92, C_DIM);
  // big percentage: digits from the numeric font, a smaller % sign after them
  int wd = textWidth(g, F_HUGE, f.ctxBig);
  bool known = f.ctxPct >= 0;
  int wp = known ? textWidth(g, F_MID, "%") + 4 : 0;
  int x = cx - (wd + wp) / 2;
  text(g, F_HUGE, f.ctxBig, x, cy + 14, C_TEXT);
  if (known) text(g, F_MID, "%", x + wd + 4, cy + 14, C_DIM);
  textC(g, F_TXT, f.ctxUsed, cx, cy + 50, C_DIM);
  if (f.ctxLeft[0]) textC(g, F_MID, f.ctxLeft, cx, cy + 86, col);
  char model[24];
  fitText(g, F_SMALL, f.model, 150, model, sizeof(model));
  pairC(g, F_SMALL, model, f.dur, cx, cy + 122, C_DIM);
  footer(g, f, cx, cy);
}

static void limitBlock(Arduino_GFX *g, int cx, int y, const char *label, uint16_t swatch, int pct,
                       const char *reset) {
  int lw = textWidth(g, F_LABEL, label);
  g->fillCircle(cx - lw / 2 - 10, y - 5, 4, swatch);
  text(g, F_LABEL, label, cx - lw / 2 + 2, y, C_DIM);
  char v[8];
  if (pct >= 0) snprintf(v, sizeof(v), "%d%%", pct);
  else snprintf(v, sizeof(v), "--");
  // neutral until the window gets tight; the ring keeps its own colour either way
  uint16_t col = pct < 0 ? C_FAINT : pct >= 90 ? C_BAD : pct >= 70 ? C_WARN : C_TEXT;
  textC(g, F_BIG, v, cx, y + 38, col);
  textC(g, F_SMALL, reset[0] ? reset : (pct >= 0 ? "" : "not reported"), cx, y + 60, C_DIM);
}

static void pageLimits(Arduino_GFX *g, const ClaudeFrame &f, int cx, int cy) {
  bool any = f.h5Pct >= 0 || f.w7Pct >= 0;
  gauge(g, cx, cy, 214, 194, f.h5Pct, C_BLUE);
  gauge(g, cx, cy, 184, 164, f.w7Pct, C_WEEK);
  if (any) {
    limitBlock(g, cx, cy - 78, "5-HOUR LIMIT", C_BLUE, f.h5Pct, f.h5Reset);
    limitBlock(g, cx, cy + 22, "WEEKLY LIMIT", C_WEEK, f.w7Pct, f.w7Reset);
    if (f.spend[0]) textC(g, F_SMALL, f.spend, cx, cy + 116, C_DIM);
  } else if (f.spPct >= 0) {
    limitBlock(g, cx, cy - 40, "SPEND LIMIT", C_BLUE, f.spPct, "");
    textC(g, F_SMALL, f.spend, cx, cy + 46, C_DIM);
  } else {
    textC(g, F_LABEL, "PLAN USAGE", cx, cy - 70, C_DIM);
    textC(g, F_TXT, "No plan limits reported", cx, cy - 30, C_TEXT);
    textC(g, F_SMALL, "(API key billing, or no reply yet)", cx, cy - 6, C_DIM);
    textC(g, F_LABEL, "SESSION COST", cx, cy + 42, C_DIM);
    textC(g, F_BIG, f.cost, cx, cy + 86, C_TEXT);
  }
  footer(g, f, cx, cy);
}

static void tile(Arduino_GFX *g, int x, int y, const char *label, const char *value, uint16_t col) {
  g->fillRoundRect(x, y, 150, 74, 14, C_PANEL);
  textC(g, F_LABEL, label, x + 75, y + 24, C_DIM);
  char v[20];
  fitText(g, F_MID, value, 138, v, sizeof(v));
  textC(g, F_MID, v, x + 75, y + 58, col);
}

// "+420 -88": added in green, removed in red
static void linesTile(Arduino_GFX *g, int x, int y, const char *lines) {
  const char *sp = strchr(lines, ' ');
  if (!sp) { tile(g, x, y, "LINES", lines, C_TEXT); return; }
  g->fillRoundRect(x, y, 150, 74, 14, C_PANEL);
  textC(g, F_LABEL, "LINES", x + 75, y + 24, C_DIM);
  char add[12];
  snprintf(add, sizeof(add), "%.*s", (int)(sp - lines), lines);
  const uint8_t *font = F_MID;
  int wa = textWidth(g, font, add), wd = textWidth(g, font, sp + 1), gap = 10;
  if (wa + gap + wd > 138) { font = F_SUB; wa = textWidth(g, font, add); wd = textWidth(g, font, sp + 1); }
  int tx = x + 75 - (wa + gap + wd) / 2;
  text(g, font, add, tx, y + 58, C_OK);
  text(g, font, sp + 1, tx + wa + gap, y + 58, C_BAD);
}

static void pageSession(Arduino_GFX *g, const ClaudeFrame &f, int cx, int cy) {
  char t[32];
  fitText(g, F_SUB, f.title, 300, t, sizeof(t));
  textC(g, F_SUB, t, cx, cy - 136, C_TEXT);
  char model[24], eff[20] = "";
  fitText(g, F_SMALL, f.model, 170, model, sizeof(model));
  if (f.effort[0]) snprintf(eff, sizeof(eff), "%s effort", f.effort);
  pairC(g, F_SMALL, model, eff, cx, cy - 110, C_DIM);

  tile(g, cx - 156, cy - 86, "COST", f.cost, C_TEXT);
  tile(g, cx + 6, cy - 86, "SESSION", f.dur, C_TEXT);
  tile(g, cx - 156, cy, "API TIME", f.api, C_TEXT);
  linesTile(g, cx + 6, cy, f.lines);

  if (f.sessions[0]) textC(g, F_SMALL, f.sessions, cx, cy + 102, C_DIM);
  if (f.petMsg[0]) {
    char m[40];
    fitText(g, F_SMALL, f.petMsg, 300, m, sizeof(m));
    textC(g, F_SMALL, m, cx, cy + (f.sessions[0] ? 126 : 108), C_WARN);
  }
  footer(g, f, cx, cy);
}

static void pageWait(Arduino_GFX *g, const ClaudeFrame &f, int cx, int cy) {
  g->drawArc(cx, cy, 214, 213, GAUGE_START, GAUGE_START + GAUGE_SWEEP, C_TRACK);
  if (f.petThumb) drawThumb(g, f.petThumb, cx, cy - 66, 96);
  textC(g, F_SUB, "Claude monitor", cx, cy + 22, C_TEXT);
  textC(g, F_TXT, f.wait1, cx, cy + 56, C_DIM);
  textC(g, F_SMALL, f.wait2, cx, cy + 82, C_FAINT);
  textC(g, F_SMALL, "swipe up for your pet", cx, cy + 150, C_FAINT);
}

void claudeDraw(Arduino_GFX *g, const ClaudeFrame &f, int16_t cx0, int16_t cy0, bool roundPanel) {
  g->fillScreen(C_BG);
  g->setTextSize(1);
  int cx = cx0 + f.ox, cy = cy0 + f.oy;
  // a red rim around the whole screen while Claude waits on you: visible from across the room
  if (f.status == CS_NEEDS_YOU && f.page != CLAUDE_PAGE_WAIT) {
    if (roundPanel) g->fillArc(cx0, cy0, 232, 226, 0, 360, C_BAD);
    else g->drawRect(cx0 - 232, cy0 - 232, 464, 464, C_BAD);
  }
  switch (f.page) {
    case 0: pageContext(g, f, cx, cy); break;
    case 1: pageLimits(g, f, cx, cy); break;
    case 2: pageSession(g, f, cx, cy); break;
    default: pageWait(g, f, cx, cy); break;
  }
}
