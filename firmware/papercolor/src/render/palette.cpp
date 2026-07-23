#include "render/palette.h"
#include <cstring>

#ifdef ARDUINO
#include <M5GFX.h>
#endif

namespace rcpaper {

bool color_from_token(const char* token, Color* out) {
  if (!token || !*token || !out) return false;
  // Concrete E6 inks.
  if (std::strcmp(token, "black")  == 0) { *out = Color::Black;  return true; }
  if (std::strcmp(token, "white")  == 0) { *out = Color::White;  return true; }
  if (std::strcmp(token, "red")    == 0) { *out = Color::Red;    return true; }
  if (std::strcmp(token, "yellow") == 0) { *out = Color::Yellow; return true; }
  if (std::strcmp(token, "green")  == 0) { *out = Color::Green;  return true; }
  if (std::strcmp(token, "blue")   == 0) { *out = Color::Blue;   return true; }
  // Semantic tokens (DSL v1) — s3 has the full 6-ink palette so each maps to its natural ink.
  if (std::strcmp(token, "accent")  == 0) { *out = Color::Red;    return true; }
  if (std::strcmp(token, "warning") == 0) { *out = Color::Yellow; return true; }
  if (std::strcmp(token, "success") == 0) { *out = Color::Green;  return true; }
  if (std::strcmp(token, "info")    == 0) { *out = Color::Blue;   return true; }
  if (std::strcmp(token, "muted")   == 0) { *out = Color::Black;  return true; }
  return false;
}

const char* color_to_token(Color c) {
  switch (c) {
    case Color::Black:  return "black";
    case Color::White:  return "white";
    case Color::Red:    return "red";
    case Color::Yellow: return "yellow";
    case Color::Green:  return "green";
    case Color::Blue:   return "blue";
  }
  return "black";
}

uint32_t color_to_rgb(Color c) {
  switch (c) {
    case Color::Black:  return kE6Black;
    case Color::White:  return kE6White;
    case Color::Red:    return kE6Red;
    case Color::Yellow: return kE6Yellow;
    case Color::Green:  return kE6Green;
    case Color::Blue:   return kE6Blue;
  }
  return kE6Black;
}

static uint32_t distance_sq(uint8_t r, uint8_t g, uint8_t b, uint32_t rgb) {
  const int32_t dr = (int32_t)r - (int32_t)((rgb >> 16) & 0xFF);
  const int32_t dg = (int32_t)g - (int32_t)((rgb >> 8) & 0xFF);
  const int32_t db = (int32_t)b - (int32_t)(rgb & 0xFF);
  return (uint32_t)(dr * dr + dg * dg + db * db);
}

uint32_t quantize_rgb_to_e6(uint8_t r, uint8_t g, uint8_t b) {
  static constexpr uint32_t colors[] = {
    kE6Black,
    kE6White,
    kE6Red,
    kE6Yellow,
    kE6Green,
    kE6Blue,
  };
  uint32_t best = colors[0];
  uint32_t best_d = distance_sq(r, g, b, best);
  for (uint8_t i = 1; i < sizeof(colors) / sizeof(colors[0]); ++i) {
    const uint32_t d = distance_sq(r, g, b, colors[i]);
    if (d < best_d) {
      best = colors[i];
      best_d = d;
    }
  }
  return best;
}

#ifdef ARDUINO
uint32_t color_to_m5gfx(Color c) {
  return color_to_rgb(c);
}
#endif

}  // namespace rcpaper
