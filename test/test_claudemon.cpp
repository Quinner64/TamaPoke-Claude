// Claude dashboard: the serial protocol, countdowns, alerts and screen frames
// (claudemon.cpp). The drawing itself (claudeui.cpp) is checked by eye with
// tools/claude_preview/build.sh.
#include "framework.h"
#include "../claudemon.h"
#include <string>

static const char *FULL =
    "v=1 st=1 ctx=124300 max=200000 pct=62 cost=421 dur=4980 api=720 la=156 lr=23 "
    "h5=23 r5=8040 w7=41 r7=273600 n=2 model=Opus%204.5 proj=TamaPoke name=Fix%20login eff=high";

TEST(claude, parse_reads_every_field) {
  ClaudeData d;
  CHECK(claudeParse(FULL, d));
  CHECK_EQ((int)d.status, (int)CS_WORKING);
  CHECK_EQ(d.ctxUsed, 124300);
  CHECK_EQ(d.ctxMax, 200000);
  CHECK_EQ(d.ctxPct, 62);
  CHECK_EQ(d.costCents, 421);
  CHECK_EQ(d.durS, 4980);
  CHECK_EQ(d.apiS, 720);
  CHECK_EQ(d.linesAdd, 156);
  CHECK_EQ(d.linesDel, 23);
  CHECK_EQ(d.h5Pct, 23);
  CHECK_EQ(d.h5ResetS, 8040);
  CHECK_EQ(d.w7Pct, 41);
  CHECK_EQ(d.w7ResetS, 273600);
  CHECK_EQ(d.sessions, 2);
  CHECK_EQ(std::string(d.model), std::string("Opus 4.5"));
  CHECK_EQ(std::string(d.project), std::string("TamaPoke"));
  CHECK_EQ(std::string(d.session), std::string("Fix login"));
  CHECK_EQ(std::string(d.effort), std::string("high"));
}

TEST(claude, missing_keys_mean_unknown_not_unchanged) {
  ClaudeData d;
  CHECK(claudeParse("v=1 st=2", d));
  CHECK_EQ((int)d.status, (int)CS_DONE);
  CHECK_EQ(d.ctxUsed, -1);
  CHECK_EQ(d.ctxPct, -1);
  CHECK_EQ(d.h5Pct, -1);
  CHECK_EQ(d.w7ResetS, -1);
  CHECK_EQ(d.costCents, -1);
  CHECK_EQ(d.model[0], '\0');
}

TEST(claude, rejects_other_protocol_versions_and_garbage) {
  ClaudeData d;
  CHECK(!claudeParse("st=1 ctx=5", d));  // no version
  CHECK(!claudeParse("v=2 st=1", d));
  CHECK(!claudeParse("", d));
  CHECK(!claudeParse(nullptr, d));
  CHECK_EQ(d.ctxUsed, -1);  // a rejected line leaves nothing behind
}

TEST(claude, unknown_keys_and_bare_words_are_ignored) {
  ClaudeData d;
  CHECK(claudeParse("v=1 future=42 st=1  bogus  ctx=10 x= =5", d));
  CHECK_EQ((int)d.status, (int)CS_WORKING);
  CHECK_EQ(d.ctxUsed, 10);
}

TEST(claude, text_is_decoded_ascii_only_and_bounded) {
  ClaudeData d;
  // %E2%9C%93 is a UTF-8 check mark: the 5x7 world has no such glyph
  CHECK(claudeParse("v=1 name=ok%E2%9C%93%20done model=%zz eff=abcdefghijklmnop", d));
  CHECK_EQ(std::string(d.session), std::string("ok??? done"));
  CHECK_EQ(std::string(d.model), std::string("%zz"));  // a bad escape is kept literally
  CHECK_EQ(strlen(d.effort), sizeof(d.effort) - 1);    // truncated, still terminated
}

TEST(claude, bad_numbers_and_statuses_stay_unknown) {
  ClaudeData d;
  CHECK(claudeParse("v=1 st=77 ctx=abc pct=-30 h5=5000", d));
  CHECK_EQ((int)d.status, (int)CS_NONE);
  CHECK_EQ(d.ctxUsed, -1);
  CHECK_EQ(d.ctxPct, -1);
  CHECK_EQ(d.h5Pct, 999);  // clamped, not wrapped
}

TEST(claude, link_is_alive_for_15_seconds_after_a_line) {
  ClaudeMon m;
  CHECK(!m.alive(0));
  ClaudeEvent e = m.feed("v=1 st=5", 1000);
  CHECK(e.linkUp);
  CHECK(m.alive(1000));
  CHECK(m.alive(1000 + CLAUDE_LINK_MS - 1));
  CHECK(!m.alive(1000 + CLAUDE_LINK_MS));
  e = m.feed("v=1 st=5", 3000);
  CHECK(!e.linkUp);
  e = m.feed("v=1 st=5", 3000 + CLAUDE_LINK_MS + 5);
  CHECK(e.linkUp);  // came back after a gap
}

TEST(claude, link_survives_the_millis_rollover) {
  ClaudeMon m;
  m.feed("v=1 st=1", 0xFFFFF000u);
  CHECK(m.alive(0x00000100u));  // 4.3 s later, across the wrap
}

// loop() reads millis() once at the top, then handles serial: the line is stamped a
// few ms later than the `now` claudeTick() gets. That used to wrap to ~49 days,
// so the board declared the PC silent and closed the dashboard every few seconds.
TEST(claude, a_line_stamped_after_now_keeps_the_link_alive) {
  ClaudeMon m;
  m.feed("v=1 st=1", 50000);
  CHECK(m.alive(49995));
  CHECK_EQ(m.sinceRxS(49995), 0u);
  CHECK_EQ(m.sinceRxMs(49995), 0u);
}

TEST(claude, countdowns_run_down_from_the_receive_time) {
  ClaudeMon m;
  m.feed("v=1 r5=600", 10000);
  CHECK_EQ(m.left(m.d.h5ResetS, 10000), 600);
  CHECK_EQ(m.left(m.d.h5ResetS, 70000), 540);
  CHECK_EQ(m.left(m.d.h5ResetS, 10000 + 900000), 0);  // never negative
  CHECK_EQ(m.left(m.d.w7ResetS, 70000), -1);          // unknown stays unknown
}

TEST(claude, alerts_needing_you_and_errors_always) {
  CHECK_EQ((int)claudeAlertFor(CS_WORKING, CS_NEEDS_YOU, 0), (int)CA_NEEDS_YOU);
  CHECK_EQ((int)claudeAlertFor(CS_NONE, CS_NEEDS_YOU, 0), (int)CA_NEEDS_YOU);
  CHECK_EQ((int)claudeAlertFor(CS_WORKING, CS_ERROR, 0), (int)CA_ERROR);
  CHECK_EQ((int)claudeAlertFor(CS_NEEDS_YOU, CS_NEEDS_YOU, 0), (int)CA_NONE);  // only on the change
}

TEST(claude, a_finished_turn_chimes_only_after_a_long_run) {
  CHECK_EQ((int)claudeAlertFor(CS_WORKING, CS_DONE, CLAUDE_DONE_CHIME_MS - 1), (int)CA_NONE);
  CHECK_EQ((int)claudeAlertFor(CS_WORKING, CS_DONE, CLAUDE_DONE_CHIME_MS), (int)CA_DONE);
  CHECK_EQ((int)claudeAlertFor(CS_IDLE, CS_DONE, 999999), (int)CA_NONE);
}

TEST(claude, feed_times_the_turn_for_the_done_chime) {
  ClaudeMon m;
  m.feed("v=1 st=2", 0);
  m.feed("v=1 st=1", 1000);   // starts working
  m.feed("v=1 st=1", 20000);  // still working: the start time is kept
  ClaudeEvent e = m.feed("v=1 st=2", 1000 + CLAUDE_DONE_CHIME_MS);
  CHECK_EQ((int)e.alert, (int)CA_DONE);
  m.feed("v=1 st=1", 100000);
  e = m.feed("v=1 st=2", 105000);  // a 5 s turn: no chime
  CHECK_EQ((int)e.alert, (int)CA_NONE);
}

TEST(claude, activity_keeps_the_screen_awake_only_when_something_moves) {
  ClaudeMon m;
  m.feed("v=1 st=2 ctx=100 cost=5", 0);
  CHECK(!m.feed("v=1 st=2 ctx=100 cost=5", 2000).activity);  // nothing changed
  CHECK(m.feed("v=1 st=2 ctx=200 cost=5", 4000).activity);   // context grew
  CHECK(m.feed("v=1 st=2 ctx=200 cost=9", 6000).activity);   // cost grew
  CHECK(m.feed("v=1 st=3 ctx=200 cost=9", 8000).activity);   // status changed
  CHECK(m.feed("v=1 st=1 ctx=200 cost=9", 10000).activity);  // working
  CHECK(m.feed("v=1 st=1 ctx=200 cost=9", 12000).activity);  // still working: stays awake
}

TEST(claude, token_format) {
  char b[16];
  claudeFmtTokens(850, b, sizeof(b));     CHECK_EQ(std::string(b), std::string("850"));
  claudeFmtTokens(1960, b, sizeof(b));    CHECK_EQ(std::string(b), std::string("2.0k"));
  claudeFmtTokens(9949, b, sizeof(b));    CHECK_EQ(std::string(b), std::string("9.9k"));
  claudeFmtTokens(9950, b, sizeof(b));    CHECK_EQ(std::string(b), std::string("10k"));
  claudeFmtTokens(124300, b, sizeof(b));  CHECK_EQ(std::string(b), std::string("124k"));
  claudeFmtTokens(999600, b, sizeof(b));  CHECK_EQ(std::string(b), std::string("1M"));
  claudeFmtTokens(1250000, b, sizeof(b)); CHECK_EQ(std::string(b), std::string("1.3M"));
  claudeFmtTokens(-1, b, sizeof(b));      CHECK_EQ(std::string(b), std::string("?"));
}

TEST(claude, duration_format) {
  char b[16];
  claudeFmtDuration(45, b, sizeof(b));     CHECK_EQ(std::string(b), std::string("45s"));
  claudeFmtDuration(754, b, sizeof(b));    CHECK_EQ(std::string(b), std::string("12m"));
  claudeFmtDuration(3900, b, sizeof(b));   CHECK_EQ(std::string(b), std::string("1h 05m"));
  claudeFmtDuration(273600, b, sizeof(b)); CHECK_EQ(std::string(b), std::string("3d 4h"));
  claudeFmtDuration(-1, b, sizeof(b));     CHECK_EQ(std::string(b), std::string("--"));
}

TEST(claude, money_format) {
  char b[16];
  claudeFmtCents(42, b, sizeof(b));      CHECK_EQ(std::string(b), std::string("$0.42"));
  claudeFmtCents(1230, b, sizeof(b));    CHECK_EQ(std::string(b), std::string("$12.30"));
  claudeFmtCents(12345, b, sizeof(b));   CHECK_EQ(std::string(b), std::string("$123"));
  claudeFmtCents(123456, b, sizeof(b));  CHECK_EQ(std::string(b), std::string("$1.2k"));
  claudeFmtCents(-1, b, sizeof(b));      CHECK_EQ(std::string(b), std::string("--"));
}

static ClaudeView view(uint8_t page) {
  ClaudeView v = { page, nullptr, "" };
  return v;
}

TEST(claude, frame_shows_context_used_and_left) {
  ClaudeMon m;
  m.feed(FULL, 0);
  ClaudeFrame f;
  claudeBuildFrame(m, view(0), 1000, f);
  CHECK_EQ((int)f.page, 0);
  CHECK_EQ(f.ctxPct, 62);
  CHECK_EQ(std::string(f.ctxBig), std::string("62"));
  CHECK_EQ(std::string(f.ctxUsed), std::string("124k of 200k"));
  CHECK_EQ(std::string(f.ctxLeft), std::string("76k left"));
  CHECK_EQ(std::string(f.ctxHp), std::string("76k/200k"));  // read like HP: what is left of the window
  CHECK_EQ(f.nSessions, 2);
  CHECK_EQ(std::string(f.dur), std::string("1h 23m"));
  CHECK_EQ(std::string(f.title), std::string("Fix login"));
  CHECK_EQ(std::string(f.sessions), std::string("2 sessions open"));
  CHECK_EQ(std::string(f.lines), std::string("+156 -23"));
}

TEST(claude, the_waiting_badge_counts_the_other_sessions) {
  ClaudeMon m;
  m.feed("v=1 st=1 n=3 wt=2", 1000);
  CHECK_EQ(m.d.waiting, 2);
  ClaudeFrame f;
  claudeBuildFrame(m, view(0), 1000, f);
  CHECK_EQ(f.nWaiting, 2);
  m.feed("v=1 st=1 n=3", 2000);  // missing = unknown = no badge
  claudeBuildFrame(m, view(0), 2000, f);
  CHECK_EQ(f.nWaiting, 0);
}

TEST(claude, frame_computes_the_percentage_when_it_is_missing) {
  ClaudeMon m;
  m.feed("v=1 st=1 ctx=50000 max=200000", 0);
  ClaudeFrame f;
  claudeBuildFrame(m, view(0), 0, f);
  CHECK_EQ(f.ctxPct, 25);
}

TEST(claude, frame_resets_count_down) {
  ClaudeMon m;
  m.feed(FULL, 0);
  ClaudeFrame f;
  claudeBuildFrame(m, view(1), 14000, f);  // inside the 15 s link window
  CHECK_EQ(std::string(f.h5Reset), std::string("resets in 2h 13m"));
  CHECK_EQ(std::string(f.w7Reset), std::string("resets in 3d 3h"));
}

TEST(claude, frame_without_a_link_is_the_waiting_screen) {
  ClaudeMon m;
  ClaudeFrame f;
  claudeBuildFrame(m, view(1), 5000, f);
  CHECK_EQ((int)f.page, (int)CLAUDE_PAGE_WAIT);
  CHECK_EQ(std::string(f.wait1), std::string("Waiting for your PC"));
  m.feed(FULL, 10000);
  claudeBuildFrame(m, view(1), 10000 + 5 * 60000, f);
  CHECK_EQ((int)f.page, (int)CLAUDE_PAGE_WAIT);
  CHECK_EQ(std::string(f.wait2), std::string("Last data 5m ago"));
}

TEST(claude, frame_key_changes_only_with_what_is_drawn) {
  ClaudeMon m;
  m.feed(FULL, 0);
  ClaudeFrame a, b;
  claudeBuildFrame(m, view(0), 1000, a);
  claudeBuildFrame(m, view(0), 1500, b);  // same minute, same everything
  CHECK_EQ(claudeFrameKey(a), claudeFrameKey(b));
  claudeBuildFrame(m, view(1), 1500, b);  // other page
  CHECK(claudeFrameKey(a) != claudeFrameKey(b));
  m.feed("v=1 st=2 ctx=124300 max=200000 pct=62", 2000);
  claudeBuildFrame(m, view(0), 2000, b);  // status changed
  CHECK(claudeFrameKey(a) != claudeFrameKey(b));
}

TEST(claude, frame_drifts_a_few_pixels_against_burn_in) {
  ClaudeMon m;
  m.feed(FULL, 0);
  ClaudeFrame f;
  int moved = 0;
  for (uint32_t t = 0; t < 16 * 60000UL; t += 60000) {
    m.rxMs = t;  // keep the link alive
    claudeBuildFrame(m, view(0), t, f);
    CHECK(f.ox >= -2 && f.ox <= 2 && f.oy >= -2 && f.oy <= 2);
    if (f.ox || f.oy) moved++;
  }
  CHECK(moved > 0);
}

TEST(claude, every_status_has_a_dialog_that_fits_the_box) {
  // the text box holds 20 characters per line in the 5x7 font at size 2
  for (uint8_t st = 0; st < CS_COUNT; st++) {
    const char *l1 = nullptr, *l2 = nullptr;
    claudeStatusLines(st, &l1, &l2);
    CHECK(l1 && l1[0]);
    CHECK(l2);
    CHECK_MSG(strlen(l1) <= 20, l1);
    CHECK_MSG(strlen(l2) <= 20, l2);
  }
  const char *l1, *l2;
  claudeStatusLines(CS_DONE, &l1, &l2);
  CHECK_EQ(std::string(l1), std::string("Your turn!"));
}

TEST(claude, the_field_pages_animate_and_the_cards_do_not) {
  ClaudeMon m;
  ClaudeFrame f;
  claudeBuildFrame(m, view(0), 0, f);
  CHECK(claudeAnimated(f));  // waiting screen: the pet naps on its field
  m.feed(FULL, 0);
  claudeBuildFrame(m, view(0), 0, f);
  CHECK(claudeAnimated(f));
  claudeBuildFrame(m, view(1), 0, f);
  CHECK(!claudeAnimated(f));
  claudeBuildFrame(m, view(2), 0, f);
  CHECK(!claudeAnimated(f));
}
