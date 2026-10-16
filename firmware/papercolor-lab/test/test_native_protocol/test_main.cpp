// Console protocol against the fixed table in README "Console protocol" / issue #17.
#include <unity.h>

#include <cstring>

#include "protocol.h"

using namespace rclab;

struct Row {
  const char* line;
  bool ok;
  Verb verb;
};

// Oracle: the protocol table. Verb-only rows take no arguments; every other
// malformed shape is a parse failure.
static const Row kTable[] = {
    {"info", true, Verb::Info},
    {"info\r", true, Verb::Info},
    {"reset", true, Verb::Reset},
    {"help", true, Verb::Help},
    {"show white native", true, Verb::Show},
    {"show black native", true, Verb::Show},
    {"  show   black native ", true, Verb::Show},
    {"raw 12 00", true, Verb::Raw},
    {"raw 04", true, Verb::Raw},
    {"rawread 70 4", true, Verb::RawRead},
    {"rawread 70 32", true, Verb::RawRead},
    {"trace on", true, Verb::Trace},
    {"trace off", true, Verb::Trace},
    {"demo 0", true, Verb::Demo},
    {"demo 6", true, Verb::Demo},
    {"demo act", true, Verb::Demo},
    {"demo 7", false, Verb::Demo},
    {"demo", false, Verb::Demo},
    {"demo 1 2", false, Verb::Demo},
    {"demo next", false, Verb::Demo},
    {"", false, Verb::Info},
    {"   ", false, Verb::Info},
    {"INFO", false, Verb::Info},
    {"info now", false, Verb::Info},
    {"show white", false, Verb::Show},
    {"show chart native", true, Verb::Show},
    {"show text native", true, Verb::Show},
    {"show invert native", true, Verb::Show},
    {"show checker 40 native", true, Verb::Show},
    {"show checker 300 native", true, Verb::Show},
    {"show checker native", false, Verb::Show},
    {"show checker 0 native", false, Verb::Show},
    {"show checker 301 native", false, Verb::Show},
    {"show checker 040 native", false, Verb::Show},
    {"show checker 40", false, Verb::Show},
    {"show text 40 native", false, Verb::Show},
    {"show text native extra", false, Verb::Show},
    {"show nope native", false, Verb::Show},
    {"show white mono", true, Verb::Show},
    {"show text interrupt", true, Verb::Show},
    {"show text interrupt cut=250", true, Verb::Show},
    {"show text interrupt cut=250 cdi=3F", true, Verb::Show},
    {"show text interrupt cut=49", false, Verb::Show},
    {"show text interrupt cut=5001", false, Verb::Show},
    {"show text interrupt cut=250 cut=340", false, Verb::Show},
    {"show text interrupt cdi=0f", false, Verb::Show},
    {"show text interrupt setup=tickey", false, Verb::Show},
    {"show text mono setup=tickey psr=0FA9 booster=none", true, Verb::Show},
    {"show checker 40 monoclear psr=0FA9", true, Verb::Show},
    {"show chart fast", true, Verb::Show},
    {"show chart quality booster=none", true, Verb::Show},
    {"show text mono psr=0FA9 psr=5F69", false, Verb::Show},
    {"show text mono cut=340", false, Verb::Show},
    {"show text mono setup", false, Verb::Show},
    {"show text mono =tickey", false, Verb::Show},
    {"show text mono setup=", false, Verb::Show},
    {"show text native cut=340", false, Verb::Show},
    {"show text slow", false, Verb::Show},
    {"show text mono psr=0F69", true, Verb::Show},
    {"show text mono psr=1234", false, Verb::Show},
    {"show text mono psr=0FA", false, Verb::Show},
    {"show text monoclear pack=2", true, Verb::Show},
    {"show text mono pack=3", false, Verb::Show},
    {"show chart fast pack=2", false, Verb::Show},
    {"show chart fast pack=2x", false, Verb::Show},
    {"show chart fast pack=4x", true, Verb::Show},
    {"show chart fast pack=4e", true, Verb::Show},
    {"show chart fast pack=2e", false, Verb::Show},
    {"show text mono pack=2e", true, Verb::Show},
    {"show text mono pack=4o", true, Verb::Show},
    {"show text mono pack=2z", false, Verb::Show},
    {"show text mono pack=4x", true, Verb::Show},
    {"show text mono pack=8", false, Verb::Show},
    {"show chart fast tres=200", true, Verb::Show},
    {"show chart fast win=1", true, Verb::Show},
    {"show text custom win=1 pack=2", true, Verb::Show},
    {"lut load mono_clear", true, Verb::Lut},
    {"lut load nope", false, Verb::Lut},
    {"lut set 10 fb", true, Verb::Lut},
    {"lut set 798 fb", true, Verb::Lut},
    {"lut set 798 fb fb", false, Verb::Lut},
    {"lut set 799 fb", false, Verb::Lut},
    {"lut get 0 32", true, Verb::Lut},
    {"lut get 790 10", false, Verb::Lut},
    {"lut get 0 33", false, Verb::Lut},
    {"lut", false, Verb::Lut},
    {"otp", true, Verb::Otp},
    {"show bands custom map=01234567 win=1", true, Verb::Show},
    {"show red custom pack=2 map=10300200", true, Verb::Show},
    {"show purple native", false, Verb::Show},
    {"show grid custom field=0:3", true, Verb::Show},
    {"show grid custom pack=2 field=3:2", true, Verb::Show},
    {"show grid custom pack=2 field=0:3", false, Verb::Show},
    {"show grid custom field=0:4", false, Verb::Show},
    {"show grid custom field=8:1", false, Verb::Show},
    {"show grid custom field=0:1 map=01230123", false, Verb::Show},
    {"show grid mono pack=2 field=0:2", false, Verb::Show},
    {"show chart custom pack=2 map=10300200", true, Verb::Show},
    {"show chart custom map=0123456F", true, Verb::Show},
    {"show chart custom pack=2 map=10400000", false, Verb::Show},
    {"show chart custom map=0123456", false, Verb::Show},
    {"show chart custom map=0123456g", false, Verb::Show},
    {"show bands mono map=01234567", false, Verb::Show},
    {"otp 0", false, Verb::Otp},
    {"show chart fast win=2", false, Verb::Show},
    {"show grid custom pll=07 pack=2 field=0:2", true, Verb::Show},
    {"show grid custom pll=7 pack=2 field=0:2", false, Verb::Show},
    {"show grid native pll=07", false, Verb::Show},
    {"show bands custom psr=1FA9 map=01234567", true, Verb::Show},
    {"show bands custom psr=4FA9 map=01234567", false, Verb::Show},
    {"show chart fast tres=300", false, Verb::Show},
    {"show text interrupt tres=200", false, Verb::Show},
    {"raw", false, Verb::Raw},
    {"raw 0x12", false, Verb::Raw},
    {"raw 1", false, Verb::Raw},
    {"raw zz", false, Verb::Raw},
    {"rawread 70", false, Verb::RawRead},
    {"rawread 70 0", false, Verb::RawRead},
    {"rawread 70 33", false, Verb::RawRead},
    {"rawread 7 4", false, Verb::RawRead},
    {"trace", false, Verb::Trace},
    {"trace maybe", false, Verb::Trace},
    {"cleanup", true, Verb::Cleanup},
    {"cleanup now", false, Verb::Cleanup},
};

void test_protocol_table() {
  for (const auto& row : kTable) {
    Command cmd;
    const char* why = nullptr;
    const bool ok = parse_command(row.line, cmd, why);
    TEST_ASSERT_EQUAL_MESSAGE(row.ok, ok, row.line);
    if (ok) TEST_ASSERT_EQUAL_MESSAGE(int(row.verb), int(cmd.verb), row.line);
    else TEST_ASSERT_NOT_NULL_MESSAGE(why, row.line);
  }
}

void test_show_arguments() {
  Command cmd;
  const char* why = nullptr;
  TEST_ASSERT_TRUE(parse_command("show black native", cmd, why));
  TEST_ASSERT_EQUAL(int(Image::Black), int(cmd.image));
  TEST_ASSERT_EQUAL(int(Mode::Native), int(cmd.mode));
  TEST_ASSERT_TRUE(parse_command("show white native", cmd, why));
  TEST_ASSERT_EQUAL(int(Image::White), int(cmd.image));
  TEST_ASSERT_TRUE(parse_command("show checker 40 native", cmd, why));
  TEST_ASSERT_EQUAL(int(Image::Checker), int(cmd.image));
  TEST_ASSERT_EQUAL(40, cmd.checker);
  TEST_ASSERT_FALSE(parse_command("show checker 0 native", cmd, why));
  TEST_ASSERT_EQUAL_STRING("size", why);
  TEST_ASSERT_FALSE(parse_command("show nope native", cmd, why));
  TEST_ASSERT_EQUAL_STRING("image", why);
}

void test_knob_defaults_and_values() {
  Command cmd;
  const char* why = nullptr;
  TEST_ASSERT_TRUE(parse_command("show text interrupt", cmd, why));
  TEST_ASSERT_EQUAL(int(Mode::Interrupt), int(cmd.mode));
  TEST_ASSERT_EQUAL(340, cmd.knobs.cut_ms);  // FreeInk REFRESH_CUTOFF_MS
  TEST_ASSERT_EQUAL_HEX8(0x0F, cmd.knobs.cdi);
  TEST_ASSERT_TRUE(parse_command("show text interrupt cdi=3F cut=450", cmd, why));
  TEST_ASSERT_EQUAL(450, cmd.knobs.cut_ms);
  TEST_ASSERT_EQUAL_HEX8(0x3F, cmd.knobs.cdi);
  // LUT modes default to native values.
  TEST_ASSERT_TRUE(parse_command("show text mono", cmd, why));
  TEST_ASSERT_FALSE(cmd.knobs.setup_tickey);
  TEST_ASSERT_EQUAL_HEX8(0x5F, cmd.knobs.psr[0]);
  TEST_ASSERT_EQUAL_HEX8(0x69, cmd.knobs.psr[1]);
  TEST_ASSERT_TRUE((cmd.knobs.pack == Pack{4, Rows::Once}));
  TEST_ASSERT_TRUE(cmd.knobs.booster);
  TEST_ASSERT_TRUE(parse_command("show chart quality booster=none psr=0FA9 setup=tickey", cmd, why));
  TEST_ASSERT_EQUAL(int(Mode::Quality), int(cmd.mode));
  TEST_ASSERT_TRUE(cmd.knobs.setup_tickey);
  TEST_ASSERT_EQUAL_HEX8(0x0F, cmd.knobs.psr[0]);
  TEST_ASSERT_EQUAL_HEX8(0xA9, cmd.knobs.psr[1]);
  TEST_ASSERT_FALSE(cmd.knobs.booster);
  TEST_ASSERT_TRUE(parse_command("show text mono psr=5FA9 pack=2", cmd, why));
  TEST_ASSERT_EQUAL_HEX8(0x5F, cmd.knobs.psr[0]);
  TEST_ASSERT_EQUAL_HEX8(0xA9, cmd.knobs.psr[1]);
  TEST_ASSERT_TRUE((cmd.knobs.pack == Pack{2, Rows::Once}));
  TEST_ASSERT_TRUE(parse_command("show text mono pack=2o", cmd, why));
  TEST_ASSERT_TRUE((cmd.knobs.pack == Pack{2, Rows::Odd}));
  TEST_ASSERT_EQUAL_STRING("2o", pack_name(cmd.knobs.pack));
  TEST_ASSERT_FALSE(parse_command("show text mono cut=340", cmd, why));
  TEST_ASSERT_EQUAL_STRING("knob", why);
}

void test_raw_bytes() {
  Command cmd;
  const char* why = nullptr;
  TEST_ASSERT_TRUE(parse_command("raw 06 6F 1f 17 27", cmd, why));
  TEST_ASSERT_EQUAL(5, cmd.count);
  const uint8_t expected[] = {0x06, 0x6F, 0x1F, 0x17, 0x27};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, cmd.bytes, 5);
}

void test_rawread_arguments() {
  Command cmd;
  const char* why = nullptr;
  TEST_ASSERT_TRUE(parse_command("rawread 70 4", cmd, why));
  TEST_ASSERT_EQUAL_HEX8(0x70, cmd.read_cmd);
  TEST_ASSERT_EQUAL(4, cmd.read_count);
}

void test_trace_flag() {
  Command cmd;
  const char* why = nullptr;
  TEST_ASSERT_TRUE(parse_command("trace on", cmd, why));
  TEST_ASSERT_TRUE(cmd.trace_on);
  TEST_ASSERT_TRUE(parse_command("trace off", cmd, why));
  TEST_ASSERT_FALSE(cmd.trace_on);
}

void test_reply_format() {
  // Reply lines per protocol: status word, then space-separated key=value.
  TEST_ASSERT_EQUAL_STRING("OK busy_ms=1234 total_ms=16010",
                           Reply(true).field("busy_ms", 1234L).field("total_ms", 16010L).c_str());
  TEST_ASSERT_EQUAL_STRING("FAIL stage=drive-release busy=0 ms=25000",
                           Reply(false).field("stage", "drive-release").field("busy", 0L).field("ms", 25000L).c_str());
  const uint8_t rev[] = {0x00, 0xA5, 0xFF};
  TEST_ASSERT_EQUAL_STRING("OK rev=00a5ff", Reply(true).hex("rev", rev, 3).c_str());
  TEST_ASSERT_EQUAL_STRING("FAIL stage=parse reason=verb",
                           Reply(false).field("stage", "parse").field("reason", "verb").c_str());
  // Over one line: never truncated, the whole reply becomes a FAIL.
  Reply long_reply(true);
  for (int i = 0; i < 30; ++i) long_reply.field("key", "value");
  TEST_ASSERT_EQUAL_STRING("FAIL stage=reply reason=length", long_reply.c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_protocol_table);
  RUN_TEST(test_show_arguments);
  RUN_TEST(test_knob_defaults_and_values);
  RUN_TEST(test_raw_bytes);
  RUN_TEST(test_rawread_arguments);
  RUN_TEST(test_trace_flag);
  RUN_TEST(test_reply_format);
  return UNITY_END();
}
