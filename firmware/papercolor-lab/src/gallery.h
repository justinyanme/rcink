// Demo gallery: one page per own-waveform capability (README "Demo gallery").
// Pure page renderers and the drive each page uses; demo.cpp drives them.
#pragma once

#include <cstddef>
#include <cstdint>

namespace rclab::gallery {

// How a plane reaches the panel. Plane values per drive:
// CleanMono, FastMono, Color, Native: panel Ink codes.
// Grid: 0..63, base = v & 7 (kGridBase order), modifier = v >> 3.
enum class Drive : uint8_t { CleanMono, FastMono, Color, Grid, Native };

// Grid bases (tools/own_grid.py BASES order).
enum GridBase : uint8_t { GridWhite, GridBlack, GridRed, GridBlue, GridYellow, GridGreen, GridPink, GridOrange };

struct Page {
  const char* name;  // console/footer token, no spaces
  Drive enter;       // drive when the page is opened
  Drive action;      // drive for button C (`demo act`): step > 0
  // Renders the page after `step` actions (0 = just opened) into kPixels values.
  void (*render)(uint8_t* plane, unsigned step);
};

extern const Page kPages[];
constexpr unsigned kPageCount = 7;

const char* drive_name(Drive drive);

}  // namespace rclab::gallery
