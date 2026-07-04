#ifdef ARDUINO

#include "net/axiom.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "config.h"
#include "net/axiom_ca.h"

namespace rcpaper {

bool axiom_fetch_pages(std::vector<PageRecord>& out) {
  WiFiClientSecure client;
  client.setCACert(kAxiomRootCa);

  HTTPClient http;
  http.setTimeout(20000);
  if (!http.begin(client, AXIOM_QUERY_URL)) {
    Serial.println("[axiom] http.begin failed");
    return false;
  }
  http.addHeader("Authorization", String("Bearer ") + AXIOM_TOKEN);
  http.addHeader("Content-Type", "application/json");

  // Use a wide static window — the APL itself does `_time > ago(1h)`, so the
  // envelope just needs to be a superset. Avoids depending on NTP being
  // synced before the first fetch (e.g. on networks that block pool.ntp.org).
  JsonDocument req;
  req["apl"]       = AXIOM_APL_QUERY;
  req["startTime"] = "2024-01-01T00:00:00Z";
  req["endTime"]   = "2099-12-31T23:59:59Z";
  String body;
  serializeJson(req, body);

  int code = http.POST(body);
  Serial.printf("[axiom] POST -> %d\n", code);
  if (code != 200) {
#ifdef BOARD_HAS_PSRAM
    if (code > 0) Serial.println(http.getString());
#else
    if (code > 0) Serial.println("[axiom] non-200 response body skipped");
#endif
    http.end();
    return false;
  }

#ifndef BOARD_HAS_PSRAM
  body = String();  // free the request buffer before reading the response
#endif
  String payload = http.getString();
  http.end();

  // Parse only the outer envelope; `layout_json` is already a stringified
  // tree (producer serializes it before ingest, so Axiom doesn't flatten
  // the nested object across columns).
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
#ifndef BOARD_HAS_PSRAM
  Serial.printf("[axiom] body=%uB parse=%s free_heap=%u\n",
                (unsigned)payload.length(), err.c_str(), (unsigned)ESP.getFreeHeap());
#endif
  if (err) {
    Serial.printf("[axiom] outer parse: %s\n", err.c_str());
    return false;
  }

  out.clear();
  if (!parse_axiom_envelope(doc.as<JsonVariantConst>(), out)) {
    Serial.println("[axiom] no usable envelope in response");
    return false;
  }
  // 0 here also covers the empty-window all-null row (see proto/envelope.h);
  // the caller treats an empty result as failure and keeps its cached pages.
  Serial.printf("[axiom] %u pages\n", (unsigned)out.size());
  return true;
}

}  // namespace rcpaper

#endif  // ARDUINO
