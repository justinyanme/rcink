#include "net/token_client.h"

#include "net/usage_config.h"

#if RCRLCD_USAGE_ENABLED
#include <Arduino.h>
#include <cstdio>

#include "net/usage_client.h"

namespace rcpaper {

std::string token_usage_url() {
  const std::string allowance = RCRLCD_USAGE_URL;
  const size_t scheme = allowance.find("://");
  const size_t path = scheme == std::string::npos
      ? std::string::npos : allowance.find('/', scheme + 3);
  const std::string origin = path == std::string::npos ? allowance : allowance.substr(0, path);
  return origin + "/api/usage-tokens";
}

bool token_fetch(TokenSnapshot& out, FetchDiag* diag) {
  const std::string url = token_usage_url();
  std::string body;
  if (!usage_get_json(url.c_str(), kTokenMaximumBytes, "token", body, diag)) return false;
  std::string reason;
  if (!decode_token(body, out, &reason)) {
    Serial.printf("[token] invalid response (%s, len=%u); keeping cache\n",
                  reason.empty() ? "unknown" : reason.c_str(),
                  (unsigned)body.size());
    if (diag) {
      diag->ok = false;
      std::snprintf(diag->error, sizeof(diag->error), "decode:%s",
                    reason.empty() ? "unknown" : reason.c_str());
    }
    return false;
  }
  return true;
}

}  // namespace rcpaper
#endif
