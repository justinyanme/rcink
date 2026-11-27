#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "render/mono_pack.h"

namespace rcpaper {

// The packed image most recently sent to the controller, also read by console p.
class St7305Frame {
 public:
  static constexpr int width = 400, height = 300;
  using Bytes = std::array<uint8_t, width * height / 8>;
  using WriteWindow = void (*)(uint8_t col_start, uint8_t col_end,
                               uint8_t row_start, uint8_t row_end,
                               const uint8_t* data, size_t size);
  explicit St7305Frame(WriteWindow write) : write_(write) {}
  const Bytes& bytes() const { return bytes_; }
  void push(const uint16_t* pixels, MonoPolarity polarity);
  void push_changes(const uint8_t* bitmap);  // row-major, MSB first, 1 = black

 private:
  Bytes bytes_{};
  WriteWindow write_;
};

}  // namespace rcpaper
