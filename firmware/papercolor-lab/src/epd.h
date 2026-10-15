// Raw ED2208/EL040EF1 panel driver. Every controller byte leaves through
// write(); nothing here paints on its own.
#pragma once

#include <cstddef>
#include <cstdint>

namespace rclab::epd {

constexpr int kPinMosi = 13;  // M5GFX.cpp:1874-1877 bus_cfg (3-wire: reads return on MOSI)
constexpr int kPinMiso = 14;
constexpr int kPinSclk = 15;
constexpr int kPinDc = 43;
constexpr int kPinCs = 44;    // panel cfg.pin_cs
constexpr int kPinRst = 12;   // M5GFX.cpp:1873 _pin_reset(GPIO_NUM_12): EPD RST
constexpr int kPinBusy = 11;  // panel cfg.pin_busy; low = busy
constexpr uint32_t kSpiWriteHz = 4000000;

constexpr uint32_t kBusyAssertTimeoutMs = 1000;
constexpr uint32_t kBusyReleaseTimeoutMs = 25000;

enum class Mode : uint8_t { Native };

struct Result {
  const char* stage = nullptr;  // nullptr: ok
  int busy = -1;                // BUSY level at failure
  uint32_t ms = 0;              // elapsed in the failing wait
  bool ok() const { return stage == nullptr; }
};

void begin();  // SPI bus, pin modes; CS/RST idle high
void set_trace(bool on);
void hard_reset();
Result init_native();
Result upload_frame(const uint8_t* packed, size_t bytes);
Result upload_lut(const uint8_t* lut, size_t bytes);
Result power_on();
Result drive(Mode mode, uint32_t& busy_ms);
Result power_off();
// 3-wire read: command, then `count` bytes clocked in on MOSI.
void read_register(uint8_t cmd, uint8_t* out, size_t count);
// The single controller write path: command byte, then `count` data bytes.
void write(uint8_t cmd, const uint8_t* data, size_t count);

}  // namespace rclab::epd
