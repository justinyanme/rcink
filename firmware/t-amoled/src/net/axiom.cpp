// src/net/axiom.cpp
#include "net/axiom.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include "config.h"

namespace rcpaper {

bool axiom_fetch_pages(std::vector<PageRecord>& out) {
  WiFiClientSecure client;
  client.setCACert(AXIOM_ROOT_CA);

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
    if (code > 0) Serial.println(http.getString());
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  // Parse only the outer envelope; `layout_json` is already a stringified
  // tree (producer serializes it before ingest, so Axiom doesn't flatten
  // the nested object across columns).
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
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
