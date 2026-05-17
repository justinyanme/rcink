#include "render/palette.h"
#include <cstring>

#ifdef ARDUINO
#include <M5GFX.h>
#endif

namespace rcpaper {

Color color_from_token(const char* token) {
  if (!token || !*token) return Color::Black;
  if (std::strcmp(token, "black")  == 0) return Color::Black;
  if (std::strcmp(token, "white")  == 0) return Color::White;
  if (std::strcmp(token, "red")    == 0) return Color::Red;
  if (std::strcmp(token, "yellow") == 0) return Color::Yellow;
  if (std::strcmp(token, "green")  == 0) return Color::Green;
  if (std::strcmp(token, "blue")   == 0) return Color::Blue;
  return Color::Black;
}

#ifdef ARDUINO
// M5GFX provides 24-bit RGB; the Spectra 6 panel quantises to the nearest of
// its 6 inks internally. Using canonical RGB anchors gives a stable mapping.
uint32_t color_to_m5gfx(Color c) {
  switch (c) {
    case Color::Black:  return 0x000000;
    case Color::White:  return 0xFFFFFF;
    case Color::Red:    return 0xD92027;
    case Color::Yellow: return 0xF7C815;
    case Color::Green:  return 0x1EA84B;
    case Color::Blue:   return 0x0F4D92;
  }
  return 0x000000;
}
#endif

}  // namespace rcpaper
