#pragma once

#include <cstddef>
#include <cstdint>

namespace rcpaper {

// ESP32 spiWriteNL loads whole uint32_t words, including the last one.
template <typename Spi>
void write_window_bytes(Spi& spi, const uint8_t* data, size_t size) {
  while (size && reinterpret_cast<uintptr_t>(data) % 4) {
    spi.transfer(*data++);
    --size;
  }
  const size_t tail = size % 4;
  const size_t bulk = size - tail;
  if (bulk) {
    spi.writeBytes(data, bulk);
    data += bulk;
  }
  for (size_t i = 0; i < tail; ++i) spi.transfer(data[i]);
}

}  // namespace rcpaper
