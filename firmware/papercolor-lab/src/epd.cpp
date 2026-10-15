#include "epd.h"

#include <Arduino.h>
#include <SPI.h>

#include "board.h"
#include "epd_init.h"

namespace rclab::epd {
namespace {

bool trace_on = false;

void spi_begin() {
  SPI.begin(kPinSclk, kPinMiso, kPinMosi, -1);
  SPI.setFrequency(kSpiWriteHz);
  SPI.setDataMode(SPI_MODE0);
  SPI.setBitOrder(MSBFIRST);
}

// M5GFX.cpp:734 _set_sd_spimode: dummy clocks with SD CS high, then CMD0
// GO_IDLE_STATE so an inserted card ignores panel traffic on the shared bus.
void sd_to_spi_mode() {
  static constexpr uint8_t dummy[16] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  static constexpr uint8_t cmd0[] = {0x40, 0, 0, 0, 0, 0x95, 0xFF, 0xFF};
  SPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
  digitalWrite(board::kPinSdCs, HIGH);
  SPI.writeBytes(dummy, sizeof(dummy));
  digitalWrite(board::kPinSdCs, LOW);
  SPI.writeBytes(cmd0, sizeof(cmd0));
  digitalWrite(board::kPinSdCs, HIGH);
  SPI.endTransaction();
}

Result wait_level(int wanted, uint32_t timeout_ms, const char* stage) {
  const uint32_t began = millis();
  while (digitalRead(kPinBusy) != wanted) {
    if (millis() - began >= timeout_ms) return Result{stage, digitalRead(kPinBusy), millis() - began};
    delay(1);
  }
  return Result{};
}

// Panel_ED2208::_wait_busy: only if BUSY is low, wait for release then 200 ms.
Result wait_idle(const char* stage) {
  if (digitalRead(kPinBusy) == HIGH) return Result{};
  if (auto r = wait_level(HIGH, kBusyReleaseTimeoutMs, stage); !r.ok()) return r;
  delay(200);
  return Result{};
}

}  // namespace

void begin() {
  pinMode(kPinBusy, INPUT_PULLUP);  // Panel_ED2208::init
  pinMode(kPinDc, OUTPUT);
  digitalWrite(kPinDc, HIGH);
  pinMode(kPinRst, OUTPUT);
  digitalWrite(kPinRst, HIGH);
  spi_begin();
  sd_to_spi_mode();
}

void set_trace(bool on) { trace_on = on; }

// M5GFX.cpp:711 _pin_reset(pin, true): high 1 ms, low 2 ms, high 10 ms.
void hard_reset() {
  digitalWrite(kPinRst, HIGH);
  delay(1);
  digitalWrite(kPinRst, LOW);
  delay(2);
  digitalWrite(kPinRst, HIGH);
  delay(10);
}

void write(uint8_t cmd, const uint8_t* data, size_t count) {
  if (trace_on) Serial.printf("# write cmd=%02x len=%u\n", cmd, unsigned(count));
  SPI.beginTransaction(SPISettings(kSpiWriteHz, MSBFIRST, SPI_MODE0));
  digitalWrite(kPinCs, LOW);
  digitalWrite(kPinDc, LOW);
  SPI.write(cmd);
  digitalWrite(kPinDc, HIGH);
  if (count) SPI.writeBytes(data, count);
  digitalWrite(kPinCs, HIGH);
  SPI.endTransaction();
}

// Panel_ED2208::_init_sequence: wait, list0 commands, wait, resolution.
Result init_native() {
  if (auto r = wait_idle("init-idle"); !r.ok()) return r;
  for (size_t i = 0; i < sizeof(kNativeInit);) {
    const uint8_t cmd = kNativeInit[i++], len = kNativeInit[i++];
    if (cmd == 0x61) {
      if (auto r = wait_idle("init-resolution-idle"); !r.ok()) return r;
    }
    write(cmd, kNativeInit + i, len);
    i += len;
  }
  return Result{};
}

Result upload_frame(const uint8_t* packed, size_t bytes) {
  if (auto r = wait_idle("frame-idle"); !r.ok()) return r;
  write(kCmdFrame, packed, bytes);
  return Result{};
}

Result upload_lut(const uint8_t* lut, size_t bytes) {
  if (auto r = wait_idle("lut-idle"); !r.ok()) return r;
  write(kCmdLut, lut, bytes);
  return Result{};
}

// Panel_ED2208::_turn_on_display first half: 04, wait, 200 ms, booster, 200 ms.
Result power_on() {
  write(kCmdPowerOn, nullptr, 0);
  if (auto r = wait_level(LOW, kBusyAssertTimeoutMs, "power-on-assert"); !r.ok()) return r;
  if (auto r = wait_idle("power-on-release"); !r.ok()) return r;
  delay(200);
  write(kCmdBooster, kBoosterAfterPowerOn, sizeof(kBoosterAfterPowerOn));
  delay(200);
  return Result{};
}

Result drive(Mode mode, uint32_t& busy_ms) {
  (void)mode;  // Native is the only mode in this stage.
  static constexpr uint8_t zero = 0x00;
  write(kCmdRefresh, &zero, 1);
  if (auto r = wait_level(LOW, kBusyAssertTimeoutMs, "drive-assert"); !r.ok()) return r;
  const uint32_t asserted = millis();
  if (auto r = wait_level(HIGH, kBusyReleaseTimeoutMs, "drive-release"); !r.ok()) return r;
  busy_ms = millis() - asserted;
  delay(200);  // _wait_busy settle
  return Result{};
}

// Panel_ED2208::_turn_on_display tail: 02 00, wait, 200 ms.
Result power_off() {
  static constexpr uint8_t zero = 0x00;
  write(kCmdPowerOff, &zero, 1);
  if (auto r = wait_idle("power-off-release"); !r.ok()) return r;
  delay(200);
  return Result{};
}

// Bit-banged half-duplex read (LGFX Bus_SPI spi_3wire SIO: data returns on
// MOSI). The hardware SPI releases the pins for the duration.
void read_register(uint8_t cmd, uint8_t* out, size_t count) {
  if (trace_on) Serial.printf("# read cmd=%02x len=%u\n", cmd, unsigned(count));
  SPI.end();
  pinMode(kPinSclk, OUTPUT);
  digitalWrite(kPinSclk, LOW);
  pinMode(kPinMosi, OUTPUT);
  digitalWrite(kPinCs, LOW);
  digitalWrite(kPinDc, LOW);
  for (int bit = 7; bit >= 0; --bit) {
    digitalWrite(kPinMosi, (cmd >> bit) & 1);
    delayMicroseconds(1);
    digitalWrite(kPinSclk, HIGH);
    delayMicroseconds(1);
    digitalWrite(kPinSclk, LOW);
  }
  digitalWrite(kPinDc, HIGH);
  pinMode(kPinMosi, INPUT_PULLUP);
  for (size_t i = 0; i < count; ++i) {
    uint8_t value = 0;
    for (int bit = 0; bit < 8; ++bit) {
      digitalWrite(kPinSclk, HIGH);
      delayMicroseconds(1);
      value = uint8_t(value << 1 | digitalRead(kPinMosi));
      digitalWrite(kPinSclk, LOW);
      delayMicroseconds(1);
    }
    out[i] = value;
  }
  digitalWrite(kPinCs, HIGH);
  pinMode(kPinMosi, OUTPUT);
  spi_begin();
}

}  // namespace rclab::epd
