#pragma once

#include "render/monument.h"

namespace rcpaper {

// Keep storage out of the pure codec so native tests need not link NVS.
inline bool persist_monument_snapshot(const MonumentSnapshot& snapshot) {
  MonumentSnapshot stored;
  if (decode_monument(nvs_load_monument(), stored) &&
      monument_content_equal(stored, snapshot)) {
    return true;
  }
  return nvs_save_monument(encode_monument(snapshot));
}

}  // namespace rcpaper
