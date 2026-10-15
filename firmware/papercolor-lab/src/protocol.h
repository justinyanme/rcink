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

enum class Verb : uint8_t { Info, Reset, Show, Cleanup, Lut, Otp, Raw, RawRead, Trace, Help };
enum class Image : uint8_t { White, Black, Chart, Text, Checker, Invert, Bands };
constexpr unsigned kMaxCheckerSize = 300;
// Custom: the console-edited RAM LUT (`lut` verb) over the mono transition frame.
enum class Mode : uint8_t { Native, Interrupt, Mono, MonoClear, Fast, Quality, Custom };
// LUT-mode plane format: bits per pixel and data rows per image row.
// Once: one data row; Twice: the row twice; Even/Odd: the row then a filler row
// of code 0 (Even), or filler then the row (Odd).
enum class Rows : uint8_t { Once, Twice, Even, Odd };
struct Pack {
  uint8_t bits = 4;  // 4 or 2
  Rows rows = Rows::Once;
  bool operator==(const Pack& o) const { return bits == o.bits && rows == o.rows; }
};
// "4" "4x" "4e" "4o" "2" "2x" "2e" "2o"; false for anything else.
bool parse_pack(const char* text, Pack& out);
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
  bool window = false;              // LUT `win=0|1`: 83 full-panel window (FreeInk bytes) after init
  Pack pack{};                      // LUT `pack=<4|2><|x|e|o>` (2-bit mono only), default 4
  bool frame_raw = false;           // custom `frame=transition|raw`: raw = 4 bpp nibble = ink value (LUT row - 1)
  bool booster = true;              // LUT `booster=native|none`: 06 6F 1F 17 27 after 04
};

bool is_lut_mode(Mode mode);

constexpr size_t kLutBytes799 = 799;
// RAM LUT edits: `lut load <table>`, `lut set <offset> <hex2...>`, `lut get <offset> <n>`.
enum class LutOp : uint8_t { Load, Set, Get };
// Tables `lut load` accepts; index order is modes.cpp's table order.
constexpr const char* kLutTables[] = {"clear_initial", "mono", "mono_clear", "fast1", "fast2",
                                      "fast_cleanup", "quality1", "quality2", "quality3"};
constexpr size_t kLutTableCount = sizeof(kLutTables) / sizeof(kLutTables[0]);

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
  LutOp lut_op{};                // lut
  size_t lut_table{};            // lut load: index into kLutTables
  unsigned lut_offset{};         // lut set/get: byte offset, range within kLutBytes799
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
