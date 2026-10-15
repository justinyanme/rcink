// Drive sequences: the current image (ink plane) through one refresh mode.
// Every controller byte goes through epd::write(); nothing writes OTP and no
// voltage/power register is written beyond the pinned native values and the
// explicit knobs (Knobs in protocol.h).
#pragma once

#include <cstdint>

#include "epd.h"
#include "protocol.h"

namespace rclab::modes {

// PSRAM buffers owned by main: kPixels inks, kNativeFrameBytes packed plane,
// kMonoPlaneBytes mono history.
void begin(const uint8_t* inks, uint8_t* packed, uint8_t* old_plane);

// The mono old plane no longer describes the panel (boot, reset, cleanup, any
// non-mono drive, any failed drive).
void forget_history();

// Drives the current image. On success appends the mode's fields to `ok`; on
// failure returns the failing stage. Experimental modes leave the controller
// hard-reset and natively initialised on every exit.
epd::Result run(Mode mode, const Knobs& knobs, Reply& ok);

}  // namespace rclab::modes
