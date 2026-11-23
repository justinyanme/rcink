#pragma once

#include <string>

#include "net/fetch_diag.h"
#include "render/token.h"

namespace rcpaper {
std::string token_usage_url();
bool token_fetch(TokenSnapshot& out, FetchDiag* diag = nullptr);
}
