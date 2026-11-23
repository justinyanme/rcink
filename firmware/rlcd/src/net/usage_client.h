#pragma once
#include "net/fetch_diag.h"
#include "render/usage.h"
namespace rcpaper {
// Shared bounded HTTPS GET used by Allowance, Token, and Monument in one Wi-Fi session.
bool usage_get_json(const char* url, size_t maximum_bytes, const char* log_label,
                    std::string& body, FetchDiag* diag = nullptr,
                    bool send_read_key = true);
bool usage_fetch(UsageSnapshot& out, FetchDiag* diag = nullptr);
}
