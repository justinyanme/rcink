// src/net/wifi.h
#pragma once
#include <stdbool.h>

namespace rcpaper {
bool wifi_connect(int max_attempts = 30);
void wifi_disconnect();
}  // namespace rcpaper
