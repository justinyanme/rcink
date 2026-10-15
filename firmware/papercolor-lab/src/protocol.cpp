#include "protocol.h"

#include <cstdio>
#include <cstring>

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
    if (!strcmp(value, "5F69")) return knobs.psr_tickey = false, true;
    if (!strcmp(value, "0FA9")) return knobs.psr_tickey = true, true;
    return false;
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

bool is_lut_mode(Mode mode) {
  return mode == Mode::Mono || mode == Mode::MonoClear || mode == Mode::Fast || mode == Mode::Quality;
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
      {"info", Verb::Info}, {"reset", Verb::Reset}, {"cleanup", Verb::Cleanup}, {"help", Verb::Help}};
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
    return true;
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
