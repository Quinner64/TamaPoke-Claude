#pragma once
#include <stdint.h>
#include <stddef.h>

// Claude Code usage monitor ("Claude dashboard").
//
// The PC runs tools/claude_monitor.py, which reads what Claude Code reports to
// its status line (context window, cost, session time, plan limits) and to a few
// hooks (working / waiting for you / done), and sends one line over USB serial
// every couple of seconds:
//
//   CLAUDE v=1 st=1 ctx=124300 max=200000 pct=62 cost=421 dur=4980 api=720
//          la=156 lr=23 h5=23 r5=8040 w7=41 r7=273600 n=2 model=Opus%204.5 ...
//
// Every line is a full snapshot: a key that is missing means "unknown" (-1 or
// empty), not "unchanged". Unknown keys are ignored, so the PC side can add
// fields without breaking older firmware. Text values are percent-encoded
// (%20 = space) and ASCII only. Reset times travel as "seconds from now", so the
// countdowns work without the board's clock being right.
//
// Pure logic, no hardware: compiled and tested on the PC (test/test_claudemon.cpp).
// The drawing lives in claudeui.cpp.

#define CLAUDE_PROTO 1
#define CLAUDE_LINK_MS 15000UL  // no line for this long = the PC bridge is gone
#define CLAUDE_DONE_CHIME_MS 30000UL  // a turn that ran at least this long chimes when it ends

enum ClaudeStatus : uint8_t {
  CS_NONE = 0,   // no Claude Code session reported
  CS_WORKING,    // Claude is responding or running tools
  CS_DONE,       // turn finished: your move
  CS_NEEDS_YOU,  // waiting for a permission or an answer
  CS_ERROR,      // the turn ended with an API error (rate limit, auth, ...)
  CS_IDLE,       // session open, nothing going on
  CS_COUNT
};

enum ClaudeAlert : uint8_t { CA_NONE = 0, CA_DONE, CA_NEEDS_YOU, CA_ERROR };

// One snapshot, as sent by the PC. -1 = not reported.
struct ClaudeData {
  uint8_t status;
  int32_t ctxUsed, ctxMax;    // tokens in the context window / its size
  int16_t ctxPct;             // 0..100
  int32_t costCents;          // estimated session cost, US cents
  int32_t durS, apiS;         // session wall time / time waiting on the API
  int32_t linesAdd, linesDel;
  int16_t h5Pct, w7Pct;       // plan usage: 5-hour and 7-day windows, 0..100
  int32_t h5ResetS, w7ResetS; // seconds until each window resets (at receive time)
  int16_t spPct;              // spend limit (behind a Claude apps gateway)
  int32_t spResetS, spUsedCents, spLimitCents;
  int16_t sessions;           // Claude Code sessions open on the PC
  char model[24];
  char project[28];
  char session[32];
  char effort[8];
  void clear();
};

struct ClaudeEvent {
  uint8_t alert;   // ClaudeAlert worth a chime / wake
  bool activity;   // something Claude did changed the numbers (keeps the screen awake)
  bool linkUp;     // first line after the link was down
};

struct ClaudeMon {
  ClaudeData d;
  bool ever = false;           // a line was received since boot
  uint32_t rxMs = 0;           // millis() of the last line
  uint32_t workSinceMs = 0;    // when the status last became CS_WORKING

  ClaudeMon() { d.clear(); }
  // payload = the line after "CLAUDE ". Returns what changed.
  ClaudeEvent feed(const char *payload, uint32_t nowMs);
  bool alive(uint32_t nowMs) const { return ever && (uint32_t)(nowMs - rxMs) < CLAUDE_LINK_MS; }
  // seconds left of a countdown received as `s`; -1 stays -1, never below 0
  int32_t left(int32_t s, uint32_t nowMs) const;
  uint32_t sinceRxS(uint32_t nowMs) const { return (uint32_t)(nowMs - rxMs) / 1000; }
};

// Parses one payload into `out` (cleared first). False if it is not protocol v1.
bool claudeParse(const char *payload, ClaudeData &out);
// Which status changes deserve a chime: needing you and errors always, a finished
// turn only if Claude worked on it for a while (you have probably looked away).
uint8_t claudeAlertFor(uint8_t prev, uint8_t next, uint32_t workedMs);

// --- formatting (shared by the screens and the tests) ----------------------
void claudeFmtTokens(int32_t t, char *buf, size_t n);    // 850, 12.3k, 124k, 1.2M
void claudeFmtDuration(int32_t s, char *buf, size_t n);  // 45s, 12m, 1h 23m, 3d 4h
void claudeFmtCents(int32_t c, char *buf, size_t n);     // $0.42, $12.30, $123, $1.2k
const char *claudeStatusText(uint8_t st);

// --- what a dashboard frame shows ------------------------------------------
// Everything a screen draws, already formatted. The firmware compares
// claudeFrameKey() between frames and only redraws (and pushes the frame over
// QSPI) when it changes: the dashboard is static most of the time.
#define CLAUDE_PAGES 3
#define CLAUDE_PAGE_WAIT 0xFF  // "waiting for the PC bridge"

struct ClaudeFrame {
  uint8_t page;          // 0 context, 1 plan limits, 2 session, CLAUDE_PAGE_WAIT
  uint8_t status;
  int8_t ox, oy;         // anti burn-in orbit, a few px
  int16_t ctxPct;        // -1 unknown
  int16_t h5Pct, w7Pct, spPct;
  char ctxBig[6];        // "62"
  char ctxUsed[28];      // "124k of 200k"
  char ctxLeft[20];      // "76k left"
  char model[24];
  char dur[12];
  char h5Reset[24], w7Reset[24];
  char spend[40];
  char title[32];        // session name, else project
  char cost[12], api[12], lines[24];
  char sessions[24];     // "2 sessions open" (empty for one)
  char effort[8];
  char wait1[32], wait2[32];
  const uint8_t *petThumb;  // gallery thumbnail of the pet (waiting screen), may be null
  char petMsg[40];          // "Pikachu is hungry" (session page), may be empty
};

struct ClaudeView {
  uint8_t page;
  const uint8_t *petThumb;
  const char *petMsg;
};

void claudeBuildFrame(const ClaudeMon &m, const ClaudeView &v, uint32_t nowMs, ClaudeFrame &f);
uint32_t claudeFrameKey(const ClaudeFrame &f);
