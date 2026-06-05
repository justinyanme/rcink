// src/proto/envelope.h — Axiom legacy-format query envelope → PageRecords.
// Pure logic (no Arduino deps) so the native test env can cover it.
#pragma once
#include <ArduinoJson.h>
#include <cstdint>
#include <string>
#include <vector>

namespace rcpaper {

struct PageRecord {
  std::string id;
  int         order = 0;
  uint64_t    data_ts = 0;   // producer build-time (epoch seconds); 0 = missing
  std::string layout_json;   // raw JSON substring; parsed on-demand by the renderer.
};

// Parse the legacy `matches[]` envelope into `out` (appended in order).
// Returns false when the envelope is malformed (no matches[] — e.g. Axiom
// answered with the buckets/series aggregation shape instead).
//
// Rows with an empty/null id or layout_json are skipped: when the query's
// `ago(1h)` window is empty (producer outage), `summarize arg_max(...)`
// returns ONE all-null aggregation row rather than an empty matches[].
// Accepting it as a page would replace the cached pages with an unparseable
// blank and brick the display until fresh data appears ("layout_json parse
// failed", 2026-06-05). Zero usable rows → caller keeps its cache.
bool parse_axiom_envelope(JsonVariantConst doc, std::vector<PageRecord>& out);

}  // namespace rcpaper
