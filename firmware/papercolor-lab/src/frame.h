// Frame encoders: pure functions from ink-code planes to controller bytes.
#pragma once

#include <cstddef>
#include <cstdint>

namespace rclab {

constexpr unsigned kPanelWidth = 400;
constexpr unsigned kPanelHeight = 600;
constexpr size_t kPixels = size_t(kPanelWidth) * kPanelHeight;
constexpr size_t kNativeRowBytes = kPanelWidth / 2;
constexpr size_t kNativeFrameBytes = kNativeRowBytes * kPanelHeight;

// Panel ink codes, as pinned Panel_ED2208.cpp names them (EPD_* constants).
enum Ink : uint8_t { InkBlack = 0x0, InkWhite = 0x1, InkYellow = 0x2, InkRed = 0x3, InkBlue = 0x5, InkGreen = 0x6 };

// Native packing: two ink codes per byte, left pixel in the high nibble.
// `count` is even; writes count/2 bytes.
void encode_native_row(const uint8_t* inks, size_t count, uint8_t* out);

// Whole plane: `inks` has kPixels codes, `out` receives kNativeFrameBytes.
void encode_native_frame(const uint8_t* inks, uint8_t* out);

}  // namespace rclab
