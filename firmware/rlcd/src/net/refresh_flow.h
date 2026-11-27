#pragma once

#include <cstdint>
#include <vector>

#include "net/fetch_diag.h"
#include "proto/envelope.h"
#include "render/link_status.h"
#include "render/monument.h"
#include "render/token.h"
#include "render/usage.h"

namespace rcpaper {

enum class FetchResult : uint8_t {
  WifiFailed,
  DataFailed,
  Unchanged,
  Changed,
};

struct RefreshCompletion {
  FetchResult result = FetchResult::DataFailed;
  LinkFailure link_failure = LinkFailure::None;
  std::vector<PageRecord> pages;
  UsageSnapshot usage;
  TokenSnapshot token;
  MonumentSnapshot monument;
  FetchDiag usage_diag{};
  FetchDiag token_diag{};
  FetchDiag monument_diag{};
  bool token_attempted = false;
  bool token_ok = false;
  bool monument_ok = false;
  bool clock_synced = false;
  uint64_t completed_at_s = 0;
};

struct RefreshFeeds {
  bool busy;
  bool token_pending;
  TokenWork token_work;
};

void refresh_flow_init();
bool refresh_start(uint32_t previous_fingerprint, bool token_due = false);
RefreshFeeds refresh_feeds();
inline bool refresh_token_pending() { return refresh_feeds().token_pending; }
inline bool refresh_busy() { return refresh_feeds().busy; }
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
