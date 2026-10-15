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

struct Result {
  const char* stage = nullptr;  // nullptr: ok
  int busy = -1;                // BUSY level at failure
  uint32_t ms = 0;              // elapsed in the failing wait
  bool ok() const { return stage == nullptr; }
};

void begin();  // SPI bus, pin modes; CS/RST idle high
void set_trace(bool on);
// M5GFX _pin_reset: RST high 1 ms, low 2 ms, high 10 ms.
void hard_reset();
// Pinned Panel_ED2208 init list + resolution (epd_init.h).
Result init_native();
// Waits for BUSY idle, then `10` + plane.
Result upload_frame(const uint8_t* packed, size_t bytes);
// Waits for BUSY idle, then PSR `00 <psr0 psr1>` and `20` + LUT (sf32rc tickey_mono.c:35-42).
Result upload_lut(const uint8_t psr[2], const uint8_t* lut, size_t bytes);
// `04`; BUSY must assert within 1 s and release within 25 s. No settle delay.
Result power_on();
// `06 6F 1F 17 27`: the booster Panel_ED2208::_turn_on_display sends after 04.
void booster();
// `12 00`; BUSY must assert within 1 s and release within 25 s.
// busy_ms = asserted duration. Stage names: <stage>-assert, <stage>-release.
Result refresh(const char* stage, uint32_t& busy_ms);
// `12 00`, wait for BUSY assert, then RST low `cut_ms` after the observed
// assertion (FreeInk Ed2208M5Driver.cpp:238-257); RST high again after 2 ms
// + 10 ms settle. busy_ms = assert -> cut, measured.
Result refresh_cut(uint32_t cut_ms, uint32_t& busy_ms);
// `02 00`, wait for BUSY idle. No settle delay.
Result power_off();
// 3-wire read: command, then `count` bytes clocked in on MOSI.
void read_register(uint8_t cmd, uint8_t* out, size_t count);
// The single controller write path: command byte, then `count` data bytes.
void write(uint8_t cmd, const uint8_t* data, size_t count);

}  // namespace rclab::epd
