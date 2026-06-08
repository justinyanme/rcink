#include "render/fonts.h"

namespace rcpaper {

// Pixel heights tuned for the 536x240 landscape bar (only ~188px usable after
// the top/bottom strips). H1 is deliberately modest — a 52px header ate a
// quarter of the screen and overflowed the dense dashboard pages. CJK glyphs
// render at 16px regardless (see kCjkLineH). Mirror in producer
// devices_profile._T_AMOLED_SIZES.
int16_t font_height(TextSize size, Weight /*weight*/) {
  switch (size) {
    case TextSize::H1:   return 32;
    case TextSize::H2:   return 26;
    case TextSize::Body: return 16;
    case TextSize::Sm:
    default:             return 8;
  }
}

#ifdef ARDUINO

#include "render/fonts/wqy16_gb2312.h"

namespace {
U8g2_for_TFT_eSPI g_u8f;
bool g_u8f_ready = false;
float g_text_scale = 1.0f;

// One baked CJK face serves every text size; u8g2 GB2312 bitmaps top out near
// 16px, so larger headers render Chinese at 16px inside their taller row (the
// same fixed-size compromise firmware/s3 makes with efontCN). The 16px floor
// is also the smallest CJK can scale to.
const uint8_t* cjk_font_for(TextSize /*size*/, Weight /*weight*/) {
  return u8g2_font_wqy16_t_gb2312;
}

// Discrete built-in font ladder (GLCD + Font2 + Font4 with setTextSize
// multipliers). The fit-to-height pass steps Latin text down this ladder by
// picking the tallest entry whose height fits the scaled target px.
struct FontPick { uint8_t font; uint8_t mult; int16_t height; };
constexpr FontPick kLadder[] = {
  {1, 1, 8},   // GLCD
  {2, 1, 16},  // Font2
  {1, 3, 24},  // GLCD x3
  {4, 1, 26},  // Font4
  {2, 2, 32},  // Font2 x2
};

FontPick pick_font(int16_t target_px) {
  FontPick best = kLadder[0];
  for (const auto& f : kLadder) {
    if (f.height <= target_px) best = f;
  }
  return best;
}

int16_t scaled_target_px(TextSize size) {
  int16_t px = (int16_t)(font_height(size, Weight::Regular) * g_text_scale + 0.5f);
  return px < 1 ? 1 : px;
}
}  // namespace

void fonts_begin(TFT_eSprite& canvas) {
  g_u8f.begin(canvas);
  g_u8f_ready = true;
}

void set_text_scale(float s) { g_text_scale = s; }

int16_t scale_px(int16_t v) { return (int16_t)(v * g_text_scale + 0.5f); }

int16_t text_line_height(TextSize size) { return pick_font(scaled_target_px(size)).height; }

static void select_font(TFT_eSprite& canvas, TextSize size, Weight /*weight*/) {
  const FontPick p = pick_font(scaled_target_px(size));
  canvas.setTextFont(p.font);
  canvas.setTextSize(p.mult);
}

bool string_needs_cjk(const std::string& s) {
  for (unsigned char c : s) {
    if (c >= 0x80) return true;
  }
  return false;
}

// Strip multi-byte UTF-8 to '?' so the ASCII-only built-in fonts never emit
// garbage. Only reached on the non-CJK path, where it is effectively a no-op.
std::string printable_text(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size();) {
    const unsigned char c = (unsigned char)in[i];
    if (c < 0x80) {
      out.push_back((char)c);
      ++i;
      continue;
    }
    size_t step = (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
    if (i + step > in.size()) step = 1;
    out.push_back('?');
    i += step;
  }
  return out;
}

int16_t measured_width(TFT_eSprite& canvas, TextSize size, Weight weight,
                       const std::string& s) {
  if (g_u8f_ready && string_needs_cjk(s)) {
    g_u8f.setFont(cjk_font_for(size, weight));
    return (int16_t)g_u8f.getUTF8Width(s.c_str());
  }
  select_font(canvas, size, weight);
  return canvas.textWidth(printable_text(s).c_str());
}

void draw_utf8(TFT_eSprite& canvas, int16_t x, int16_t y_top, TextSize size,
               Weight weight, uint16_t fg, bool opaque, uint16_t bg,
               const std::string& s) {
  if (g_u8f_ready && string_needs_cjk(s)) {
    g_u8f.setFont(cjk_font_for(size, weight));
    g_u8f.setFontMode(1);  // transparent: caller pre-fills any background
    g_u8f.setForegroundColor(fg);
    const int16_t baseline = y_top + g_u8f.getFontAscent();
    g_u8f.drawUTF8(x, baseline, s.c_str());
    return;
  }
  select_font(canvas, size, weight);
  canvas.setTextWrap(false, false);
  if (opaque) canvas.setTextColor(fg, bg);
  else        canvas.setTextColor(fg);
  canvas.setCursor(x, y_top);
  canvas.print(printable_text(s).c_str());
}

#endif  // ARDUINO

}  // namespace rcpaper
