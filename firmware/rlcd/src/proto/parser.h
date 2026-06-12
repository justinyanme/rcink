#pragma once
#include <ArduinoJson.h>
#include "proto/schema.h"

namespace rcpaper {

LayoutNode parse_layout(JsonVariantConst v);

}
