#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>

namespace rcpaper {

struct FetchDiag {
  bool attempted = false;
  bool ok = false;
  uint8_t attempts = 0;
  int16_t status = 0;      // HTTP status or negative HTTPClient error; 0 if dns/config
  uint32_t dns_ms = 0;
  uint32_t get_ms = 0;
  uint32_t body_ms = 0;
  uint64_t at_s = 0;       // epoch seconds stamped on main task
  char error[48] = {};     // sanitized short error code; never keys/payloads
};

inline void format_fetch_diag(const FetchDiag& d, char* out, size_t cap,
                              bool include_time = true) {
  if (!out || cap == 0) return;
  if (!d.attempted) {
    std::snprintf(out, cap, "none");
    return;
  }
  char at_buf[16] = {};
  if (include_time && d.at_s >= 1704067200ULL) {
    const time_t instant = (time_t)d.at_s;
    std::tm local = {};
    localtime_r(&instant, &local);
    std::snprintf(at_buf, sizeof(at_buf), " @%02d:%02d:%02d",
                  local.tm_hour, local.tm_min, local.tm_sec);
  }
  if (d.ok) {
    std::snprintf(out, cap, "ok dns=%lu get=%lu body=%lu t=%u%s",
                  (unsigned long)d.dns_ms, (unsigned long)d.get_ms,
                  (unsigned long)d.body_ms, (unsigned)d.attempts, at_buf);
  } else {
    const char* err = d.error[0] ? d.error : "fail";
    std::snprintf(out, cap, "%s dns=%lu get=%lu t=%u%s",
                  err, (unsigned long)d.dns_ms, (unsigned long)d.get_ms,
                  (unsigned)d.attempts, at_buf);
  }
}

}  // namespace rcpaper
