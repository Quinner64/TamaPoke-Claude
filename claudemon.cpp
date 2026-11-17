#include "claudemon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ClaudeData::clear() {
  memset(this, 0, sizeof(*this));
  status = CS_NONE;
  ctxUsed = ctxMax = -1;
  ctxPct = -1;
  costCents = -1;
  durS = apiS = -1;
  linesAdd = linesDel = -1;
  h5Pct = w7Pct = spPct = -1;
  h5ResetS = w7ResetS = spResetS = -1;
  spUsedCents = spLimitCents = -1;
  sessions = 0;
}

static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// copies a percent-encoded value [s, e) into dst, printable ASCII only
static void decodeText(const char *s, const char *e, char *dst, size_t n) {
  size_t k = 0;
  while (s < e && k + 1 < n) {
    char c = *s++;
    if (c == '%' && e - s >= 2 && hexVal(s[0]) >= 0 && hexVal(s[1]) >= 0) {
      c = (char)(hexVal(s[0]) * 16 + hexVal(s[1]));
      s += 2;
    }
    dst[k++] = (c >= 0x20 && c <= 0x7E) ? c : '?';
  }
  dst[k] = 0;
}

static int32_t toInt(const char *s, const char *e) {
  char tmp[16];
  size_t len = (size_t)(e - s);
  if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;
  memcpy(tmp, s, len);
  tmp[len] = 0;
  char *end = nullptr;
  long v = strtol(tmp, &end, 10);
  if (end == tmp) return -1;
  if (v > 2000000000L) v = 2000000000L;
  if (v < -1) v = -1;
  return (int32_t)v;
}

static int16_t toPct(int32_t v) { return v < 0 ? -1 : (int16_t)(v > 999 ? 999 : v); }

bool claudeParse(const char *p, ClaudeData &out) {
  out.clear();
  if (!p) return false;
  int32_t version = -1;
  while (*p) {
    while (*p == ' ') p++;
    if (!*p) break;
    const char *k = p;
    while (*p && *p != '=' && *p != ' ') p++;
    const char *ke = p;
    if (*p != '=') continue;  // a bare word: ignored
    const char *v = ++p;
    while (*p && *p != ' ') p++;
    const char *ve = p;
    size_t kl = (size_t)(ke - k);
#define KEY(name) (kl == sizeof(name) - 1 && memcmp(k, name, kl) == 0)
    if (KEY("v")) version = toInt(v, ve);
    else if (KEY("st")) { int32_t s = toInt(v, ve); out.status = (s > 0 && s < CS_COUNT) ? (uint8_t)s : (uint8_t)CS_NONE; }
    else if (KEY("ctx")) out.ctxUsed = toInt(v, ve);
    else if (KEY("max")) out.ctxMax = toInt(v, ve);
    else if (KEY("pct")) out.ctxPct = toPct(toInt(v, ve));
    else if (KEY("cost")) out.costCents = toInt(v, ve);
    else if (KEY("dur")) out.durS = toInt(v, ve);
    else if (KEY("api")) out.apiS = toInt(v, ve);
    else if (KEY("la")) out.linesAdd = toInt(v, ve);
    else if (KEY("lr")) out.linesDel = toInt(v, ve);
    else if (KEY("h5")) out.h5Pct = toPct(toInt(v, ve));
    else if (KEY("r5")) out.h5ResetS = toInt(v, ve);
    else if (KEY("w7")) out.w7Pct = toPct(toInt(v, ve));
    else if (KEY("r7")) out.w7ResetS = toInt(v, ve);
    else if (KEY("sp")) out.spPct = toPct(toInt(v, ve));
    else if (KEY("rs")) out.spResetS = toInt(v, ve);
    else if (KEY("spu")) out.spUsedCents = toInt(v, ve);
    else if (KEY("spl")) out.spLimitCents = toInt(v, ve);
    else if (KEY("n")) { int32_t n = toInt(v, ve); out.sessions = (int16_t)(n < 0 ? 0 : n > 99 ? 99 : n); }
    else if (KEY("model")) decodeText(v, ve, out.model, sizeof(out.model));
    else if (KEY("proj")) decodeText(v, ve, out.project, sizeof(out.project));
    else if (KEY("name")) decodeText(v, ve, out.session, sizeof(out.session));
    else if (KEY("eff")) decodeText(v, ve, out.effort, sizeof(out.effort));
#undef KEY
  }
  if (version != CLAUDE_PROTO) {
    out.clear();
    return false;
  }
  return true;
}

uint8_t claudeAlertFor(uint8_t prev, uint8_t next, uint32_t workedMs) {
  if (next == prev) return CA_NONE;
  if (next == CS_NEEDS_YOU) return CA_NEEDS_YOU;
  if (next == CS_ERROR) return CA_ERROR;
  if (next == CS_DONE && prev == CS_WORKING && workedMs >= CLAUDE_DONE_CHIME_MS) return CA_DONE;
  return CA_NONE;
}

ClaudeEvent ClaudeMon::feed(const char *payload, uint32_t nowMs) {
  ClaudeEvent ev = { CA_NONE, false, false };
  ClaudeData nd;
  if (!claudeParse(payload, nd)) return ev;
  ev.linkUp = !alive(nowMs);
  uint8_t prev = ever ? d.status : (uint8_t)CS_NONE;
  uint32_t worked = (prev == CS_WORKING) ? nowMs - workSinceMs : 0;
  if (nd.status == CS_WORKING && prev != CS_WORKING) workSinceMs = nowMs;
  ev.alert = claudeAlertFor(prev, nd.status, worked);
  // Claude working keeps the screen awake; so does any number that moved
  ev.activity = nd.status == CS_WORKING ||
                (ever && (nd.status != d.status || nd.ctxUsed != d.ctxUsed || nd.costCents != d.costCents));
  d = nd;
  ever = true;
  rxMs = nowMs;
  return ev;
}

int32_t ClaudeMon::left(int32_t s, uint32_t nowMs) const {
  if (s < 0) return -1;
  int32_t l = s - (int32_t)sinceRxS(nowMs);
  return l < 0 ? 0 : l;
}

// --- formatting ------------------------------------------------------------

void claudeFmtTokens(int32_t t, char *buf, size_t n) {
  if (t < 0) snprintf(buf, n, "?");
  else if (t < 1000) snprintf(buf, n, "%d", (int)t);
  else if (t < 9950) {
    int32_t tenths = (t + 50) / 100;  // thousands, one decimal
    snprintf(buf, n, "%d.%dk", (int)(tenths / 10), (int)(tenths % 10));
  }
  else if (t < 999500) snprintf(buf, n, "%dk", (int)((t + 500) / 1000));
  else {
    int32_t tenths = (t + 50000) / 100000;  // millions, one decimal
    if (tenths % 10) snprintf(buf, n, "%d.%dM", (int)(tenths / 10), (int)(tenths % 10));
    else snprintf(buf, n, "%dM", (int)(tenths / 10));
  }
}

void claudeFmtDuration(int32_t s, char *buf, size_t n) {
  if (s < 0) snprintf(buf, n, "--");
  else if (s < 60) snprintf(buf, n, "%ds", (int)s);
  else if (s < 3600) snprintf(buf, n, "%dm", (int)(s / 60));
  else if (s < 86400) snprintf(buf, n, "%dh %02dm", (int)(s / 3600), (int)(s % 3600 / 60));
  else snprintf(buf, n, "%dd %dh", (int)(s / 86400), (int)(s % 86400 / 3600));
}

void claudeFmtCents(int32_t c, char *buf, size_t n) {
  if (c < 0) snprintf(buf, n, "--");
  else if (c < 10000) snprintf(buf, n, "$%d.%02d", (int)(c / 100), (int)(c % 100));
  else if (c < 100000) snprintf(buf, n, "$%d", (int)((c + 50) / 100));
  else snprintf(buf, n, "$%d.%dk", (int)(c / 100000), (int)(c % 100000 / 10000));
}

const char *claudeStatusText(uint8_t st) {
  switch (st) {
    case CS_WORKING: return "WORKING";
    case CS_DONE: return "YOUR TURN";
    case CS_NEEDS_YOU: return "NEEDS YOU";
    case CS_ERROR: return "ERROR";
    case CS_IDLE: return "IDLE";
    default: return "NO SESSION";
  }
}

void claudeStatusLines(uint8_t st, const char **l1, const char **l2) {
  *l2 = "";
  switch (st) {
    case CS_WORKING: *l1 = "Claude is working..."; break;
    case CS_DONE: *l1 = "Your turn!"; *l2 = "What will you do?"; break;
    case CS_NEEDS_YOU: *l1 = "Claude needs you!"; *l2 = "Check the terminal."; break;
    case CS_ERROR: *l1 = "Claude hit an error!"; *l2 = "Check the terminal."; break;
    case CS_IDLE: *l1 = "Claude is resting."; break;
    default: *l1 = "No Claude session."; *l2 = "Start Claude Code."; break;
  }
}

// --- frame -----------------------------------------------------------------

static void copyText(char *dst, size_t n, const char *src) {
  snprintf(dst, n, "%s", src ? src : "");
}

static void fmtReset(const ClaudeMon &m, int32_t s, uint32_t nowMs, char *buf, size_t n) {
  int32_t l = m.left(s, nowMs);
  if (l < 0) { buf[0] = 0; return; }
  if (l == 0) { snprintf(buf, n, "resetting now"); return; }
  char d[12];
  claudeFmtDuration(l, d, sizeof(d));
  snprintf(buf, n, "resets in %s", d);
}

void claudeBuildFrame(const ClaudeMon &m, const ClaudeView &v, uint32_t nowMs, ClaudeFrame &f) {
  memset(&f, 0, sizeof(f));  // the key hashes every byte: padding and string tails must be zero
  const ClaudeData &d = m.d;
  bool live = m.alive(nowMs);
  f.page = live ? (uint8_t)(v.page % CLAUDE_PAGES) : (uint8_t)CLAUDE_PAGE_WAIT;
  f.status = live ? d.status : (uint8_t)CS_NONE;

  // anti burn-in: the whole dashboard drifts around a 2 px square, one step every 2 minutes
  static const int8_t ORBIT[8][2] = { {0, 0}, {2, 0}, {2, 2}, {0, 2}, {-2, 2}, {-2, 0}, {-2, -2}, {0, -2} };
  uint32_t step = (nowMs / 120000UL) % 8;
  f.ox = ORBIT[step][0];
  f.oy = ORBIT[step][1];

  f.petThumb = v.petThumb;
  copyText(f.petMsg, sizeof(f.petMsg), v.petMsg);

  if (!live) {
    if (m.ever) {
      char ago[12];
      claudeFmtDuration((int32_t)m.sinceRxS(nowMs), ago, sizeof(ago));
      copyText(f.wait1, sizeof(f.wait1), "Lost the PC link.");
      snprintf(f.wait2, sizeof(f.wait2), "Last data %s ago", ago);
    } else {
      copyText(f.wait1, sizeof(f.wait1), "Waiting for your PC");
      copyText(f.wait2, sizeof(f.wait2), "claude_monitor.py serve");
    }
    return;
  }

  int16_t pct = d.ctxPct;
  if (pct < 0 && d.ctxUsed >= 0 && d.ctxMax > 0) pct = (int16_t)((int64_t)d.ctxUsed * 100 / d.ctxMax);
  if (pct > 100) pct = 100;
  f.ctxPct = pct;
  if (pct >= 0) snprintf(f.ctxBig, sizeof(f.ctxBig), "%d", pct);
  else copyText(f.ctxBig, sizeof(f.ctxBig), "--");

  char a[12], b[12];
  if (d.ctxUsed >= 0 && d.ctxMax > 0) {
    claudeFmtTokens(d.ctxUsed, a, sizeof(a));
    claudeFmtTokens(d.ctxMax, b, sizeof(b));
    snprintf(f.ctxUsed, sizeof(f.ctxUsed), "%s of %s", a, b);
    int32_t rest = d.ctxMax - d.ctxUsed;
    claudeFmtTokens(rest < 0 ? 0 : rest, a, sizeof(a));
    snprintf(f.ctxLeft, sizeof(f.ctxLeft), "%s left", a);
    snprintf(f.ctxHp, sizeof(f.ctxHp), "%s/%s", a, b);
  } else if (d.ctxUsed >= 0) {
    claudeFmtTokens(d.ctxUsed, a, sizeof(a));
    snprintf(f.ctxUsed, sizeof(f.ctxUsed), "%s tokens", a);
  } else {
    copyText(f.ctxUsed, sizeof(f.ctxUsed), d.status == CS_NONE ? "no session open" : "no reply yet");
  }

  copyText(f.model, sizeof(f.model), d.model[0] ? d.model : "Claude");
  claudeFmtDuration(d.durS, f.dur, sizeof(f.dur));

  f.h5Pct = d.h5Pct;
  f.w7Pct = d.w7Pct;
  f.spPct = d.spPct;
  fmtReset(m, d.h5ResetS, nowMs, f.h5Reset, sizeof(f.h5Reset));
  fmtReset(m, d.w7ResetS, nowMs, f.w7Reset, sizeof(f.w7Reset));
  if (d.spPct >= 0) {
    if (d.spUsedCents >= 0 && d.spLimitCents > 0) {
      claudeFmtCents(d.spUsedCents, a, sizeof(a));
      claudeFmtCents(d.spLimitCents, b, sizeof(b));
      snprintf(f.spend, sizeof(f.spend), "spend %s of %s", a, b);
    } else {
      snprintf(f.spend, sizeof(f.spend), "spend limit %d%%", d.spPct);
    }
  }

  copyText(f.title, sizeof(f.title), d.session[0] ? d.session : d.project[0] ? d.project : "Claude Code");
  claudeFmtCents(d.costCents, f.cost, sizeof(f.cost));
  claudeFmtDuration(d.apiS, f.api, sizeof(f.api));
  if (d.linesAdd >= 0 || d.linesDel >= 0)
    snprintf(f.lines, sizeof(f.lines), "+%d -%d", (int)(d.linesAdd < 0 ? 0 : d.linesAdd % 1000000),
             (int)(d.linesDel < 0 ? 0 : d.linesDel % 1000000));
  else
    copyText(f.lines, sizeof(f.lines), "--");
  f.nSessions = d.sessions;
  if (d.sessions > 1) snprintf(f.sessions, sizeof(f.sessions), "%d sessions open", d.sessions);
  copyText(f.effort, sizeof(f.effort), d.effort);
}

uint32_t claudeFrameKey(const ClaudeFrame &f) {
  const uint8_t *p = (const uint8_t *)&f;
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < sizeof(f); i++) h = (h ^ p[i]) * 16777619u;
  return h;
}
