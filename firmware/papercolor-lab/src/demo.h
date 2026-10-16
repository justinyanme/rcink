// Demo gallery driver: buttons A (G9) next, B (G10) previous, C (G1) page action on
// release, C held 0.8 s Cleanup (native refresh to white); console `demo <page>|act|cleanup` does the same.
// Pages: gallery.h.
#pragma once

#include "epd.h"
#include "protocol.h"

namespace rclab::demo {

// `plane`: the kPixels buffer the drives read (main's g_inks).
void begin(uint8_t* plane);
// Opens page `page` (step 0) and drives it; appends page/drive/busy fields.
epd::Result open(unsigned page, Reply& ok);
// Runs the current page's action (step + 1); fails before any open.
epd::Result act(Reply& ok);
// Cleanup: native refresh to white (factory waveform) removes residue. Page and step kept.
epd::Result cleanup(Reply& ok);
// Polls the buttons; on a press drives and prints "# demo ..." (no reply: unsolicited).
// Returns true when it drove the panel (main's image is then the page).
bool poll();

}  // namespace rclab::demo
