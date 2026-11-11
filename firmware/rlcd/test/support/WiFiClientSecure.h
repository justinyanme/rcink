#pragma once
#include "Arduino.h"
#include <cstring>

namespace http_fixture {
inline int status = 200;
inline int declared = -1;
inline std::string body;
inline size_t cursor = 0;
inline bool stalled = false;
inline bool clock_valid = true;
inline bool ca_set = false;
inline unsigned requests = 0;
inline bool redirects_disabled = false;
inline String media = "application/json";
inline String transfer;
}
class WiFiClient {
 public:
  int available() { return http_fixture::stalled ? 0 : http_fixture::body.size() - http_fixture::cursor; }
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
