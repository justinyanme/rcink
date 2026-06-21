// Standalone SSD1677 bring-up sketch. Validates the vendored EInkDisplay driver
// on the real Xteink X4 panel, in isolation from WiFi/render. Whatever shows on
// the panel confirms: SPI pins, init sequence, orientation, full + fast refresh.
//
//   pio run -e c3-x4-bringup -t upload --upload-port /dev/cu.usbmodem101
//
// Only compiled into the `c3-x4-bringup` env (see build_src_filter).
#include <Arduino.h>
#include <EInkDisplay.h>
#include <cstring>

#include "display/pins_config.h"

static EInkDisplay epd(EPD_SCLK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY);

// 1bpp framebuffer: 0xFF = white, bit cleared = black ink, MSB-first, row-major.
static inline void ink(uint8_t* fb, uint16_t stride, int x, int y, int w, int h) {
  if (x < 0 || y < 0 || x >= w || y >= h) return;
  fb[y * stride + (x >> 3)] &= ~(uint8_t)(0x80 >> (x & 7));
}

static void draw_test_pattern() {
  uint8_t* fb = epd.getFrameBuffer();
  const int W = epd.getDisplayWidth();
  const int H = epd.getDisplayHeight();
  const uint16_t stride = epd.getDisplayWidthBytes();
  std::memset(fb, 0xFF, epd.getBufferSize());

  // 4px black border — proves full-screen coverage and edge alignment.
  for (int x = 0; x < W; ++x)
    for (int t = 0; t < 4; ++t) { ink(fb, stride, x, t, W, H); ink(fb, stride, x, H - 1 - t, W, H); }
  for (int y = 0; y < H; ++y)
    for (int t = 0; t < 4; ++t) { ink(fb, stride, t, y, W, H); ink(fb, stride, W - 1 - t, y, W, H); }

  // Diagonal from top-left — proves orientation (origin + axis direction).
  for (int i = 0; i < W && i < H; ++i) { ink(fb, stride, i, i, W, H); ink(fb, stride, i + 1, i, W, H); }

  // Solid black block in the top-left quadrant, with a white square punched
  // out of its centre — proves ink polarity (block black, hole white).
  for (int y = 60; y < 220; ++y)
    for (int x = 60; x < 380; ++x) ink(fb, stride, x, y, W, H);
  for (int y = 110; y < 170; ++y)
    for (int x = 180; x < 260; ++x) fb[y * stride + (x >> 3)] |= (uint8_t)(0x80 >> (x & 7));
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n[bringup] Xteink X4 SSD1677 bring-up");
  epd.begin();
  Serial.printf("[bringup] %ux%u stride=%u buf=%uB free_heap=%u\n",
                epd.getDisplayWidth(), epd.getDisplayHeight(),
                epd.getDisplayWidthBytes(), (unsigned)epd.getBufferSize(),
                (unsigned)ESP.getFreeHeap());

  epd.clearScreen(0xFF);
  epd.displayBuffer(EInkDisplay::FULL_REFRESH);
  Serial.println("[bringup] white full-refresh done");

  draw_test_pattern();
  epd.displayBuffer(EInkDisplay::FULL_REFRESH);
  Serial.println("[bringup] test pattern full-refresh done");
}

void loop() {
  static uint32_t n = 0;
  delay(5000);
  Serial.printf("[bringup] alive #%u heap=%u\n", (unsigned)++n, (unsigned)ESP.getFreeHeap());
}
