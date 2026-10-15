// Lab console protocol: pure parser and reply formatter (no Arduino).
//
// Request: one newline-terminated line, space-separated tokens, verb first.
// Reply: exactly one line, "OK key=value ..." or "FAIL key=value ...".
// Progress/trace lines start with "# " and carry no result.
#pragma once

#include <cstddef>
#include <cstdint>

namespace rclab {

constexpr size_t kMaxLine = 255;
constexpr size_t kMaxRawBytes = 64;   // raw: command byte + data
constexpr size_t kMaxReadBytes = 32;  // rawread: bytes returned

enum class Verb : uint8_t { Info, Reset, Show, Cleanup, Raw, RawRead, Trace, Help };
enum class Image : uint8_t { White, Black, Chart, Text, Checker, Invert };
constexpr unsigned kMaxCheckerSize = 300;
enum class Mode : uint8_t { Native };

struct Command {
  Verb verb{};
  Image image{};                 // show
  unsigned checker{};            // show checker N: square size, 1..kMaxCheckerSize
  Mode mode{};                   // show
  uint8_t bytes[kMaxRawBytes]{}; // raw: bytes[0] is the command
  size_t count{};                // raw: bytes used
  uint8_t read_cmd{};            // rawread
  size_t read_count{};           // rawread: 1..kMaxReadBytes
  bool trace_on{};               // trace
};

// Parses one line (trailing CR/LF tolerated). On failure returns false and
// sets `why` to a short token for `FAIL stage=parse reason=<why>`.
bool parse_command(const char* line, Command& out, const char*& why);

const char* image_name(Image image);
const char* mode_name(Mode mode);

// Builds one reply line. Fields appear in insertion order.
class Reply {
 public:
  explicit Reply(bool ok);
  Reply& field(const char* key, const char* value);
  Reply& field(const char* key, long value);
  Reply& hex(const char* key, const uint8_t* bytes, size_t count);  // key=0a1b
  const char* c_str() const { return buf_; }

 private:
  void append(const char* text);
  char buf_[kMaxLine + 1];
  size_t len_ = 0;
};

}  // namespace rclab
