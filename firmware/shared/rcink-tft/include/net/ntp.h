#pragma once

namespace rcpaper {

void apply_local_tz(const char* tz);
bool ntp_sync_once(const char* tz);

}  // namespace rcpaper
