// src/proto/envelope.cpp
#include "proto/envelope.h"

#include <algorithm>

namespace rcpaper {
namespace {

void append_page(JsonVariantConst data, const char* group_id, std::vector<PageRecord>& out) {
  if (data.isNull()) return;

  const char* data_id = data["id"] | "";
  const char* id = (group_id && group_id[0] != '\0') ? group_id : data_id;
  const char* layout_json = data["layout_json"] | "";
  if (id[0] == '\0' || layout_json[0] == '\0') return;

  PageRecord rec;
  rec.id          = id;
  rec.order       = data["order"] | 0;
  rec.data_ts     = (uint64_t)(data["data_ts"] | (uint64_t)0);
  rec.layout_json = layout_json;
  out.push_back(std::move(rec));
}

void sort_pages(std::vector<PageRecord>& out) {
  std::sort(out.begin(), out.end(), [](const PageRecord& a, const PageRecord& b) {
    if (a.order != b.order) return a.order < b.order;
    return a.id < b.id;
  });
}

void parse_matches(JsonArrayConst matches, std::vector<PageRecord>& out) {
  for (JsonVariantConst m : matches) {
    append_page(m["data"], nullptr, out);
  }
}

bool parse_bucket_groups(JsonVariantConst doc, std::vector<PageRecord>& out) {
  JsonArrayConst series = doc["buckets"]["series"].as<JsonArrayConst>();
  if (series.isNull()) return false;

  for (JsonVariantConst bucket : series) {
    JsonArrayConst groups = bucket["groups"].as<JsonArrayConst>();
    if (groups.isNull()) continue;
    for (JsonVariantConst group : groups) {
      const char* id = group["group"]["id"] | "";
      JsonArrayConst aggregations = group["aggregations"].as<JsonArrayConst>();
      if (aggregations.isNull()) continue;
      for (JsonVariantConst aggregation : aggregations) {
        size_t before = out.size();
        append_page(aggregation["data"], id, out);
        if (out.size() != before) break;
      }
    }
  }
  return true;
}

}  // namespace

bool parse_axiom_envelope(JsonVariantConst doc, std::vector<PageRecord>& out) {
  JsonArrayConst matches = doc["matches"].as<JsonArrayConst>();
  if (!matches.isNull() && matches.size() > 0) {
    parse_matches(matches, out);
    sort_pages(out);
    return true;
  }

  if (parse_bucket_groups(doc, out)) {
    sort_pages(out);
    return true;
  }

  if (!matches.isNull()) return true;
  return false;
}

}  // namespace rcpaper
