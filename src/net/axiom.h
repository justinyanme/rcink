// src/net/axiom.h
#pragma once
#include <Arduino.h>
#include <vector>

namespace rcpaper {

struct PageRecord {
  String id;
  int    order = 0;
  String layout_json;   // raw JSON substring; parsed on-demand by the renderer.
};

// Returns true on success and fills `out`. On failure, `out` is unchanged.
bool axiom_fetch_pages(std::vector<PageRecord>& out);

}  // namespace rcpaper
