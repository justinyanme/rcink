#include "net/usage_client.h"
#include "net/usage_config.h"

#if RCRLCD_USAGE_ENABLED
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include "net/usage_ca.h"
#include "sense/clock.h"

namespace rcpaper {
namespace {
// The device's Wi-Fi path to the Worker has been measured at ~250 ms RTT with
// loss: a TLS handshake there takes 5-10 s (cert checks are ~1 s of it), so 10 s
// deadlines failed about one fetch in six. The fetch runs off the UI task.
constexpr int kConnectTimeoutMs = 10000;
constexpr int kHandshakeTimeoutS = 20;
constexpr int kReadTimeoutMs = 20000;
constexpr uint32_t kBodyDeadlineMs = 20000;
constexpr uint8_t kMaxTransientAttempts = 2;
constexpr uint32_t kTransientRetryDelayMs = 500;

std::string extract_https_host(const char* url) {
  if (!url || strncmp(url, "https://", 8) != 0) return "";
  const char* start = url + 8;
  const char* end = start;
  while (*end != '\0' && *end != '/' && *end != ':') ++end;
  return std::string(start, (size_t)(end - start));
}

void set_diag_error(FetchDiag& d, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(d.error, sizeof(d.error), fmt, args);
  va_end(args);
}

bool is_transient_http_status(int status) {
  return status < 0 || status == 502 || status == 503 || status == 504;
}

bool usage_get_json_impl(const char* url, size_t maximum_bytes,
                         const char* log_label, std::string& body,
                         FetchDiag& result) {
  result.attempted = true;
  const std::string host = extract_https_host(url);
  if (!wall_clock_is_valid() || host.empty() ||
      strlen(RCRLCD_USAGE_READ_KEY) == 0) {
    Serial.printf("[%s] HTTPS configuration or clock unavailable\n", log_label);
    set_diag_error(result, "config-or-clock");
    return false;
  }

  for (uint8_t attempt = 1; attempt <= kMaxTransientAttempts; ++attempt) {
    result.attempts = attempt;
    result.status = 0;
    result.dns_ms = 0;
    result.get_ms = 0;
    result.body_ms = 0;
    result.error[0] = '\0';

    IPAddress resolved;
    const uint32_t dns_t0 = millis();
    const bool dns_ok = WiFi.hostByName(host.c_str(), resolved) != 0;
    result.dns_ms = millis() - dns_t0;
    if (!dns_ok) {
      Serial.printf("[%s] DNS failed after %u ms (attempt %u)\n",
                    log_label, (unsigned)result.dns_ms, (unsigned)attempt);
      set_diag_error(result, "dns-fail");
      if (attempt < kMaxTransientAttempts) {
        delay(kTransientRetryDelayMs);
        continue;
      }
      return false;
    }

    WiFiClientSecure client;
    client.setCACert(kUsageRootCa);
    client.setHandshakeTimeout(kHandshakeTimeoutS);
    HTTPClient http;
    http.setConnectTimeout(kConnectTimeoutMs);
    http.setTimeout(kReadTimeoutMs);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    // Request close-delimited identity encoding; read the body ourselves with a
    // total deadline, since HTTPClient's stream-copy loop can wait indefinitely.
    http.useHTTP10(true);
    if (!http.begin(client, url)) {
      set_diag_error(result, "begin-fail");
      return false;
    }
    const char* headers[] = {"Content-Type", "Transfer-Encoding"};
    http.collectHeaders(headers, 2);
    http.setUserAgent("rcink-rlcd/1.0");
    http.addHeader("Accept", "application/json");
    http.addHeader("Accept-Encoding", "identity");
    http.addHeader("X-Usage-Read-Key", RCRLCD_USAGE_READ_KEY);

    const uint32_t get_t0 = millis();
    const int status = http.GET();
    result.get_ms = millis() - get_t0;
    result.status = (int16_t)status;
    Serial.printf("[%s] GET status=%d dns=%u ms get=%u ms (attempt %u)\n",
                  log_label, status, (unsigned)result.dns_ms,
                  (unsigned)result.get_ms, (unsigned)attempt);

    if (is_transient_http_status(status)) {
      set_diag_error(result, "http:%d", status);
      http.end();
      if (attempt < kMaxTransientAttempts) {
        delay(kTransientRetryDelayMs);
        continue;
      }
      return false;
    }
    if (status != 200) {
      set_diag_error(result, "http:%d", status);
      http.end();
      return false;
    }

    String media = http.header("Content-Type");
    const int separator = media.indexOf(';');
    if (separator >= 0) media = media.substring(0, separator);
    media.trim();
    const int declared = http.getSize();
    const bool ct_ok = media.equalsIgnoreCase("application/json");
    const bool te_present = http.header("Transfer-Encoding").length() != 0;
    if (declared > (int)maximum_bytes) {
      Serial.printf("[%s] rejected headers (ct_ok=%d te=%d len=%d); keeping cache\n",
                    log_label, ct_ok ? 1 : 0, te_present ? 1 : 0, declared);
      set_diag_error(result, "oversized:%d", declared);
      http.end();
      return false;
    }
    if (!ct_ok || te_present) {
      Serial.printf("[%s] rejected headers (ct_ok=%d te=%d len=%d); keeping cache\n",
                    log_label, ct_ok ? 1 : 0, te_present ? 1 : 0, declared);
      set_diag_error(result, "hdr:ct%d,te%d", ct_ok ? 1 : 0, te_present ? 1 : 0);
      http.end();
      return false;
    }

    body.clear();
    body.reserve(declared > 0 ? declared : 1024);
    WiFiClient* stream = http.getStreamPtr();
    const uint32_t body_t0 = millis();
    const uint32_t deadline = body_t0 + kBodyDeadlineMs;
    bool complete = false;
    bool oversized = false;
    while ((int32_t)(millis() - deadline) < 0) {
      const int available = stream->available();
      if (available > 0) {
        uint8_t buffer[256];
        const int read = stream->read(buffer, std::min(available, (int)sizeof(buffer)));
        if (read > 0) body.append((const char*)buffer, read);
        if (body.size() > maximum_bytes) {
          oversized = true;
          break;
        }
        if (declared >= 0 && body.size() >= (size_t)declared) {
          complete = body.size() == (size_t)declared;
          break;
        }
      } else if (!http.connected()) {
        complete = declared < 0 || body.size() == (size_t)declared;
        break;
      } else {
        delay(1);
      }
    }
    result.body_ms = millis() - body_t0;
    http.end();

    if (oversized) {
      Serial.printf("[%s] oversized response (got=%u max=%u); keeping cache\n",
                    log_label, (unsigned)body.size(), (unsigned)maximum_bytes);
      set_diag_error(result, "oversized:%u", (unsigned)body.size());
      return false;
    }
    if (!complete) {
      Serial.printf("[%s] incomplete response (got=%u declared=%d body=%u ms attempt=%u); keeping cache\n",
                    log_label, (unsigned)body.size(), declared,
                    (unsigned)result.body_ms, (unsigned)attempt);
      set_diag_error(result, "body:%u/%d", (unsigned)body.size(), declared);
      if (attempt < kMaxTransientAttempts) {
        delay(kTransientRetryDelayMs);
        continue;
      }
      return false;
    }

    result.ok = true;
    result.error[0] = '\0';
    Serial.printf("[%s] ok dns=%u ms get=%u ms body=%u ms len=%u tries=%u\n",
                  log_label, (unsigned)result.dns_ms, (unsigned)result.get_ms,
                  (unsigned)result.body_ms, (unsigned)body.size(), (unsigned)attempt);
    return true;
  }
  return false;
}
}  // namespace

bool usage_get_json(const char* url, size_t maximum_bytes, const char* log_label,
                    std::string& body, FetchDiag* diag) {
  FetchDiag result;
  const bool ok = usage_get_json_impl(url, maximum_bytes, log_label, body, result);
  if (diag) *diag = result;
  return ok;
}

bool usage_fetch(UsageSnapshot& out, FetchDiag* diag) {
  std::string body;
  if (!usage_get_json(RCRLCD_USAGE_URL, kUsageMaximumBytes, "usage", body, diag)) return false;
  std::string reason;
  if (!decode_usage(body, out, &reason)) {
    Serial.printf("[usage] invalid response (%s, len=%u); keeping cache\n",
                  reason.empty() ? "unknown" : reason.c_str(),
                  (unsigned)body.size());
    if (diag) {
      diag->ok = false;
      set_diag_error(*diag, "decode:%s", reason.empty() ? "unknown" : reason.c_str());
    }
    return false;
  }
  return true;
}
}  // namespace rcpaper
#endif
