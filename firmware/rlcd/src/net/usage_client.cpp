#include "net/usage_client.h"
#include "net/usage_config.h"

#if RCRLCD_USAGE_ENABLED
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <algorithm>
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
}  // namespace

bool usage_fetch(UsageSnapshot& out) {
  if (!wall_clock_is_valid() || strncmp(RCRLCD_USAGE_URL, "https://", 8) != 0 ||
      strlen(RCRLCD_USAGE_READ_KEY) == 0) {
    Serial.println("[usage] HTTPS configuration or clock unavailable");
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
  if (!http.begin(client, RCRLCD_USAGE_URL)) return false;
  const char* headers[] = {"Content-Type", "Transfer-Encoding"};
  http.collectHeaders(headers, 2);
  http.setUserAgent("rcink-rlcd/1.0");
  http.addHeader("Accept", "application/json");
  http.addHeader("Accept-Encoding", "identity");
  http.addHeader("X-Usage-Read-Key", RCRLCD_USAGE_READ_KEY);
  const uint32_t started_ms = millis();
  const int status = http.GET();
  Serial.printf("[usage] GET status=%d after %u ms\n", status, (unsigned)(millis() - started_ms));
  String media = http.header("Content-Type");
  const int separator = media.indexOf(';');
  if (separator >= 0) media = media.substring(0, separator);
  media.trim();
  const int declared = http.getSize();
  if (status != 200 || !media.equalsIgnoreCase("application/json") ||
      http.header("Transfer-Encoding").length() != 0 || declared > (int)kUsageMaximumBytes) {
    http.end();
    return false;
  }
  std::string body;
  body.reserve(declared > 0 ? declared : 1024);
  WiFiClient* stream = http.getStreamPtr();
  const uint32_t deadline = millis() + kBodyDeadlineMs;
  bool complete = false;
  while ((int32_t)(millis() - deadline) < 0) {
    const int available = stream->available();
    if (available > 0) {
      uint8_t buffer[256];
      const int read = stream->read(buffer, std::min(available, (int)sizeof(buffer)));
      if (read > 0) body.append((const char*)buffer, read);
      if (body.size() > kUsageMaximumBytes) break;
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
  http.end();
  if (!complete || !decode_usage(body, out)) {
    Serial.println("[usage] incomplete, oversized, or invalid response; keeping cache");
    return false;
  }
  return true;
}
}  // namespace rcpaper
#endif
