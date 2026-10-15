#pragma once

#include <Arduino.h>

namespace rclab {

// Prints "[boot] fatal: ..." (the host scripts' fatal marker) and aborts.
template <typename... Args>
[[noreturn]] void fatal(const char* fmt, Args... args) {
  Serial.print("[boot] fatal: ");
  Serial.printf(fmt, args...);
  Serial.println();
  Serial.flush();
  abort();
}

}  // namespace rclab
