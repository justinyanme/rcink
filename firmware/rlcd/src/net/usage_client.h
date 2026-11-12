#pragma once
#include "render/usage.h"
namespace rcpaper {
// Shared bounded HTTPS GET used by Allowance and Token in one Wi-Fi session.
bool usage_get_json(const char* url, size_t maximum_bytes, const char* log_label,
                    std::string& body);
bool usage_fetch(UsageSnapshot& out);
}
