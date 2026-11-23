#pragma once
#include "Arduino.h"
#include "WiFiClientSecure.h"

struct IPAddress {
  uint32_t raw = 0;
};

class WiFiClass {
 public:
  bool hostByName(const char*, IPAddress&) {
    http_fixture::dns_requests++;
    test_millis += http_fixture::dns_ms;
    if (http_fixture::dns_requests > 1 && http_fixture::use_second_dns_ok) {
      return http_fixture::dns_ok_after_first;
    }
    return http_fixture::dns_ok;
  }
};

inline WiFiClass WiFi;
