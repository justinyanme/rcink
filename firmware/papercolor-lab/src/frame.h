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

// --- TICKEY mono (sf32rc ce0a01a v2/src/tickey_mono.c) ---
// Old plane: 1 bit per pixel (1 = light), row-major, MSB first.
constexpr size_t kMonoPlaneBytes = kPixels / 8;

// tickey_mono.c:21-25: white or yellow is light; every other ink is dark.
bool mono_light(uint8_t ink);
// Transition nibble (old << 1) | light(new), two pixels per byte, left pixel
// high (tickey_mono.c:27-33, 64-69). `old_plane` nullptr = all-zero history.
// `count` pixels from index 0; count is even; writes count/2 bytes.
void encode_mono(const uint8_t* inks, const uint8_t* old_plane, size_t count, uint8_t* out);
// The light plane of `inks` (the next drive's old plane); writes count/8 bytes.
void mono_plane(const uint8_t* inks, size_t count, uint8_t* plane);

// --- TICKEY staged colour (sf32rc v2/src/tickey_color.c) ---
enum class Palette : uint8_t { Fast, Quality };

// Ink -> 6-bit encoded byte: sf32rc tools/v2_tickey_color.py encode() applied
// to sf32rc's own ink RGB (tools/v2_image.py PALETTE), no dither; exact black
// pins to code 3 (v2_tickey_color.py:90-96).
uint8_t staged_code(uint8_t ink, Palette palette);
// Phase selector: phase 0 (high) = code >> 3, phase 1 (low) = code & 7.
uint8_t staged_selector(uint8_t code, bool high);
// One phase plane: selector nibbles, two pixels per byte, left pixel high.
void encode_staged(const uint8_t* inks, size_t count, Palette palette, bool high, uint8_t* out);

}  // namespace rclab
