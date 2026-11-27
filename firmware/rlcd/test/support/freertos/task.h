#pragma once

#include "freertos/FreeRTOS.h"

BaseType_t xTaskCreate(void (*task)(void*), const char*, unsigned, void*, UBaseType_t, void*);
inline void vTaskDelete(void*) {}
