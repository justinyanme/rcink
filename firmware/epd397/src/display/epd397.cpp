#ifdef ARDUINO

#include "display/epd397.h"

#include <Arduino.h>
#include <SPI.h>

#include <initializer_list>

#include "display/pins_config.h"
#include "render/gray_pack.h"

namespace rcpaper {
namespace {

// Landscape 800x480. The 4-gray buffer is 2 bits/px (96000 bytes); the SSD1677
// 4-gray waveform writes two 1bpp planes (48000 bytes each) split from it.
constexpr int kW = 800;
constexpr int kH = 480;
constexpr int kGrayBytes = (kW * kH) / 4;   // 96000
constexpr int kPlaneBytes = (kW * kH) / 8;  // 48000

// HSPI mirrors the vendor's SPI host. 12 MHz is conservative for this panel and
// plenty — the ~3 s waveform dominates, not the byte transfer.
SPIClass g_spi(HSPI);
constexpr uint32_t kSpiHz = 12 * 1000 * 1000;

uint8_t* g_gray = nullptr;  // ps_malloc'd 96000-byte 2bpp gray buffer

inline void cs_lo() { digitalWrite(EPD_CS, LOW); }
inline void cs_hi() { digitalWrite(EPD_CS, HIGH); }

void send_command(uint8_t c) {
  digitalWrite(EPD_DC, LOW);
  cs_lo();
  g_spi.transfer(c);
  cs_hi();
}

void send_data(uint8_t d) {
  digitalWrite(EPD_DC, HIGH);
  cs_lo();
  g_spi.transfer(d);
  cs_hi();
}

void send_cmd(uint8_t c, std::initializer_list<uint8_t> data) {
  send_command(c);
  for (uint8_t b : data) send_data(b);
}

// BUSY is active HIGH (HIGH = busy, LOW = idle).
void wait_busy() {
  delay(1);
  const uint32_t start = millis();
  while (digitalRead(EPD_BUSY) == HIGH) {
    delay(5);
    if (millis() - start > 8000) {  // ~3 s typical; bail so we never wedge
      Serial.println("[epd] busy timeout");
      return;
    }
  }
}

void hw_reset() {
  digitalWrite(EPD_RST, HIGH);
  delay(20);
  digitalWrite(EPD_RST, LOW);
  delay(2);
  digitalWrite(EPD_RST, HIGH);
  delay(20);
}

// --- vendor register sequences (EPD_3in97.cpp, 800x480) ----------------------
void init_full() {
  hw_reset();
  wait_busy();
  send_command(0x12);  // SWRESET
  wait_busy();
  send_cmd(0x18, {0x80});
  send_cmd(0x0C, {0xAE, 0xC7, 0xC3, 0xC0, 0x80});
  send_cmd(0x01, {(uint8_t)((kH - 1) % 256), (uint8_t)((kH - 1) / 256), 0x02});
  send_cmd(0x3C, {0x01});
  send_cmd(0x11, {0x01});  // data entry mode
  send_cmd(0x44, {0x00, 0x00, (uint8_t)((kW - 1) % 256), (uint8_t)((kW - 1) / 256)});
  send_cmd(0x45, {(uint8_t)((kH - 1) % 256), (uint8_t)((kH - 1) / 256), 0x00, 0x00});
  send_cmd(0x4E, {0x00, 0x00});
  send_cmd(0x4F, {0x00, 0x00});
  wait_busy();
}

void init_4gray() {
  hw_reset();
  wait_busy();
  send_command(0x12);  // SWRESET
  wait_busy();
  send_cmd(0x0C, {0xAE, 0xC7, 0xC3, 0xC0, 0x80});
  send_cmd(0x01, {(uint8_t)((kH - 1) % 256), (uint8_t)((kH - 1) / 256), 0x02});
  send_cmd(0x11, {0x01});
  send_cmd(0x44, {0x00, 0x00, (uint8_t)((kW - 1) % 256), (uint8_t)((kW - 1) / 256)});
  send_cmd(0x45, {(uint8_t)((kH - 1) % 256), (uint8_t)((kH - 1) / 256), 0x00, 0x00});
  send_cmd(0x4E, {0x00, 0x00});
  send_cmd(0x4F, {0x00, 0x00});
  wait_busy();
  send_cmd(0x3C, {0x01});
  send_cmd(0x18, {0x80});
  send_cmd(0x1A, {0x5A});  // 4-gray waveform select
}

void init_fast() {
  hw_reset();
  wait_busy();
  send_command(0x12);  // SWRESET
  wait_busy();
  send_cmd(0x0C, {0xAE, 0xC7, 0xC3, 0xC0, 0x80});
  send_cmd(0x01, {(uint8_t)((kH - 1) % 256), (uint8_t)((kH - 1) / 256), 0x02});
  send_cmd(0x11, {0x01});
  send_cmd(0x44, {0x00, 0x00, (uint8_t)((kW - 1) % 256), (uint8_t)((kW - 1) / 256)});
  send_cmd(0x45, {(uint8_t)((kH - 1) % 256), (uint8_t)((kH - 1) / 256), 0x00, 0x00});
  send_cmd(0x4E, {0x00, 0x00});
  send_cmd(0x4F, {0x00, 0x00});
  wait_busy();
  send_cmd(0x3C, {0x01});
  send_cmd(0x18, {0x80});
  send_cmd(0x1A, {0x6A});  // fast (1.5s) waveform select
}

void turn_on(uint8_t mode) {  // 0xF7 = full, 0xD7 = fast / 4-gray
  send_cmd(0x22, {mode});
  send_command(0x20);
  wait_busy();
}

// Compute one SSD1677 plane (0=RAM 0x24, 1=RAM 0x26) from the 2bpp gray buffer
// and stream it, holding CS low across the whole plane.
void send_plane_bulk(uint8_t reg, int plane) {
  send_command(reg);
  digitalWrite(EPD_DC, HIGH);
  cs_lo();
  for (int base = 0; base < kW * kH; base += 8) {
    g_spi.transfer(gray_plane_byte(g_gray, base, plane));
  }
  cs_hi();
}

void display_4gray() {
  send_plane_bulk(0x24, 0);
  delay(1);
  send_plane_bulk(0x26, 1);
  turn_on(0xD7);
}

// Threshold the gray buffer to 1bpp and stream a plane, CS held low across it.
void send_mono_bulk(uint8_t reg) {
  send_command(reg);
  digitalWrite(EPD_DC, HIGH);
  cs_lo();
  for (int base = 0; base < kW * kH; base += 8) {
    g_spi.transfer(gray_to_mono_byte(g_gray, base));
  }
  cs_hi();
}

// Fast (~1.5s) mono refresh. Writes the thresholded image to both RAM planes so
// it renders fully and ghost-free as a standalone frame (vendor Display_Fast_Base).
void display_fast() {
  send_mono_bulk(0x24);
  delay(1);
  send_mono_bulk(0x26);
  turn_on(0xD7);
}

void fill_plane(uint8_t reg, uint8_t value) {
  send_command(reg);
  digitalWrite(EPD_DC, HIGH);
  cs_lo();
  for (int i = 0; i < kPlaneBytes; ++i) g_spi.transfer(value);
  cs_hi();
}

void clear_white_full() {
  fill_plane(0x24, 0xFF);
  fill_plane(0x26, 0xFF);
  turn_on(0xF7);
}

}  // namespace

void epd397_init() {
  pinMode(EPD_DC, OUTPUT);
  pinMode(EPD_CS, OUTPUT);
  pinMode(EPD_RST, OUTPUT);
  pinMode(EPD_BUSY, INPUT);
  cs_hi();
  // miso=-1 (write-only panel); the benign "HSPI Does not have default pins"
  // warning is expected. SCK/MOSI still bind.
  g_spi.begin(EPD_SCK, -1, EPD_MOSI, -1);

  if (!g_gray) g_gray = (uint8_t*)ps_malloc(kGrayBytes);
  if (!g_gray) {
    Serial.println("[epd] FATAL gray buffer alloc failed");
    return;
  }

  g_spi.beginTransaction(SPISettings(kSpiHz, MSBFIRST, SPI_MODE0));
  init_full();
  clear_white_full();
  g_spi.endTransaction();
  Serial.printf("[epd] init done %dx%d 4-gray (gray buf %d bytes)\n",
                kW, kH, kGrayBytes);
}

void epd397_clear() {
  if (!g_gray) return;
  g_spi.beginTransaction(SPISettings(kSpiHz, MSBFIRST, SPI_MODE0));
  init_full();
  clear_white_full();
  g_spi.endTransaction();
}

void epd397_push(const uint16_t* px, int w, int h) {
  if (w != kW || h != kH) {
    Serial.printf("[epd] push REJECTED: got %dx%d, expected %dx%d\n", w, h, kW, kH);
    return;
  }
  if (!g_gray) return;
  const uint32_t t0 = millis();
  pack_gray_landscape(px, w, h, g_gray);
  g_spi.beginTransaction(SPISettings(kSpiHz, MSBFIRST, SPI_MODE0));
  init_4gray();
  display_4gray();
  g_spi.endTransaction();
  Serial.printf("[epd] 4-gray refresh in %u ms\n", (unsigned)(millis() - t0));
}

void epd397_push_fast(const uint16_t* px, int w, int h) {
  if (w != kW || h != kH) {
    Serial.printf("[epd] fast push REJECTED: got %dx%d, expected %dx%d\n", w, h, kW, kH);
    return;
  }
  if (!g_gray) return;
  const uint32_t t0 = millis();
  pack_gray_landscape(px, w, h, g_gray);
  g_spi.beginTransaction(SPISettings(kSpiHz, MSBFIRST, SPI_MODE0));
  init_fast();
  display_fast();
  g_spi.endTransaction();
  Serial.printf("[epd] fast refresh in %u ms\n", (unsigned)(millis() - t0));
}

const uint8_t* epd397_last_frame(size_t* len) {
  if (len) *len = kGrayBytes;
  return g_gray;
}

void epd397_sleep() {
  g_spi.beginTransaction(SPISettings(kSpiHz, MSBFIRST, SPI_MODE0));
  send_cmd(0x10, {0x01});  // deep sleep
  delay(100);
  g_spi.endTransaction();
}

}  // namespace rcpaper

#endif  // ARDUINO
