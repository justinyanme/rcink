#pragma once

namespace rcpaper {

bool wall_clock_is_valid();
void seed_clock_from_rtc();
void write_rtc_from_clock();

}  // namespace rcpaper
