#ifdef ARDUINO

#include "display/epd397.h"

#include <Arduino.h>
#include <SPI.h>

#include <cstring>
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

// HSPI mirrors the vendor's SPI host. The panel's ~3 s 4-gray waveform
// dominates, so 20 MHz is only a small win over 12 — but free.
SPIClass g_spi(HSPI);
constexpr uint32_t kSpiHz = 20 * 1000 * 1000;

uint8_t* g_gray = nullptr;   // 96000-byte 2bpp gray buffer (PSRAM)
uint8_t* g_plane = nullptr;  // 48000-byte scratch plane for bulk SPI writes
bool g_in_4gray = false;     // panel currently armed in the 4-gray waveform mode

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

// Stream a precomputed full plane (48000 bytes) in one bulk SPI burst, CS held
// low across it — far faster than per-byte transfer().
void write_plane(uint8_t reg) {
  send_command(reg);
  digitalWrite(EPD_DC, HIGH);
  cs_lo();
  g_spi.writeBytes(g_plane, kPlaneBytes);
  cs_hi();
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
  g_in_4gray = false;
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
  g_in_4gray = true;
}

void turn_on(uint8_t mode) {  // 0xF7 = full, 0xD7 = 4-gray
  send_cmd(0x22, {mode});
  send_command(0x20);
  wait_busy();
}

void display_4gray() {
  // Reset the RAM address counter so each frame starts from the top — makes
  // skipping the per-push re-init safe (the two full planes each wrap exactly).
  send_cmd(0x4E, {0x00, 0x00});
  send_cmd(0x4F, {0x00, 0x00});
  for (int i = 0, base = 0; base < kW * kH; base += 8, ++i)
    g_plane[i] = gray_plane_byte(g_gray, base, 0);
  write_plane(0x24);
  for (int i = 0, base = 0; base < kW * kH; base += 8, ++i)
    g_plane[i] = gray_plane_byte(g_gray, base, 1);
  write_plane(0x26);
  turn_on(0xD7);
}

void clear_white_full() {
  std::memset(g_plane, 0xFF, kPlaneBytes);
  write_plane(0x24);
  write_plane(0x26);
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
  if (!g_plane) g_plane = (uint8_t*)ps_malloc(kPlaneBytes);
  if (!g_gray || !g_plane) {
    Serial.println("[epd] FATAL buffer alloc failed");
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
  if (!g_plane) return;
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
  if (!g_gray || !g_plane) return;
  const uint32_t t0 = millis();
  pack_gray_landscape(px, w, h, g_gray);
  g_spi.beginTransaction(SPISettings(kSpiHz, MSBFIRST, SPI_MODE0));
  if (!g_in_4gray) init_4gray();  // arm 4-gray once; later pushes reuse it
  display_4gray();
  g_spi.endTransaction();
  Serial.printf("[epd] 4-gray refresh in %u ms%s\n",
                (unsigned)(millis() - t0), g_in_4gray ? "" : " (re-init)");
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
  g_in_4gray = false;  // sleep drops the waveform; next push must re-init
}

}  // namespace rcpaper

#endif  // ARDUINO
