#pragma once

#include <cassert>
#include "freertos/FreeRTOS.h"

using SemaphoreHandle_t = bool*;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { static bool locked; return &locked; }
inline int xSemaphoreTake(SemaphoreHandle_t lock, int) {
  assert(!*lock);
  *lock = true;
  return pdTRUE;
}
inline int xSemaphoreGive(SemaphoreHandle_t lock) {
  assert(*lock);
  *lock = false;
  return pdTRUE;
}
