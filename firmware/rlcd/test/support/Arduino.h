#pragma once
#include <algorithm>
#include <cstdint>
#include <cctype>
#include <cstring>
#include <string>

class String : public std::string {
 public:
  using std::string::string;
  String() = default;
  String(const std::string& value) : std::string(value) {}
  int indexOf(char c) const { const auto pos = find(c); return pos == npos ? -1 : (int)pos; }
  String substring(int start, int end) const { return substr(start, end - start); }
  void trim() {
    const auto first = find_first_not_of(" \t\r\n");
    *this = first == npos ? std::string() : substr(first, find_last_not_of(" \t\r\n") - first + 1);
  }
  bool equalsIgnoreCase(const char* other) const {
    std::string a = *this, b = other;
    std::transform(a.begin(), a.end(), a.begin(), ::tolower);
    std::transform(b.begin(), b.end(), b.begin(), ::tolower);
    return a == b;
  }
  size_t write(uint8_t byte) { push_back((char)byte); return 1; }
  size_t write(const uint8_t* data, size_t size) { append((const char*)data, size); return size; }
  int read() { return cursor_ < size() ? (unsigned char)(*this)[cursor_++] : -1; }
  size_t readBytes(char* output, size_t count) {
    count = std::min(count, size() - cursor_);
    memcpy(output, data() + cursor_, count);
    cursor_ += count;
    return count;
  }
 private:
  size_t cursor_ = 0;
};
inline uint32_t test_millis = 0;
inline uint32_t millis() { return test_millis; }
inline void delay(uint32_t ms) { test_millis += ms; }
struct TestSerial {
  void println(const char*) {}
  template<class... Args> void printf(const char*, Args...) {}
};
inline TestSerial Serial;
