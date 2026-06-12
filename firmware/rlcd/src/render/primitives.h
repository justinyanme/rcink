#pragma once

#ifdef ARDUINO
#include <TFT_eSPI.h>

#include "render/engine.h"

namespace rcpaper {

void paint_layout(TFT_eSprite& canvas, const LaidOutNode& laid);

}  // namespace rcpaper
#endif
