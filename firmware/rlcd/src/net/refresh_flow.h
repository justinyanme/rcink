#pragma once

#include <cstdint>
#include <vector>

#include "proto/envelope.h"
#include "render/usage.h"
#include "render/token.h"

namespace rcpaper {

enum class FetchResult : uint8_t {
  WifiFailed,
  DataFailed,
  Unchanged,
  Changed,
};

struct RefreshCompletion {
  FetchResult result = FetchResult::DataFailed;
  std::vector<PageRecord> pages;
  UsageSnapshot usage;
  TokenSnapshot token;
  bool token_attempted = false;
  bool token_ok = false;
  bool clock_synced = false;
  uint64_t completed_at_s = 0;
};

void refresh_flow_init();
bool refresh_start(uint32_t previous_fingerprint, bool token_due = false);
bool refresh_token_pending();
bool refresh_busy();
bool refresh_take(RefreshCompletion& out);

inline bool fetch_succeeded(FetchResult result) {
  return result == FetchResult::Unchanged || result == FetchResult::Changed;
}

inline const char* fetch_result_name(FetchResult result) {
  switch (result) {
    case FetchResult::WifiFailed:
      return "wifi-failed";
    case FetchResult::DataFailed:
      return "fetch-failed";
    case FetchResult::Unchanged:
    case FetchResult::Changed:
      return "fetch-ok";
  }
  return "fetch-invalid";
}

}  // namespace rcpaper
