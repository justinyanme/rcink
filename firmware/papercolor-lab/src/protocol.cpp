#include "protocol.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace rclab {
namespace {

constexpr size_t kMaxTokens = kMaxRawBytes + 1;

size_t tokenize(char* buf, const char* tokens[kMaxTokens]) {
  size_t n = 0;
  for (char* p = buf; *p;) {
    while (*p == ' ') ++p;
    if (!*p) break;
    if (n == kMaxTokens) return kMaxTokens + 1;  // too many: caller rejects
    tokens[n++] = p;
    while (*p && *p != ' ') ++p;
    if (*p) *p++ = 0;
  }
  return n;
}

int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Exactly two hex digits, no prefix: "12", "ff".
bool parse_hex2(const char* tok, uint8_t& out) {
  if (strlen(tok) != 2) return false;
  const int hi = hex_digit(tok[0]), lo = hex_digit(tok[1]);
  if (hi < 0 || lo < 0) return false;
  out = uint8_t(hi << 4 | lo);
  return true;
}

// Decimal, no sign or leading zero, within [lo, hi].
bool parse_uint(const char* tok, unsigned lo, unsigned hi, unsigned& out) {
  if (!*tok || strlen(tok) > 5 || (tok[0] == '0' && tok[1])) return false;
  unsigned value = 0;
  for (const char* p = tok; *p; ++p) {
    if (*p < '0' || *p > '9') return false;
    value = value * 10 + unsigned(*p - '0');
  }
  if (value < lo || value > hi) return false;
  out = value;
  return true;
}

constexpr struct { const char* name; Image image; } kImages[] = {
    {"white", Image::White}, {"black", Image::Black},     {"chart", Image::Chart},
    {"text", Image::Text},   {"checker", Image::Checker}, {"invert", Image::Invert},
    {"bands", Image::Bands}, {"yellow", Image::Yellow}, {"red", Image::Red},  {"blue", Image::Blue},
    {"green", Image::Green}, {"grid", Image::Grid},
};

bool parse_image(const char* tok, Image& out) {
  for (const auto& entry : kImages) {
    if (!strcmp(tok, entry.name)) return out = entry.image, true;
  }
  return false;
}

constexpr struct { const char* name; Mode mode; } kModes[] = {
    {"native", Mode::Native}, {"interrupt", Mode::Interrupt}, {"mono", Mode::Mono},
    {"monoclear", Mode::MonoClear}, {"fast", Mode::Fast}, {"quality", Mode::Quality},
    {"custom", Mode::Custom},
};

bool parse_mode(const char* tok, Mode& out) {
  for (const auto& entry : kModes) {
    if (!strcmp(tok, entry.name)) return out = entry.mode, true;
  }
  return false;
}

// One `key=value` token valid for `mode`; `seen` rejects repeated keys.
bool parse_knob(const char* tok, Mode mode, Knobs& knobs, unsigned& seen) {
  const char* eq = strchr(tok, '=');
  if (!eq) return false;
  const size_t key_len = size_t(eq - tok);
  const char* value = eq + 1;
  auto key_is = [&](const char* key) { return strlen(key) == key_len && !strncmp(tok, key, key_len); };
  auto once = [&](unsigned bit) {
    if (seen & bit) return false;
    seen |= bit;
    return true;
  };
  if (mode == Mode::Interrupt) {
    if (key_is("cut")) return once(1) && parse_uint(value, kMinCutMs, kMaxCutMs, knobs.cut_ms);
    if (key_is("cdi")) {
      if (!once(2)) return false;
      if (!strcmp(value, "0F")) return knobs.cdi = 0x0F, true;
      if (!strcmp(value, "3F")) return knobs.cdi = 0x3F, true;
      return false;
    }
    return false;
  }
  if (!is_lut_mode(mode)) return false;
  if (key_is("setup")) {
    if (!once(4)) return false;
    if (!strcmp(value, "native")) return knobs.setup_tickey = false, true;
    if (!strcmp(value, "tickey")) return knobs.setup_tickey = true, true;
    return false;
  }
  if (key_is("psr")) {
    if (!once(8)) return false;
    // Byte 0: native 5F (4 bpp, LUT frames reach rows 0-491), sf32rc 0F (2 bpp) or
    // 1F (4 bpp over all 600 rows, found 2026-10-16); byte 1: native 69 / sf32rc A9.
    if (strlen(value) != 4) return false;
    const bool b0 = !strncmp(value, "5F", 2) || !strncmp(value, "0F", 2) || !strncmp(value, "1F", 2);
    const bool b1 = !strcmp(value + 2, "69") || !strcmp(value + 2, "A9");
    if (!b0 || !b1) return false;
    knobs.psr[0] = value[0] == '5' ? 0x5F : value[0] == '1' ? 0x1F : 0x0F;
    knobs.psr[1] = value[2] == '6' ? 0x69 : 0xA9;
    return true;
  }
  if (key_is("tres")) {
    if (!once(64)) return false;
    if (!strcmp(value, "400")) return knobs.tres_half = false, true;
    if (!strcmp(value, "200")) return knobs.tres_half = true, true;
    return false;
  }
  if (key_is("win")) {
    if (!once(128)) return false;
    if (!strcmp(value, "0")) return knobs.window = false, true;
    if (!strcmp(value, "1")) return knobs.window = true, true;
    return false;
  }
  if (key_is("map")) {
    if (mode != Mode::Custom || !once(256) || strlen(value) != 8) return false;
    for (size_t i = 0; i < 8; ++i) {
      const char c = value[i];
      if (c >= '0' && c <= '9') knobs.map[i] = uint8_t(c - '0');
      else if (c >= 'A' && c <= 'F') knobs.map[i] = uint8_t(c - 'A' + 10);
      else return false;
    }
    return knobs.mapped = true;
  }
  if (key_is("field")) {
    if (mode != Mode::Custom || !once(512) || strlen(value) != 3 || value[1] != ':') return false;
    if (value[0] < '0' || value[0] > '7' || value[2] < '1' || value[2] > '3') return false;
    knobs.field_shift = int8_t(value[0] - '0');
    knobs.field_bits = uint8_t(value[2] - '0');
    return true;
  }
  if (key_is("pack")) {
    if (!once(32)) return false;
    const bool mono = mode == Mode::Mono || mode == Mode::MonoClear || mode == Mode::Custom;  // colour needs 3 bits
    if (!parse_pack(value, knobs.pack)) return false;
    return mono || knobs.pack.bits == 4;
  }
  if (key_is("booster")) {
    if (!once(16)) return false;
    if (!strcmp(value, "native")) return knobs.booster = true, true;
    if (!strcmp(value, "none")) return knobs.booster = false, true;
    return false;
  }
  return false;
}

}  // namespace

namespace {
constexpr struct { const char* name; Pack pack; } kPacks[] = {
    {"4", {4, Rows::Once}}, {"4x", {4, Rows::Twice}}, {"4e", {4, Rows::Even}}, {"4o", {4, Rows::Odd}},
    {"2", {2, Rows::Once}}, {"2x", {2, Rows::Twice}}, {"2e", {2, Rows::Even}}, {"2o", {2, Rows::Odd}},
};
}  // namespace

bool parse_pack(const char* text, Pack& out) {
  for (const auto& entry : kPacks) {
    if (!strcmp(text, entry.name)) return out = entry.pack, true;
  }
  return false;
}

const char* pack_name(Pack pack) {
  for (const auto& entry : kPacks) {
    if (entry.pack == pack) return entry.name;
  }
  return "?";
}

bool is_lut_mode(Mode mode) {
  return mode == Mode::Mono || mode == Mode::MonoClear || mode == Mode::Fast || mode == Mode::Quality ||
         mode == Mode::Custom;
}

bool parse_command(const char* line, Command& out, const char*& why) {
  char buf[kMaxLine + 1];
  const size_t len = strlen(line);
  if (len > kMaxLine) { why = "length"; return false; }
  memcpy(buf, line, len + 1);
  for (size_t end = len; end && (buf[end - 1] == '\r' || buf[end - 1] == '\n'); --end) buf[end - 1] = 0;

  const char* tok[kMaxTokens];
  const size_t n = tokenize(buf, tok);
  if (n == 0) { why = "empty"; return false; }
  if (n > kMaxTokens) { why = "args"; return false; }
  out = Command{};
  const char* verb = tok[0];

  static constexpr struct { const char* name; Verb verb; } kBare[] = {
      {"info", Verb::Info}, {"reset", Verb::Reset}, {"cleanup", Verb::Cleanup}, {"otp", Verb::Otp},
      {"help", Verb::Help}};
  for (const auto& bare : kBare) {
    if (strcmp(verb, bare.name)) continue;
    if (n != 1) { why = "args"; return false; }
    out.verb = bare.verb;
    return true;
  }
  if (!strcmp(verb, "show")) {
    // show <image> [N] <mode> [key=value...]: only `checker` takes N.
    out.verb = Verb::Show;
    size_t i = 1;
    if (n < 3) { why = "args"; return false; }
    if (!parse_image(tok[i++], out.image)) { why = "image"; return false; }
    if (out.image == Image::Checker) {
      if (!parse_uint(tok[i++], 1, kMaxCheckerSize, out.checker)) { why = "size"; return false; }
    }
    if (i >= n) { why = "args"; return false; }
    if (!parse_mode(tok[i++], out.mode)) { why = "mode"; return false; }
    unsigned seen = 0;
    for (; i < n; ++i) {
      if (!parse_knob(tok[i], out.mode, out.knobs, seen)) { why = "knob"; return false; }
    }
    if (out.knobs.mapped) {  // every mapped value must fit the pack's pixel bits
      for (uint8_t v : out.knobs.map) {
        if (v >> out.knobs.pack.bits) { why = "map"; return false; }
      }
    }
    if (out.knobs.field_shift >= 0 && (out.knobs.mapped || out.knobs.field_bits > out.knobs.pack.bits)) {
      why = "field";
      return false;
    }
    return true;
  }
  if (!strcmp(verb, "lut")) {
    // lut set reuses bytes/count; lut get reuses read_count.
    out.verb = Verb::Lut;
    if (n == 3 && !strcmp(tok[1], "load")) {
      out.lut_op = LutOp::Load;
      for (size_t t = 0; t < kLutTableCount; ++t) {
        if (!strcmp(tok[2], kLutTables[t])) return out.lut_table = t, true;
      }
      why = "table";
      return false;
    }
    if (n >= 4 && (!strcmp(tok[1], "set") || !strcmp(tok[1], "get"))) {
      if (!parse_uint(tok[2], 0, kLutBytes799 - 1, out.lut_offset)) { why = "offset"; return false; }
      if (tok[1][0] == 'g') {
        unsigned count = 0;
        if (n != 4 || !parse_uint(tok[3], 1, kMaxReadBytes, count)) { why = "count"; return false; }
        if (out.lut_offset + count > kLutBytes799) { why = "range"; return false; }
        out.lut_op = LutOp::Get;
        out.read_count = count;
        return true;
      }
      if (n - 3 > kMaxReadBytes || out.lut_offset + (n - 3) > kLutBytes799) { why = "range"; return false; }
      for (size_t i = 3; i < n; ++i) {
        if (!parse_hex2(tok[i], out.bytes[i - 3])) { why = "hex"; return false; }
      }
      out.lut_op = LutOp::Set;
      out.count = n - 3;
      return true;
    }
    why = "args";
    return false;
  }
  if (!strcmp(verb, "raw")) {
    if (n < 2 || n - 1 > kMaxRawBytes) { why = "args"; return false; }
    out.verb = Verb::Raw;
    for (size_t i = 1; i < n; ++i) {
      if (!parse_hex2(tok[i], out.bytes[i - 1])) { why = "hex"; return false; }
    }
    out.count = n - 1;
    return true;
  }
  if (!strcmp(verb, "rawread")) {
    if (n != 3) { why = "args"; return false; }
    out.verb = Verb::RawRead;
    if (!parse_hex2(tok[1], out.read_cmd)) { why = "hex"; return false; }
    unsigned count = 0;
    if (!parse_uint(tok[2], 1, kMaxReadBytes, count)) { why = "count"; return false; }
    out.read_count = count;
    return true;
  }
  if (!strcmp(verb, "trace")) {
    if (n != 2) { why = "args"; return false; }
    out.verb = Verb::Trace;
    if (!strcmp(tok[1], "on")) out.trace_on = true;
    else if (!strcmp(tok[1], "off")) out.trace_on = false;
    else { why = "arg"; return false; }
    return true;
  }
  why = "verb";
  return false;
}

const char* image_name(Image image) {
  for (const auto& entry : kImages) {
    if (entry.image == image) return entry.name;
  }
  return "?";
}

const char* mode_name(Mode mode) {
  for (const auto& entry : kModes) {
    if (entry.mode == mode) return entry.name;
  }
  return "?";
}

Reply::Reply(bool ok) {
  buf_[0] = 0;
  append(ok ? "OK" : "FAIL");
}

void Reply::append(const char* text) {
  const size_t add = strlen(text);
  if (len_ + add > kMaxLine) {
    overflow_ = true;
    return;
  }
  memcpy(buf_ + len_, text, add + 1);
  len_ += add;
}

Reply& Reply::field(const char* key, const char* value) {
  append(" ");
  append(key);
  append("=");
  append(value);
  return *this;
}

Reply& Reply::field(const char* key, long value) {
  char digits[24];
  snprintf(digits, sizeof(digits), "%ld", value);
  return field(key, digits);
}

Reply& Reply::hex(const char* key, const uint8_t* bytes, size_t count) {
  char text[2 * kMaxRawBytes + 1];
  if (count > kMaxRawBytes) count = kMaxRawBytes;
  for (size_t i = 0; i < count; ++i) snprintf(text + 2 * i, 3, "%02x", bytes[i]);
  text[2 * count] = 0;
  return field(key, text);
}

}  // namespace rclab
