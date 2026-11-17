// Renders the Claude dashboard screens on a PC with the firmware's own code
// (claudemon.cpp + claudeui.cpp) and the real Arduino_GFX drawing core.
// Each scene feeds ClaudeMon the same "CLAUDE ..." line the PC bridge sends,
// so the pictures show exactly what the board draws for that data.
//
//   preview <out_dir> [pet_thumb.bin] [live]
//
// Writes <out_dir>/<scene>.rgb565 (466x466, little-endian RGB565) per scene;
// make_png.py turns them into PNGs. Built and run by build.sh.
// With "live", reads one "CLAUDE ..." line from stdin instead (what
// `claude_monitor.py status` prints) and renders the three pages for it.
#include <Arduino.h>
#include "Arduino_GFX_Library.h"
#include "../../claudemon.h"
#include "../../claudeui.h"
#include <vector>
#include <string>

static uint32_t gMillis = 0;
uint32_t millis() { return gMillis; }
uint32_t micros() { return gMillis * 1000; }

// the canvas needs an output device; nothing is ever flushed to it here
class NullOutput : public Arduino_G {
public:
  NullOutput() : Arduino_G(466, 466) {}
  bool begin(int32_t) override { return true; }
  void drawBitmap(int16_t, int16_t, uint8_t *, int16_t, int16_t, uint16_t, uint16_t) override {}
  void drawIndexedBitmap(int16_t, int16_t, uint8_t *, uint16_t *, int16_t, int16_t, int16_t) override {}
  void draw3bitRGBBitmap(int16_t, int16_t, uint8_t *, int16_t, int16_t) override {}
  void draw16bitRGBBitmap(int16_t, int16_t, uint16_t *, int16_t, int16_t) override {}
  void draw24bitRGBBitmap(int16_t, int16_t, uint8_t *, int16_t, int16_t) override {}
};

struct Scene {
  const char *name;
  uint8_t page;
  const char *line;      // what the bridge sends
  uint32_t ageMs;        // time since that line arrived when the frame is drawn
  const char *petMsg;
  bool petThumb;
};

static const Scene SCENES[] = {
  { "context-working", 0,
    "v=1 st=1 ctx=64230 max=200000 pct=32 cost=184 dur=1520 api=410 la=86 lr=12 h5=18 r5=12600 w7=35 r7=302400 "
    "n=1 model=Opus%204.5 proj=TamaPoke name=Claude%20usage%20display eff=high", 1000, "", false },
  { "context-your-turn", 0,
    "v=1 st=2 ctx=142800 max=200000 pct=71 cost=612 dur=5340 api=1460 la=420 lr=88 h5=46 r5=8040 w7=52 r7=273600 "
    "n=2 model=Opus%204.5 proj=TamaPoke name=Claude%20usage%20display eff=high", 1000, "", false },
  { "context-needs-you", 0,
    "v=1 st=3 ctx=176400 max=200000 pct=88 cost=940 dur=7620 api=2210 la=612 lr=140 h5=71 r5=3300 w7=58 r7=262800 "
    "n=2 model=Opus%204.5 proj=TamaPoke name=Claude%20usage%20display eff=high", 1000, "", false },
  { "context-1m", 0,
    "v=1 st=1 ctx=412000 max=1000000 pct=41 cost=1220 dur=9900 api=3100 la=1290 lr=410 h5=33 r5=9000 w7=40 r7=200000 "
    "n=1 model=Sonnet%205.5%20(1M) proj=api-gateway eff=medium", 1000, "", false },
  { "limits", 1,
    "v=1 st=1 ctx=64230 max=200000 pct=32 cost=184 dur=1520 api=410 la=86 lr=12 h5=23 r5=8040 w7=41 r7=273600 "
    "n=1 model=Opus%204.5 proj=TamaPoke", 1000, "", false },
  { "limits-high", 1,
    "v=1 st=2 ctx=98000 max=200000 pct=49 cost=2210 dur=14400 api=5200 la=900 lr=300 h5=92 r5=1260 w7=76 r7=90000 "
    "n=1 model=Opus%204.5 proj=TamaPoke", 1000, "", false },
  { "limits-api-key", 1,
    "v=1 st=2 ctx=51000 max=200000 pct=25 cost=347 dur=2400 api=700 la=40 lr=5 n=1 model=Opus%204.5 proj=TamaPoke",
    1000, "", false },
  { "session", 2,
    "v=1 st=1 ctx=142800 max=200000 pct=71 cost=612 dur=5340 api=1460 la=420 lr=88 h5=46 r5=8040 w7=52 r7=273600 "
    "n=2 model=Opus%204.5 proj=TamaPoke name=Claude%20usage%20display eff=high", 1000, "Pikachu is hungry", false },
  { "waiting", 0, nullptr, 0, "", true },
};

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s OUT_DIR [PET_THUMB.bin]\n", argv[0]);
    return 2;
  }
  std::string out = argv[1];
  std::vector<uint8_t> thumb;
  if (argc >= 3) {
    FILE *t = fopen(argv[2], "rb");
    if (t) {
      uint8_t buf[8192];
      size_t n = fread(buf, 1, sizeof(buf), t);
      thumb.assign(buf, buf + n);
      fclose(t);
    }
  }
  NullOutput output;
  Arduino_Canvas canvas(466, 466, &output);
  if (!canvas.begin(GFX_SKIP_OUTPUT_BEGIN)) {
    fprintf(stderr, "canvas begin failed\n");
    return 1;
  }
  std::vector<Scene> scenes(SCENES, SCENES + sizeof(SCENES) / sizeof(SCENES[0]));
  static char liveLine[1024];
  if (argc >= 4 && strcmp(argv[3], "live") == 0) {
    if (!fgets(liveLine, sizeof(liveLine), stdin) || strncmp(liveLine, "CLAUDE ", 7) != 0) {
      fprintf(stderr, "live: expected a CLAUDE line on stdin\n");
      return 1;
    }
    liveLine[strcspn(liveLine, "\r\n")] = 0;
    scenes = { { "live-context", 0, liveLine + 7, 1000, "", false },
               { "live-limits", 1, liveLine + 7, 1000, "", false },
               { "live-session", 2, liveLine + 7, 1000, "", false } };
  }
  for (const Scene &s : scenes) {
    ClaudeMon mon;
    gMillis = 0;  // orbit step 0: the previews show the dashboard un-shifted
    if (s.line) mon.feed(s.line, gMillis);
    gMillis += s.ageMs;
    ClaudeView view = { s.page, (s.petThumb && !thumb.empty()) ? thumb.data() : nullptr, s.petMsg };
    ClaudeFrame f;
    claudeBuildFrame(mon, view, gMillis, f);
    claudeDraw(&canvas, f, 233, 233, true);
    std::string path = out + "/" + s.name + ".rgb565";
    FILE *fo = fopen(path.c_str(), "wb");
    if (!fo) { perror(path.c_str()); return 1; }
    fwrite(canvas.getFramebuffer(), 2, 466 * 466, fo);
    fclose(fo);
    printf("%s (key %08x)\n", s.name, claudeFrameKey(f));
  }
  return 0;
}
