#include "render/neon_scene.h"

#include <cmath>
#include <cstring>

#include "render/neon_art.h"

namespace rcpaper {
namespace {

constexpr bool ink = true, light = false;
constexpr int base = 130, street = 266, counter = 244;

struct Pen {
  NeonBitmap& bits;

  void pixel(int x, int y, bool color) {
    if (unsigned(x) >= kNeonWidth || unsigned(y) >= kNeonHeight) return;
    auto& byte = bits[y * (kNeonWidth / 8) + x / 8];
    const uint8_t mask = 0x80 >> (x % 8);
    if (color) byte |= mask;
    else byte &= ~mask;
  }

  void rect(int x, int y, int w, int h, bool color) {
    for (int j = 0; j < h; ++j)
      for (int i = 0; i < w; ++i) pixel(x + i, y + j, color);
  }

  void border(int x, int y, int w, int h, int t, bool color) {
    rect(x, y, w, t, color);
    rect(x, y + h - t, w, t, color);
    rect(x, y, t, h, color);
    rect(x + w - t, y, t, h, color);
  }

  void sign(int x, int y, int w, int h, bool lit, int t = 1) {
    rect(x, y, w, h, ink);
    if (lit) {
      border(x, y, w, h, t, light);
      return;
    }
    for (int i = 1; i < w; i += 2) {
      pixel(x + i, y, light);
      pixel(x + i, y + h - 1, light);
    }
    for (int i = 1; i < h; i += 2) {
      pixel(x, y + i, light);
      pixel(x + w - 1, y + i, light);
    }
  }

  void dither(int x, int y, int w, int h, bool color) {
    for (int j = 0; j < h; ++j)
      for (int i = j % 2; i < w; i += 2) pixel(x + i, y + j, color);
  }

  void disc(int cx, int cy, int r, bool color) {
    for (int y = -r; y <= r; ++y)
      for (int x = -r; x <= r; ++x)
        if (x*x + y*y <= r*r + r/2) pixel(cx + x, cy + y, color);
  }

  template<class T, size_t H>
  void bitmap(const T (&rows)[H], int w, int x, int y, int scale, bool color) {
    for (size_t j = 0; j < H; ++j)
      for (int i = 0; i < w; ++i)
        if ((rows[j] >> (w - 1 - i)) & 1)
          rect(x + scale*i, y + scale*j, scale, scale, color);
  }

  void glyph(char c, int x, int y, int scale, bool color) {
    if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    const char* g = std::strchr(neon_art::kGlyphs, c);
    if (!g || !c) g = std::strchr(neon_art::kGlyphs, '?');
    bitmap(neon_art::kFont[g - neon_art::kGlyphs], 5, x, y, scale, color);
  }

  void text(const std::string& s, int x, int y, int scale, bool color) {
    for (char c : s) {
      glyph(c, x, y, scale, color);
      x += 6 * scale;
    }
  }
};

int text_width(const std::string& s) {
  return static_cast<int>(s.size()) * 12 - 2;
}

// Mulberry32, seeded independently for each part of the scene as in the mock.
struct Random {
  uint32_t seed;
  int below(unsigned limit) {
    uint32_t t = seed += 0x6D2B79F5u;
    t = (t ^ (t >> 15)) * (1u | t);
    t ^= t + (t ^ (t >> 7)) * (61u | t);
    return (uint64_t(t ^ (t >> 14)) * limit) >> 32;
  }
};

void sky(Pen& p, const NeonView& v, bool paper, uint32_t frame) {
  const bool fg = paper;
  Random stars{0x51A7C471};
  for (int i = 0; i < (paper ? 30 : 80); ++i) {
    int x = stars.below(400), y = stars.below(112);
    if (y >= 2 && !(x >= 108 && x < 292 && y >= 32) &&
        (x-356)*(x-356) + (y-34)*(y-34) >= 19*19) p.pixel(x, y, fg);
  }
  p.rect(6, 2, 2, base, fg);
  p.rect(0, 8, 14, 1, fg);
  p.rect(0, 16, 14, 1, fg);
  for (int x = 14; x < 400; ++x) {
    const float sag = std::sin((x - 14) * (3.14159265f / 386));
    p.pixel(x, 9 + std::lround(sag * 14), fg);
    p.pixel(x, 17 + std::lround(sag * 20), fg);
  }
  Random rain{99};
  for (int i = 0; i < (frame ? 70 : 22); ++i) {
    const int rx = rain.below(400), speed = 2 + rain.below(2);
    const int len = 5 + rain.below(4), phase = rain.below(400);
    const int y = (phase + (frame % 112) * speed) % 112;
    const int x = (rx + 400 - (frame / 2) % 400) % 400;
    for (int k = 0; k < len && y + k < 112; ++k) {
      const int xx = x - k/2;
      if (xx >= 112 && xx < 288 && y+k >= 36 && y+k < 108) continue;
      p.pixel(xx, y+k, fg);
    }
  }
  p.disc(356, 34, 13, fg);
  if (paper) p.disc(356, 34, 11, light);
  p.rect(351, 30, 3, 3, fg);
  p.rect(360, 38, 3, 3, fg);
  p.rect(352, 41, 2, 2, fg);

  for (size_t i = 0; i < v.skyline_bars.size(); ++i) {
    const int x = 8 + 49 * i;
    const int h = 8 + (40 * v.skyline_bars[i] + kStationSkylineMaxBarH/2) / kStationSkylineMaxBarH;
    const int y = base - h;
    p.rect(x, y, 44, h, ink);
    if (!paper)
      for (int j = 0; j < h; j += 2)
        for (int k = 0; k < 44; k += 2) p.pixel(x+k, y+j, light);
    Random windows{100u + static_cast<unsigned>(i)};
    for (int wy = y+5; wy < base-3; wy += 6)
      for (int wx = x+4; wx < x+41; wx += 6)
        if (!windows.below(2)) p.rect(wx, wy, 2, 2, light);
    if (i % 3 == 1 && h > 20) p.rect(x+8, y+3, 10, 3, light);
  }
}

void billboard(Pen& p, const NeonView& v, bool fg, bool colon) {
  p.rect(140, 104, 3, 26, fg);
  p.rect(252, 104, 3, 26, fg);
  for (int k = 0; k < 10; ++k) {
    p.pixel(144 + k*22/10, 106 + 2*k, fg);
    p.pixel(250 - (k*22+9)/10, 106 + 2*k, fg);
  }
  p.sign(116, 40, 168, 64, true, 2);
  p.dither(114, 38, 172, 2, fg);
  p.dither(114, 104, 172, 2, fg);
  p.dither(114, 40, 2, 64, fg);
  p.dither(284, 40, 2, 64, fg);
  p.text(v.clock_hhmm.substr(0, 2), 136, 48, 5, light);
  p.text(v.clock_hhmm.substr(3, 2), 213, 48, 5, light);
  if (colon) {
    p.rect(200, 56, 5, 5, light);
    p.rect(200, 70, 5, 5, light);
  }
  std::string caption = v.env_temp + '\xB0';
  if (v.day) {
    const auto date = v.weekday + " " + std::to_string(v.day);
    const int gap = text_width(date + "  " + caption) <= 160 ? 2 : 1;
    caption.insert(0, date + std::string(gap, ' '));
  }
  p.text(caption, 200 - text_width(caption)/2, 86, 2, light);
}

bool occupied(int x, int y) {
  constexpr int zones[][4] = {{6,136,96,240}, {306,132,400,164}, {310,162,354,184},
                              {370,162,400,252}, {308,200,350,266}, {252,194,308,266},
                              {88,180,246,268}};
  for (const auto& z : zones)
    if (x >= z[0] && y >= z[1] && x < z[2] && y < z[3]) return true;
  return false;
}

void fuel(Pen& p, const NeonView& v, bool paper, uint32_t frame) {
  const bool lit = !v.outage;
  p.border(7, 137, 88, 102, 3, ink);
  p.sign(10, 140, 82, 96, lit, 2);
  if (lit) {
    if (!paper) {
      p.dither(6, 136, 90, 1, light);
      p.dither(6, 240, 90, 1, light);
      p.dither(6, 136, 1, 104, light);
      p.dither(96, 136, 1, 104, light);
    }
    p.text(frame % 53 >= 50 ? "F EL" : "FUEL", 16, 146, 2, light);
    p.rect(16, 164, 60, 1, light);
  }
  for (size_t i = 0; i < v.providers.size(); ++i) {
    const auto& row = v.providers[i];
    const int y = 170 + 16*i;
    const int fill = (row.left * 46 + 50) / 100;
    p.sign(32, y+2, 48, 10, lit);
    if (!lit) continue;
    p.glyph(row.id_initial, 16, y, 2, light);
    if (row.left < 0) {
      for (int x : {40,45,50}) p.pixel(x, y+7, light);
    } else if (!row.low_fuel || frame % 4 < 2) {
      p.rect(33, y+3, fill, 8, light);
    } else if (fill == 0) {
      // Blink the outline when the bar has no fill pixels to blink.
      p.border(32, y+2, 48, 10, 1, ink);
    }
    if (row.low_fuel) p.glyph('!', 84, y+3, 1, light);
  }
}

void right_wall(Pen& p, const NeonView& v, uint32_t frame) {
  const bool lit = !v.outage;
  p.rect(310, 134, 86, 30, ink);
  p.rect(312, 136, 83, 1, light);
  for (int x : {324,350,376}) {
    p.rect(x, 137, 1, 6, light);
    if (lit) {
      p.rect(x-6, 144, 12, 14, light);
      for (int y : {144,148,152,156}) p.rect(x-6, y, 12, y == 144 || y == 156 ? 2 : 1, ink);
      p.rect(x-1, 161, 3, 3, light);
    } else p.sign(x-6, 144, 12, 14, false);
  }
  p.rect(313, 163, 42, 18, ink);
  if (!lit || !v.battery_low || frame % 8 < 4) {
    p.sign(316, 166, 28, 12, lit);
    if (lit) {
      p.rect(344, 169, 2, 6, light);
      p.rect(318, 168, (v.battery_pct*24+50)/100, 8, light);
      if (v.battery_low) p.glyph('!', 349, 168, 1, light);
    }
  }
  p.border(371, 163, 26, 90, 3, ink);
  p.sign(374, 166, 20, 84, lit);
  if (lit)
    for (int i = 0; i < 4; ++i)
      if (i != 2 || frame % 37 < 35) p.glyph("OPEN"[i], 379, 171 + i*20, 2, light);
  // Vending machine and noren doorway stay lit during an outage.
  p.rect(311, 203, 36, 63, ink);
  p.rect(315, 207, 28, 58, light);
  p.rect(317, 210, 24, 24, ink);
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 4; ++c)
      if ((r+c) % 3 != 1) p.rect(319+c*6, 212+r*8, 3, 5, light);
  p.rect(317, 238, 24, 3, ink);
  p.rect(317, 246, 12, 8, ink);
  p.rect(332, 246, 9, 3, ink);
  p.rect(317, 259, 24, 4, ink);
  p.border(255, 193, 50, 22, 3, ink);
  p.sign(258, 196, 44, 16, lit);
  if (lit) p.text("24H", 263, 197, 2, light);
  p.rect(261, 215, 34, 51, ink);
  p.rect(265, 227, 26, 39, light);
  p.rect(277, 227, 2, 39, ink);
  for (int x : {272,279,286}) p.rect(x, 219, 2, 7, light);
}

void stall(Pen& p, const NeonView& v, uint32_t frame) {
  p.border(92, 182, 150, 86, 2, ink);
  for (int x = 94; x < 240; x += 8) {
    const bool stripe = ((x-94)/8) % 2;
    p.rect(x, 184, 8, 10, stripe);
    p.disc(x+4, 194, 4, stripe);
  }
  p.rect(104, 200, 126, 48, light);
  for (int x : {150,184}) {
    p.rect(x, 200, 1, 6, ink);
    p.rect(x-5, 206, 11, 4, ink);
    p.rect(x-3, 210, 7, 1, ink);
  }
  p.bitmap(neon_art::kWorker, 42, 106, 200, 1, ink);
  p.border(182, 238, 6, 6, 1, ink);
  p.rect(189, 240, 1, 2, ink);
  p.pixel(183, 235, ink);
  p.pixel(186, 231, ink);
  p.pixel(183, 227, ink);
  if (v.cat_enabled)
    p.bitmap(frame % 8 < 4 ? neon_art::kCatA : neon_art::kCatB, 15, 200, 224, 2, ink);
  p.rect(104, counter, 126, 3, ink);
  p.rect(94, counter+4, 146, 18, ink);
  if (v.outage) p.sign(100, 250, 132, 14, false);
  else p.text("ODO " + v.total_odo, 100, 250, 2, light);
}

void wet_street(Pen& p, const NeonView& v, bool fg, uint32_t frame) {
  const int bottom = v.footer_chip.empty() ? 300 : 280;
  p.rect(0, street, 400, 34, !fg);
  for (int y = street+2; y < bottom-1; y += 3)
    for (int x = 0; x < 400; ++x)
      if ((x*3 + (y % 6)*2) % 15 < (fg ? 12 : 9)) p.pixel(x, y, fg);
  p.rect(0, street, 400, 2, fg);
  auto streak = [&](int x, int w, int len) {
    for (int d = 0; d < len && street+2+d < bottom-1; ++d) {
      if (d < 6 || (d-6) % (3 + (d-6)/8) < 2)
        p.rect(x + (frame ? ((x+d+frame) & 1) : 0), street+2+d, w, 1, light);
    }
  };
  for (int x : {150,182,222,252}) streak(x-3, 6, 40);
  if (!v.outage) {
    for (int x : {24,48,72}) streak(x-2, 5, 26);
    streak(316, 4, 14);
    for (int x : {324,350,376}) streak(x-3, 6, 22);
    streak(378, 8, 24);
  }
  for (int x : {270,283}) streak(x, 4, 30);
  for (int x : {320,334}) streak(x-3, 6, 18);
  for (int x : {118,150,204}) streak(x, 8, 30);
  if (bottom == 300) {
    for (int y = -4; y <= 4; ++y)
      for (int x = -30; x <= 30; ++x)
        if (16*x*x + 900*y*y <= 15120)
          p.pixel(150+x, 287+y, 9*x*x + 784*y*y > 7408 ? fg : !fg);
    streak(147, 6, 40);
  }
  p.rect(298, 270, 22, 6, !fg);
  p.border(298, 270, 22, 6, 1, fg);
  for (int k = 2; k < 20; k += 3) p.rect(298+k, 271, 1, 4, fg);
  if (!v.footer_chip.empty()) {
    const int w = text_width(v.footer_chip) + 10;
    p.rect(8, 282, w, 16, ink);
    p.text(v.footer_chip, 13, 283, 2, light);
    p.text(v.footer_text, 8+w+8, 283, 2, fg);
  }
}

}  // namespace

void render_neon_scene(const NeonView& v, MonoPolarity polarity, uint32_t frame, NeonBitmap& out) {
  const bool paper = polarity == MonoPolarity::Light;
  out.fill(paper ? 0 : 0xff);
  Pen p{out};
  sky(p, v, paper, frame);
  p.dither(0, base, 400, street-base, paper);
  Random windows{7};
  for (int y = 136; y < street-8; y += 13)
    for (int x = 8; x < 392; x += 12)
      if (!occupied(x,y) && !occupied(x+4,y+5)) p.rect(x, y, 4, 5, windows.below(100) >= 45);
  fuel(p, v, paper, frame);
  right_wall(p, v, frame);
  stall(p, v, frame);
  billboard(p, v, paper, frame % 8 < 4);
  wet_street(p, v, paper, frame);
}

}  // namespace rcpaper
