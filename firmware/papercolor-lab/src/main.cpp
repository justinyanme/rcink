// papercolor-lab: boot to an idle console, drive the panel only on command.
#include <Arduino.h>
#include <esp_mac.h>

#include <cstring>

#include "board.h"
#include "epd.h"
#include "fatal.h"
#include "frame.h"
#include "protocol.h"

#ifndef RCLAB_BUILD
#error "RCLAB_BUILD is set by build_hash.py"
#endif

namespace {

using namespace rclab;

constexpr uint8_t kCmdRevision = 0x70;
constexpr size_t kRevisionBytes = 4;
constexpr uint8_t kCmdTemperature = 0x40;  // TSC: reads 2 bytes
constexpr size_t kTemperatureBytes = 2;

uint8_t* g_inks = nullptr;    // kPixels ink codes (PSRAM)
uint8_t* g_packed = nullptr;  // kNativeFrameBytes (PSRAM)
char g_line[kMaxLine + 1];
size_t g_line_len = 0;
char g_mac[18];

void read_mac() {
  uint8_t mac[6];
  if (esp_efuse_mac_get_default(mac) != ESP_OK) fatal("efuse mac read failed");
  snprintf(g_mac, sizeof(g_mac), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

bool uniform(const uint8_t* bytes, size_t n, uint8_t value) {
  for (size_t i = 0; i < n; ++i) if (bytes[i] != value) return false;
  return true;
}

void reply(const Reply& r) {
  Serial.println(r.c_str());
  if (Serial) Serial.flush();
}

void reply_fail(const epd::Result& r) {
  reply(Reply(false).field("stage", r.stage).field("busy", long(r.busy)).field("ms", long(r.ms)));
}

void do_info() {
  uint8_t rev[kRevisionBytes], temp[kTemperatureBytes];
  epd::read_register(kCmdRevision, rev, sizeof(rev));
  epd::read_register(kCmdTemperature, temp, sizeof(temp));
  reply(Reply(true)
            .field("mac", g_mac)
            .hex("rev", rev, sizeof(rev))
            .field("build", RCLAB_BUILD)
            .hex("temp", temp, sizeof(temp))
            .field("uptime_ms", long(millis())));
}

void do_reset() {
  const uint32_t began = millis();
  epd::hard_reset();
  if (auto r = epd::init_native(); !r.ok()) return reply_fail(r);
  reply(Reply(true).field("total_ms", long(millis() - began)));
}

void do_show(const Command& cmd) {
  const uint32_t began = millis();
  memset(g_inks, cmd.image == Image::White ? InkWhite : InkBlack, kPixels);
  encode_native_frame(g_inks, g_packed);
  if (auto r = epd::upload_frame(g_packed, kNativeFrameBytes); !r.ok()) return reply_fail(r);
  if (auto r = epd::power_on(); !r.ok()) return reply_fail(r);
  uint32_t busy_ms = 0;
  if (auto r = epd::drive(epd::Mode::Native, busy_ms); !r.ok()) return reply_fail(r);
  if (auto r = epd::power_off(); !r.ok()) return reply_fail(r);
  reply(Reply(true)
            .field("image", image_name(cmd.image))
            .field("mode", mode_name(cmd.mode))
            .field("busy_ms", long(busy_ms))
            .field("total_ms", long(millis() - began)));
}

void dispatch(const char* line) {
  Command cmd;
  const char* why = nullptr;
  if (!parse_command(line, cmd, why)) {
    return reply(Reply(false).field("stage", "parse").field("reason", why));
  }
  switch (cmd.verb) {
    case Verb::Info: return do_info();
    case Verb::Reset: return do_reset();
    case Verb::Show: return do_show(cmd);
    case Verb::Raw:
      epd::write(cmd.bytes[0], cmd.bytes + 1, cmd.count - 1);
      return reply(Reply(true).hex("cmd", cmd.bytes, 1).field("len", long(cmd.count - 1)));
    case Verb::RawRead: {
      uint8_t data[kMaxReadBytes];
      epd::read_register(cmd.read_cmd, data, cmd.read_count);
      return reply(Reply(true).hex("cmd", &cmd.read_cmd, 1).hex("data", data, cmd.read_count));
    }
    case Verb::Trace:
      epd::set_trace(cmd.trace_on);
      return reply(Reply(true).field("trace", cmd.trace_on ? "on" : "off"));
    case Verb::Help:
      return reply(Reply(true)
                       .field("verbs", "info,reset,show,raw,rawread,trace,help")
                       .field("show", "<white|black> native")
                       .field("raw", "<cmd> [data...] hex2")
                       .field("rawread", "<cmd> <1..32>")
                       .field("trace", "<on|off>"));
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  read_mac();
  const uint8_t supply = board::begin();
  epd::begin();
  epd::hard_reset();
  if (auto r = epd::init_native(); !r.ok()) fatal("panel init stage=%s busy=%d ms=%u", r.stage, r.busy, unsigned(r.ms));
  uint8_t rev[kRevisionBytes];
  epd::read_register(kCmdRevision, rev, sizeof(rev));
  if (uniform(rev, sizeof(rev), 0x00) || uniform(rev, sizeof(rev), 0xFF)) {
    fatal("panel absent: register 0x70 = %02x%02x%02x%02x", rev[0], rev[1], rev[2], rev[3]);
  }
  g_inks = static_cast<uint8_t*>(ps_malloc(kPixels));
  g_packed = static_cast<uint8_t*>(ps_malloc(kNativeFrameBytes));
  if (!g_inks || !g_packed) fatal("psram alloc failed");
  Serial.printf("# ready build=%s mac=%s rev=%02x%02x%02x%02x supply=0x%02x uptime_ms=%u\n",
                RCLAB_BUILD, g_mac, rev[0], rev[1], rev[2], rev[3], supply, unsigned(millis()));
}

void loop() {
  // HWCDC flush() while the host is detached discards the TX queue
  // (Arduino 2.0.17); the bool conversion reports the connection handshake.
  if (Serial) Serial.flush();
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c < 0) break;
    if (c == '\n') {
      g_line[g_line_len] = 0;
      dispatch(g_line);
      g_line_len = 0;
    } else if (g_line_len < kMaxLine) {
      g_line[g_line_len++] = char(c);
    } else {
      g_line_len = 0;  // overlong line: drop it; the next newline reports parse failure
      reply(Reply(false).field("stage", "parse").field("reason", "length"));
    }
  }
  delay(5);
}
