#pragma once

#include "render/token.h"

namespace rcpaper {

// Keep storage out of the pure codec so every native consumer need not link NVS.
inline bool persist_token_snapshot(const TokenSnapshot& snapshot) {
  TokenSnapshot stored;
  if (decode_token(nvs_load_token(), stored) && token_content_equal(stored, snapshot)) {
    return true;  // servedAt alone must not cause an NVS rewrite
  }
  return nvs_save_token(encode_token(snapshot));
}

}  // namespace rcpaper
