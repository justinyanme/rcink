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
enum class Mode : uint8_t { Native, Interrupt, Mono, MonoClear, Fast, Quality };
// LUT-mode plane format: bits per pixel, and whether every row is sent twice.
enum class Pack : uint8_t { Bits4, Bits2, Bits2Twice, Bits4Twice };
const char* pack_name(Pack pack);

constexpr unsigned kDefaultCutMs = 340;  // FreeInk e91e34e Ed2208M5Driver.cpp:29 REFRESH_CUTOFF_MS
constexpr unsigned kMinCutMs = 50;
constexpr unsigned kMaxCutMs = 5000;

// Drive variables, `key=value` after the mode. Interrupt defaults are FreeInk's
// (the reference being reproduced); LUT-mode defaults are the native values, so
// every TICKEY value is a deliberate, reported deviation.
struct Knobs {
  unsigned cut_ms = kDefaultCutMs;  // interrupt `cut=`: RST cut after BUSY assert
  uint8_t cdi = 0x0F;               // interrupt `cdi=0F|3F`: 50 <cdi> (FreeInk 0F, native 3F)
  bool setup_tickey = false;        // LUT `setup=native|tickey`: 30 04, 41 00, E0 02, E6 19
  uint8_t psr[2] = {0x5F, 0x69};    // LUT `psr=<5F|0F><69|A9>`: PSR sent before each LUT
  bool tres_half = false;           // LUT `tres=400|200`: resolution 61 width sent after init (geometry, not power)
  Pack pack = Pack::Bits4;          // LUT `pack=4|4x|2|2x` (2, 2x mono only): x = each row sent twice
  bool booster = true;              // LUT `booster=native|none`: 06 6F 1F 17 27 after 04
};

bool is_lut_mode(Mode mode);

struct Command {
  Verb verb{};
  Image image{};                 // show
  unsigned checker{};            // show checker N: square size, 1..kMaxCheckerSize
  Mode mode{};                   // show
  Knobs knobs{};                 // show: mode variables
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

// Builds one reply line. Fields appear in insertion order. A line that would
// exceed kMaxLine becomes "FAIL stage=reply reason=length" (never truncated).
class Reply {
 public:
  explicit Reply(bool ok);
  Reply& field(const char* key, const char* value);
  Reply& field(const char* key, long value);
  Reply& hex(const char* key, const uint8_t* bytes, size_t count);  // key=0a1b
  const char* c_str() const { return overflow_ ? "FAIL stage=reply reason=length" : buf_; }

 private:
  void append(const char* text);
  char buf_[kMaxLine + 1];
  size_t len_ = 0;
  bool overflow_ = false;
};

}  // namespace rclab
