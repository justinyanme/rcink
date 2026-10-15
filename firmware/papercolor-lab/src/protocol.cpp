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

bool parse_count(const char* tok, size_t& out) {
  if (!*tok || strlen(tok) > 2) return false;
  size_t value = 0;
  for (const char* p = tok; *p; ++p) {
    if (*p < '0' || *p > '9') return false;
    value = value * 10 + size_t(*p - '0');
  }
  if (value < 1 || value > kMaxReadBytes) return false;
  out = value;
  return true;
}

}  // namespace

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
    if (n != 3) { why = "args"; return false; }
    out.verb = Verb::Show;
    if (!strcmp(tok[1], "white")) out.image = Image::White;
    else if (!strcmp(tok[1], "black")) out.image = Image::Black;
    else { why = "image"; return false; }
    if (!strcmp(tok[2], "native")) out.mode = Mode::Native;
    else { why = "mode"; return false; }
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
    if (!parse_count(tok[2], out.read_count)) { why = "count"; return false; }
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
  return image == Image::White ? "white" : "black";
}

const char* mode_name(Mode mode) {
  (void)mode;
  return "native";
}

Reply::Reply(bool ok) {
  buf_[0] = 0;
  append(ok ? "OK" : "FAIL");
}

void Reply::append(const char* text) {
  const size_t add = strlen(text);
  if (len_ + add > kMaxLine) return;  // a reply never exceeds one line
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
