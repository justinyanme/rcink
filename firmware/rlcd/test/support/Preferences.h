#pragma once
#include "Arduino.h"
#include <map>

namespace nvs_fixture {
inline std::map<std::string, std::string> values;
inline bool fail_write = false;
}
class Preferences {
 public:
  bool begin(const char*, bool, const char*) { return true; }
  void end() {}
  bool isKey(const char* key) { return nvs_fixture::values.count(key) != 0; }
  uint8_t getUChar(const char* key, uint8_t fallback) { return getNumber(key, fallback); }
  uint32_t getUInt(const char* key, uint32_t fallback) { return getNumber(key, fallback); }
  uint64_t getULong64(const char* key, uint64_t fallback) { return getNumber(key, fallback); }
  String getString(const char* key, const char* fallback) {
    return isKey(key) ? nvs_fixture::values.at(key) : std::string(fallback);
  }
  size_t getBytesLength(const char* key) { return isKey(key) ? nvs_fixture::values.at(key).size() : 0; }
  size_t getBytes(const char* key, void* output, size_t length) {
    length = std::min(length, getBytesLength(key));
    if (length) memcpy(output, nvs_fixture::values.at(key).data(), length);
    return length;
  }
  size_t putBytes(const char* key, const void* data, size_t size) {
    if (nvs_fixture::fail_write) return 0;
    nvs_fixture::values[key] = std::string((const char*)data, size);
    return size;
  }
  void putUChar(const char* key, uint8_t value) { putNumber(key, value); }
  void putUInt(const char* key, uint32_t value) { putNumber(key, value); }
  void putULong64(const char* key, uint64_t value) { putNumber(key, value); }
  void putString(const char* key, const char* value) { nvs_fixture::values[key] = value; }
 private:
  uint64_t getNumber(const char* key, uint64_t fallback) {
    return isKey(key) ? std::stoull(nvs_fixture::values.at(key)) : fallback;
  }
  void putNumber(const char* key, uint64_t value) { nvs_fixture::values[key] = std::to_string(value); }
};
