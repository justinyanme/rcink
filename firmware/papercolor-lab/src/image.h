// Built-in Lab images: pure renderers into a kPixels one-byte-per-pixel
// ink-code plane (row-major, 400x600, x right, y down). No hardware.
#pragma once

#include <cstddef>
#include <cstdint>

#include "frame.h"

namespace rclab {

enum class FontSize : uint8_t { Pt13, Pt16, Pt18, Pt24 };

void render_fill(uint8_t* inks, Ink ink);
// The 2026-10-13 chart (production src/dev/waveform_trial.cpp pixel()):
// 4 px black border; y<300 six 200x100 patches black|white, yellow|red,
// blue|green; y 300..499 four 50 px black->ink RGB ramps (yellow, red, blue,
// green) quantised to the nearest ink; y>=500 20 px black/white checker.
void render_chart(uint8_t* inks);
// Squares of `size` px; the square at (0,0) is black.
void render_checker(uint8_t* inks, unsigned size);
// One block per VT323 size (13/16/18/24 pt): A-Z a-z 0-9 black on white,
// wrapped at the 8 px margins; then one 16 pt line white on a black band.
void render_text(uint8_t* inks);
// Eight full-width bands of 75 px; band i holds value i (0..7). Values 4 and 7
// are not panel inks: for custom frame=raw, where the value selects LUT row i+1.
void render_bands(uint8_t* inks);
// Black <-> white; chromatic inks unchanged.
void invert(uint8_t* inks, size_t count);

// Draws `text` with its baseline at y, starting at x; returns the end x.
// Pixels outside the panel are dropped.
unsigned draw_text(uint8_t* inks, FontSize size, unsigned x, unsigned y, const char* text, Ink ink);
unsigned line_height(FontSize size);

}  // namespace rclab
