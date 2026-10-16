// Demo pages against their drive's value contract (gallery.h Drive): a value
// outside it renders as some other ink (mono map sends colour to black, colour
// map sends 4/7 to black) or overflows the grid fields.
#include <unity.h>

#include <vector>

#include "frame.h"
#include "gallery.h"

using namespace rclab;
using namespace rclab::gallery;

static std::vector<uint8_t> plane(kPixels);

static bool allowed(Drive d, uint8_t v) {
  switch (d) {
    case Drive::CleanMono:
    case Drive::FastMono: return v == InkBlack || v == InkWhite;
    case Drive::Color:
    case Drive::Native:
      return v == InkBlack || v == InkWhite || v == InkYellow || v == InkRed || v == InkBlue || v == InkGreen;
    case Drive::Grid: return v < 64;
  }
  return false;
}

void test_pages_use_their_drive_values() {
  for (unsigned i = 0; i < kPageCount; ++i) {
    const Page& p = kPages[i];
    for (unsigned step = 0; step < 3; ++step) {
      plane.assign(kPixels, 0xFF);
      p.render(plane.data(), step);
      const Drive d = step ? p.action : p.enter;
      for (uint8_t v : plane) {
        if (!allowed(d, v)) TEST_FAIL_MESSAGE(p.name);
      }
    }
  }
}

// The grid page holds all 64 classes (8 bases x 8 modifiers) as 50 x 70 patches.
void test_grid_page_patches() {
  const Page& grid = kPages[5];
  TEST_ASSERT_EQUAL_STRING("grid48", grid.name);
  grid.render(plane.data(), 0);
  for (unsigned v = 0; v < 64; ++v) {
    const unsigned x = (v % 8) * 50 + 25, y = 40 + (v / 8) * 70 + 35;
    TEST_ASSERT_EQUAL(v, plane[size_t(y) * kPanelWidth + x]);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_pages_use_their_drive_values);
  RUN_TEST(test_grid_page_patches);
  return UNITY_END();
}
