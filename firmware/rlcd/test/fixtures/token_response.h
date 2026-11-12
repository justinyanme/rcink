#pragma once

#include <ArduinoJson.h>
#include <cstdint>
#include <string>

// Anonymous fixture shaped like the deployed rcink.token-usage.v1 response.
// The final seven days use the ticket's sample magnitudes; Gemini is really zero.
inline std::string token_response() {
  JsonDocument doc;
  doc["schema"] = "rcink.token-usage.v1";
  doc["servedAt"] = 1794446400ULL;
  doc["today"] = "2026-11-12";
  doc["requiredSourceCollectedAt"] = 1794442800ULL;
  JsonArray providers = doc["providers"].to<JsonArray>();
  const char* ids[] = {"claude", "codex", "gemini"};
  const uint64_t last7[][7] = {
      {148000000ULL, 400000000ULL, 502000000ULL, 515000000ULL, 154000000ULL, 182000000ULL, 28000000ULL},
      {157000000ULL, 305000000ULL, 1090000000ULL, 956000000ULL, 294000000ULL, 351000000ULL, 0ULL},
      {0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL},
  };
  const uint64_t lifetimes[] = {25900000000ULL, 43400000000ULL, 0ULL};
  for (size_t p = 0; p < 3; p++) {
    JsonObject row = providers.add<JsonObject>();
    row["id"] = ids[p];
    JsonArray days = row["days"].to<JsonArray>();
    for (size_t i = 0; i < 23; i++) {
      days.add(p == 2 ? 0ULL : (p == 1 && i == 0 ? 5000000000ULL : 100000000ULL * (p == 1 ? 2 : 1)));
    }
    for (size_t i = 0; i < 7; i++) days.add(last7[p][i]);
    row["lifetime"] = lifetimes[p];
  }
  std::string json;
  serializeJson(doc, json);
  return json;
}
