// Renders the Claude dashboard screens on a PC with the firmware's own code
// (claudemon.cpp, claudeui.cpp, scene.cpp, pmddraw.h) and the real Arduino_GFX
// drawing core, with the real PMD sprites from tools/sdcard/mons/. Each scene
// feeds ClaudeMon the same "CLAUDE ..." line the PC bridge sends, so the
// pictures show exactly what the board draws for that data.
//
//   preview <out_dir> <mons_dir> <pet_thumb.bin> [live]
//
// Writes <out_dir>/<scene>.rgb565 (466x466, little-endian RGB565); animated
// scenes also write <scene>.NN.rgb565 frames for a GIF. make_png.py turns them
// into PNG/GIF. Built and run by build.sh. With "live", reads one "CLAUDE ..."
// line from stdin instead (what `claude_monitor.py status` prints) and renders
// the three pages for it.
#include <Arduino.h>
#include "Arduino_GFX_Library.h"
#include "../../claudemon.h"
#include "../../claudeui.h"
#include "../../sdmon.h"
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

// PmdMon::load() reads the SD card; this is the same parse from a file on the PC
static bool loadTpk2(const std::string &path, PmdMon &m) {
  FILE *f = fopen(path.c_str(), "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  m.blob = (uint8_t *)malloc(size);
  if (!m.blob || fread(m.blob, 1, size, f) != (size_t)size || memcmp(m.blob, "TPK2", 4) != 0) {
    fclose(f);
    return false;
  }
  fclose(f);
  uint8_t nActs = m.blob[4];
  memcpy(&m.palCount, m.blob + 5, 2);
  memcpy(m.pal, m.blob + 7, m.palCount * 2);
  const uint8_t *p = m.blob + 7 + m.palCount * 2;
  for (uint8_t i = 0; i < nActs; i++) {
    uint8_t id = p[0], w = p[1], h = p[2], nf = p[3];
    p += 4;
    PmdAct &a = m.acts[id];
    a.w = w;
    a.h = h;
    a.frames = nf;
    for (uint8_t k = 0; k < nf; k++, p += 2) a.ms[k] = (p[0] | (p[1] << 8)) ? (p[0] | (p[1] << 8)) : 100;
    a.data = p;
    p += (uint32_t)w * h * nf;
    uint8_t base = 1;  // lowest row with a pixel in any frame: anchor by the feet
    for (uint8_t fr = 0; fr < nf; fr++)
      for (int r = h - 1; r >= 0; r--) {
        bool any = false;
        for (int c = 0; c < w && !any; c++) any = a.data[(uint32_t)fr * w * h + r * w + c] != 0xFF;
        if (any) { if (r + 1 > base) base = r + 1; break; }
      }
    a.base = base;
  }
  m.loaded = true;
  return true;
}

struct Scene {
  const char *name;
  uint8_t page;
  const char *line;   // what the bridge sends (nullptr: never connected)
  int dex;            // the pet
  uint8_t biome;      // its biome (dex.h)
  int hour;
  const char *petMsg;
  int gifFrames;      // > 0: also render an animation
};

static const Scene SCENES[] = {
  { "context-working", 0,
    "v=1 st=1 ctx=64230 max=200000 pct=32 cost=184 dur=1520 api=410 la=86 lr=12 h5=18 r5=12600 w7=35 r7=302400 "
    "n=1 model=Opus%204.5 proj=TamaPoke name=Claude%20usage%20display eff=high", 25, 0, 13, "", 24 },
  { "context-working-waiting", 0,
    "v=1 st=1 ctx=64230 max=200000 pct=32 cost=184 dur=1520 api=410 la=86 lr=12 h5=18 r5=12600 w7=35 r7=302400 "
    "n=3 wt=1 model=Opus%204.5 proj=TamaPoke name=Claude%20usage%20display eff=high", 25, 0, 13, "", 0 },
  { "context-your-turn", 0,
    "v=1 st=2 ctx=142800 max=200000 pct=71 cost=612 dur=5340 api=1460 la=420 lr=88 h5=46 r5=8040 w7=52 r7=273600 "
    "n=2 model=Opus%204.5 proj=TamaPoke name=Claude%20usage%20display eff=high", 4, 3, 19, "", 30 },
  { "context-needs-you", 0,
    "v=1 st=3 ctx=176400 max=200000 pct=88 cost=940 dur=7620 api=2210 la=612 lr=140 h5=71 r5=3300 w7=58 r7=262800 "
    "n=2 model=Opus%204.5 proj=TamaPoke name=Claude%20usage%20display eff=high", 7, 1, 11, "", 16 },
  { "context-idle-night", 0,
    "v=1 st=5 ctx=412000 max=1000000 pct=41 cost=1220 dur=9900 api=3100 la=1290 lr=410 h5=33 r5=9000 w7=40 r7=200000 "
    "n=1 model=Sonnet%205.5 proj=api-gateway eff=medium", 1, 2, 22, "", 0 },
  { "limits", 1,
    "v=1 st=1 ctx=64230 max=200000 pct=32 cost=184 dur=1520 api=410 la=86 lr=12 h5=23 r5=8040 w7=41 r7=273600 "
    "n=1 model=Opus%204.5 proj=TamaPoke", 25, 0, 13, "", 0 },
  { "limits-high", 1,
    "v=1 st=2 ctx=98000 max=200000 pct=49 cost=2210 dur=14400 api=5200 la=900 lr=300 h5=92 r5=1260 w7=76 r7=90000 "
    "n=1 model=Opus%204.5 proj=TamaPoke", 25, 0, 13, "", 0 },
  { "limits-api-key", 1,
    "v=1 st=2 ctx=51000 max=200000 pct=25 cost=347 dur=2400 api=700 la=40 lr=5 n=1 model=Opus%204.5 proj=TamaPoke",
    25, 0, 13, "", 0 },
  { "session", 2,
    "v=1 st=1 ctx=142800 max=200000 pct=71 cost=612 dur=5340 api=1460 la=420 lr=88 h5=46 r5=8040 w7=52 r7=273600 "
    "n=2 wt=1 model=Opus%204.5 proj=TamaPoke name=Claude%20usage%20display eff=high", 25, 0, 13, "Pikachu is hungry", 0 },
  { "waiting", 0, nullptr, 25, 0, 13, "", 0 },
};

int main(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr, "usage: %s OUT_DIR MONS_DIR PET_THUMB.bin [live]\n", argv[0]);
    return 2;
  }
  std::string out = argv[1], mons = argv[2];
  std::vector<uint8_t> thumb;
  if (FILE *t = fopen(argv[3], "rb")) {
    uint8_t buf[8192];
    size_t n = fread(buf, 1, sizeof(buf), t);
    thumb.assign(buf, buf + n);
    fclose(t);
  }
  NullOutput output;
  Arduino_Canvas canvas(466, 466, &output);
  if (!canvas.begin(GFX_SKIP_OUTPUT_BEGIN)) {
    fprintf(stderr, "canvas begin failed\n");
    return 1;
  }
  std::vector<Scene> scenes(SCENES, SCENES + sizeof(SCENES) / sizeof(SCENES[0]));
  static char liveLine[1024];
  if (argc >= 5 && strcmp(argv[4], "live") == 0) {
    if (!fgets(liveLine, sizeof(liveLine), stdin) || strncmp(liveLine, "CLAUDE ", 7) != 0) {
      fprintf(stderr, "live: expected a CLAUDE line on stdin\n");
      return 1;
    }
    liveLine[strcspn(liveLine, "\r\n")] = 0;
    scenes = { { "live-context", 0, liveLine + 7, 25, 0, 13, "", 0 },
               { "live-limits", 1, liveLine + 7, 25, 0, 13, "", 0 },
               { "live-session", 2, liveLine + 7, 25, 0, 13, "", 0 } };
  }
  for (const Scene &s : scenes) {
    char path[512];
    snprintf(path, sizeof(path), "%s/p%03d.bin", mons.c_str(), s.dex);
    static PmdMon pet;  // big (palettes): keep it off the stack
    if (pet.blob) free(pet.blob);
    pet = PmdMon();
    bool havePet = loadTpk2(path, pet);
    ClaudeMon mon;
    gMillis = 0;  // orbit step 0: the previews show the dashboard un-shifted
    if (s.line) mon.feed(s.line, gMillis);
    ClaudeView view = { s.page, thumb.empty() ? nullptr : thumb.data(), s.petMsg };
    ClaudeScene sc = { s.biome, s.hour < 6 || s.hour >= 20, s.hour, false, havePet ? &pet : nullptr,
                       thumb.empty() ? nullptr : thumb.data() };
    int frames = s.gifFrames > 0 ? s.gifFrames : 1;
    for (int k = 0; k < frames; k++) {
      gMillis = k * 100;  // 10 fps, as the board on the pet screen; frame 0 = every animation's first frame
      ClaudeFrame f;
      claudeBuildFrame(mon, view, gMillis, f);
      claudeDraw(&canvas, f, sc, gMillis, 233, 233, true);
      std::string p = out + "/" + s.name;
      if (s.gifFrames > 0) {
        char suf[16];
        snprintf(suf, sizeof(suf), ".%02d", k);
        p += suf;
      }
      p += ".rgb565";
      FILE *fo = fopen(p.c_str(), "wb");
      if (!fo) { perror(p.c_str()); return 1; }
      fwrite(canvas.getFramebuffer(), 2, 466 * 466, fo);
      fclose(fo);
      // the still for an animated scene: its first frame
      if (s.gifFrames > 0 && k == 0) {
        std::string still = out + "/" + s.name + ".rgb565";
        FILE *fs = fopen(still.c_str(), "wb");
        fwrite(canvas.getFramebuffer(), 2, 466 * 466, fs);
        fclose(fs);
      }
    }
    printf("%s%s\n", s.name, havePet ? "" : " (no sprite)");
  }
  return 0;
}
