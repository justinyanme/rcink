// src/net/axiom.cpp
#include "net/axiom.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include "config.h"

namespace rcpaper {

static void iso_minus(time_t now, time_t delta, char* out, size_t n) {
  time_t t = now - delta;
  struct tm tm_;
  gmtime_r(&t, &tm_);
  strftime(out, n, "%Y-%m-%dT%H:%M:%SZ", &tm_);
}

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

  time_t now;
  time(&now);
  char start[32], end[32];
  iso_minus(now, 60 * 60, start, sizeof(start));   // 1h lookback (APL also filters)
  iso_minus(now, 0,       end,   sizeof(end));

  JsonDocument req;
  req["apl"]       = AXIOM_APL_QUERY;
  req["startTime"] = start;
  req["endTime"]   = end;
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

  // Parse the OUTER response only; keep `layout` as raw JSON.
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("[axiom] outer parse: %s\n", err.c_str());
    return false;
  }

  out.clear();
  JsonArrayConst matches = doc["matches"].as<JsonArrayConst>();
  if (matches.isNull()) {
    Serial.println("[axiom] no matches[] in response");
    return false;
  }
  for (JsonVariantConst m : matches) {
    JsonVariantConst data = m["data"];
    if (data.isNull()) continue;
    PageRecord rec;
    rec.id    = (const char*)(data["id"] | "");
    rec.order = data["order"] | 0;
    String layout_str;
    serializeJson(data["layout"], layout_str);
    rec.layout_json = layout_str;
    out.push_back(std::move(rec));
  }
  Serial.printf("[axiom] %u pages\n", (unsigned)out.size());
  return true;
}

}  // namespace rcpaper
