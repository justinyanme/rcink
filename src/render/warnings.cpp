#include "render/warnings.h"
#ifdef ARDUINO
#include <Arduino.h>

namespace rcpaper {

// A single bool is enough: the badge says "look at serial", and the serial
// log already carries the details. Avoids dragging in std::vector<String> for
// what is effectively a one-bit signal.
static bool g_has_warnings = false;

void render_warn(const char* msg) {
  g_has_warnings = true;
  Serial.printf("[warn] %s\n", msg);
}

void render_warn_clear() { g_has_warnings = false; }
bool render_has_warnings() { return g_has_warnings; }

}  // namespace rcpaper
#endif
