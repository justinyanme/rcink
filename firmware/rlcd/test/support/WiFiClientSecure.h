#pragma once
#include "Arduino.h"
#include <cstring>

namespace http_fixture {
inline int status = 200;
inline int status_after_first = 0;
inline bool use_second_status = false;
inline int declared = -1;
inline int declared_after_first = -1;
inline bool use_second_declared = false;
inline std::string body;
inline size_t cursor = 0;
inline bool stalled = false;
inline bool stalled_after_first = false;
inline bool use_second_stalled = false;
inline bool dns_ok = true;
inline bool dns_ok_after_first = true;
inline bool use_second_dns_ok = false;
inline uint32_t dns_ms = 0;
inline uint32_t get_ms = 0;
inline bool clock_valid = true;
inline bool ca_set = false;
inline unsigned requests = 0;
inline unsigned dns_requests = 0;
inline bool redirects_disabled = false;
inline String media = "application/json";
inline String transfer;
}
class WiFiClient {
 public:
  int available() {
    const bool stalled = (http_fixture::requests > 1 && http_fixture::use_second_stalled)
        ? http_fixture::stalled_after_first : http_fixture::stalled;
    return stalled ? 0 : (int)(http_fixture::body.size() - http_fixture::cursor);
  }
  int read(uint8_t* output, int count) {
    count = std::min(count, available());
    memcpy(output, http_fixture::body.data() + http_fixture::cursor, count);
    http_fixture::cursor += count;
    return count;
  }
};
class WiFiClientSecure : public WiFiClient {
 public:
  void setCACert(const char* ca) { http_fixture::ca_set = ca && strlen(ca) > 100; }
  void setHandshakeTimeout(unsigned) {}
};
