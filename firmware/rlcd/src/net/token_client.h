#pragma once

#include <string>

#include "render/token.h"

namespace rcpaper {
std::string token_usage_url();
bool token_fetch(TokenSnapshot& out);
}
