// src/proto/envelope.cpp
#include "proto/envelope.h"

namespace rcpaper {

bool parse_axiom_envelope(JsonVariantConst doc, std::vector<PageRecord>& out) {
  JsonArrayConst matches = doc["matches"].as<JsonArrayConst>();
  if (matches.isNull()) return false;
  for (JsonVariantConst m : matches) {
    JsonVariantConst data = m["data"];
    if (data.isNull()) continue;
    PageRecord rec;
    rec.id          = (const char*)(data["id"] | "");
    rec.order       = data["order"] | 0;
    rec.data_ts     = (uint64_t)(data["data_ts"] | (uint64_t)0);
    rec.layout_json = (const char*)(data["layout_json"] | "");
    // Empty-window aggregation rows (see header) — not pages, skip.
    if (rec.id.empty() || rec.layout_json.empty()) continue;
    out.push_back(std::move(rec));
  }
  return true;
}

}  // namespace rcpaper
