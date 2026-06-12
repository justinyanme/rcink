#ifdef ARDUINO

#include "display/st7305.h"

#include <Arduino.h>
#include <SPI.h>

#include <initializer_list>

#include "display/pins_config.h"
#include "render/mono_pack.h"

namespace rcpaper {
namespace {

// SPI3/HSPI mirrors the vendor demo's SPI3_HOST. 10 MHz is the demo's value;
// it is plenty for a 15 KB full-frame push (~12 ms) at a 30 s cadence and is
// the safest starting point — bump only if HIL proves the panel takes it.
SPIClass g_spi(HSPI);
constexpr uint32_t kSpiHz = 10 * 1000 * 1000;

constexpr int kW = 400;
constexpr int kH = 300;
constexpr int kBufBytes = (kW * kH) / 8;  // 15000
uint8_t g_buf[kBufBytes];

inline void cs_lo() { digitalWrite(ST7305_CS, LOW); }
inline void cs_hi() { digitalWrite(ST7305_CS, HIGH); }

// One command byte (DC low) followed by N data bytes (DC high), CS framing the
// whole thing. Must be called inside a beginTransaction/endTransaction window.
void cmd(uint8_t c, std::initializer_list<uint8_t> data = {}) {
  digitalWrite(ST7305_DC, LOW);
  cs_lo();
  g_spi.transfer(c);
  if (data.size()) {
    digitalWrite(ST7305_DC, HIGH);
    for (uint8_t b : data) g_spi.transfer(b);
  }
  cs_hi();
}

}  // namespace

void st7305_init() {
  pinMode(ST7305_DC, OUTPUT);
  pinMode(ST7305_CS, OUTPUT);
  pinMode(ST7305_RST, OUTPUT);
  cs_hi();
  // miso=-1 (write-only panel), CS managed manually. The "HSPI Does not have
  // default pins on ESP32S3" warning this logs is benign — SCK/MOSI still bind.
  g_spi.begin(ST7305_SCK, -1, ST7305_MOSI, -1);
  g_spi.beginTransaction(SPISettings(kSpiHz, MSBFIRST, SPI_MODE0));

  // Hardware reset pulse.
  digitalWrite(ST7305_RST, HIGH);
  delay(50);
  digitalWrite(ST7305_RST, LOW);
  delay(20);
  digitalWrite(ST7305_RST, HIGH);
  delay(50);

  // ST7305 init — verbatim from Waveshare's ESP32-S3-RLCD-4.2 Arduino demo
  // (08_LVGL_V8_Test/display_bsp.cpp RLCD_Init), 400x300 landscape. The
  // C1/C2/C4/C5 values are gate/source voltages tuned for this exact panel.
  cmd(0xD6, {0x17, 0x02});                    // NVM load control
  cmd(0xD1, {0x01});                          // booster enable
  cmd(0xC0, {0x11, 0x04});                    // gate voltage control
  cmd(0xC1, {0x69, 0x69, 0x69, 0x69});        // VSHP
  cmd(0xC2, {0x19, 0x19, 0x19, 0x19});        // VSLP
  cmd(0xC4, {0x4B, 0x4B, 0x4B, 0x4B});        // VSHN
  cmd(0xC5, {0x19, 0x19, 0x19, 0x19});        // VSLN
  cmd(0xD8, {0x80, 0xE9});
  cmd(0xB2, {0x02});
  cmd(0xB3, {0xE5, 0xF6, 0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45});
  cmd(0xB4, {0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45});
  cmd(0x62, {0x32, 0x03, 0x1F});
  cmd(0xB7, {0x13});
  cmd(0xB0, {0x64});                          // gate-line count
  cmd(0x11);                                  // sleep out
  delay(200);
  cmd(0xC9, {0x00});
  cmd(0x36, {0x48});                          // MADCTL (landscape orientation)
  cmd(0x3A, {0x11});                          // pixel format
  cmd(0xB9, {0x20});
  cmd(0xB8, {0x29});
  cmd(0x21);                                  // display inversion ON
  cmd(0x2A, {0x12, 0x2A});                    // column address window
  cmd(0x2B, {0x00, 0xC7});                    // row/page address window
  cmd(0x35, {0x00});                          // tearing-effect line on
  cmd(0xD0, {0xFF});
  cmd(0x38);                                  // high power (fast) mode
  cmd(0x29);                                  // display on
  g_spi.endTransaction();

  Serial.printf("[st7305] init done %dx%d (%d-byte 1bpp buffer)\n", kW, kH, kBufBytes);
}

void st7305_push_mono(const uint16_t* px, int w, int h) {
  if (w != kW || h != kH) {
    Serial.printf("[st7305] push REJECTED: got %dx%d, expected %dx%d\n", w, h, kW, kH);
    return;
  }
  pack_mono_landscape(px, w, h, g_buf);
  g_spi.beginTransaction(SPISettings(kSpiHz, MSBFIRST, SPI_MODE0));
  cmd(0x2A, {0x12, 0x2A});
  cmd(0x2B, {0x00, 0xC7});
  digitalWrite(ST7305_DC, LOW);  // memory-write command
  cs_lo();
  g_spi.transfer(0x2C);
  digitalWrite(ST7305_DC, HIGH);  // bulk pixel data, CS held low across it
  g_spi.writeBytes(g_buf, kBufBytes);
  cs_hi();
  g_spi.endTransaction();
}

void st7305_sleep() {
  g_spi.beginTransaction(SPISettings(kSpiHz, MSBFIRST, SPI_MODE0));
  cmd(0x28);  // display off
  cmd(0x10);  // sleep in
  g_spi.endTransaction();
}

}  // namespace rcpaper

#endif  // ARDUINO
