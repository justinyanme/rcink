#include "render/fonts.h"

namespace rcpaper {

int16_t font_height(TextSize size, Weight /*weight*/) {
  switch (size) {
    case TextSize::H1:   return 52;
    case TextSize::H2:   return 26;
    case TextSize::Body: return 16;
    case TextSize::Sm:
    default:             return 8;
  }
}

#ifdef ARDUINO

void select_font(TFT_eSprite& canvas, TextSize size, Weight /*weight*/) {
  canvas.setTextSize(1);
  switch (size) {
    case TextSize::H1:
      canvas.setTextFont(4);
      canvas.setTextSize(2);
      break;
    case TextSize::H2:
      canvas.setTextFont(4);
      break;
    case TextSize::Body:
      canvas.setTextFont(2);
      break;
    case TextSize::Sm:
    default:
      canvas.setTextFont(1);
      break;
  }
}

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

int16_t text_width(TFT_eSprite& canvas, const TextNode& text, const char* value) {
  select_font(canvas, text.size, text.weight);
  return canvas.textWidth(value) + 2 * text.pad;
}

#endif  // ARDUINO

}  // namespace rcpaper
