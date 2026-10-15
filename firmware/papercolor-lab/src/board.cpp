#include "board.h"

#include <Arduino.h>
#include <Wire.h>

#include "fatal.h"

namespace rclab::board {
namespace {

uint8_t read8(uint8_t reg) {
  Wire.beginTransmission(kPm1Addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) fatal("PM1 read reg=0x%02x nack", reg);
  if (Wire.requestFrom(kPm1Addr, uint8_t(1)) != 1) fatal("PM1 read reg=0x%02x short", reg);
  return uint8_t(Wire.read());
}

void write8(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(kPm1Addr);
  Wire.write(reg);
  Wire.write(value);
  if (Wire.endTransmission() != 0) fatal("PM1 write reg=0x%02x nack", reg);
}

void bit_on(uint8_t reg, uint8_t mask) { write8(reg, read8(reg) | mask); }
void bit_off(uint8_t reg, uint8_t mask) { write8(reg, read8(reg) & ~mask); }

}  // namespace

uint8_t begin() {
  // M5GFX.cpp:1845 lgfx::i2c::init(port, SDA3, SCL2); m5pm1_i2c_freq = 100000.
  Wire.begin(kI2cSda, kI2cScl, 100000);
  // M5GFX.cpp:175 _check_m5pm1: registers 0x00..0x01 read as 0x2050 little-endian.
  const uint8_t id_lo = read8(0x00), id_hi = read8(0x01);
  if (id_lo != 0x50 || id_hi != 0x20) fatal("PM1 id=0x%02x%02x expected 0x2050", id_hi, id_lo);

  // M5GFX.cpp:1856-1869, verbatim order.
  write8(0x0A, 0x00);                       // WDT_CNT=0 disables watchdog
  bit_off(0x16, uint8_t(0b11 << 0 | 0b11 << 6));  // gpio0 (EPD power), gpio3 (SD power): gpio function
  bit_on(0x10, uint8_t(1 << 0 | 1 << 3));   // output mode
  bit_off(0x13, uint8_t(1 << 0 | 1 << 3));  // push-pull
  bit_on(0x11, uint8_t(1 << 0 | 1 << 3));   // output high: EPD + SD power on
  write8(0x09, 0x00);                       // I2C_CFG: no idle sleep
  delay(100);

  // M5GFX.cpp:1871-1872: GPIO44 (panel CS) output high before any SPI traffic.
  pinMode(kPinEpdCs, OUTPUT);
  digitalWrite(kPinEpdCs, HIGH);
  // SD card shares the bus (M5GFX _set_sd_spimode on GPIO47). Deselect it; the
  // SPI-mode handshake runs in epd::begin() once the bus exists.
  pinMode(kPinSdCs, OUTPUT);
  digitalWrite(kPinSdCs, HIGH);

  // Datasheet v1.9 register 0x04 bitmap: bit0 5VIN, bit1 5VINOUT, bit2 battery.
  return read8(0x04);
}

}  // namespace rclab::board
