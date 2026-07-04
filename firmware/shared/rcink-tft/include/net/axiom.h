#pragma once
#include <vector>
#include "proto/envelope.h"  // PageRecord + envelope parsing (natively tested)

namespace rcpaper {

// Returns true on success and fills `out`. On failure, `out` is unchanged.
bool axiom_fetch_pages(std::vector<PageRecord>& out);

}  // namespace rcpaper
