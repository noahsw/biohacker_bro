// Host tests for the totem's phone commands (totem/phone_protocol.h): what
// the phone page writes is read the way it means, anything else is refused,
// the PIN check, and the status JSON never runs past its buffer.

#include "../totem/phone_protocol.h"

#include <cstdio>
#include <cstring>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)

static bool parse(const char *s, phone::Command &c) { return phone::parse((const uint8_t *)s, strlen(s), c); }

int main() {
  phone::Command c;

  // --- Each command, as the page sends it ---
  CHECK(parse("PIN 4821", c) && c.kind == phone::Command::PIN && !strcmp(c.text, "4821"), "PIN");
  CHECK(parse("SAY HAPPY BDAY SAM!", c) && c.kind == phone::Command::SAY && !strcmp(c.text, "HAPPY BDAY SAM!"), "SAY");
  CHECK(parse("PLAY 0", c) && c.kind == phone::Command::PLAY && c.arg == 0, "PLAY 0");
  CHECK(parse("PLAY 3", c) && c.kind == phone::Command::PLAY && c.arg == 3, "PLAY 3");
  CHECK(parse("STOP", c) && c.kind == phone::Command::STOP, "STOP");
  CHECK(parse("PAUSE 1", c) && c.kind == phone::Command::PAUSE && c.arg == 1, "PAUSE 1");
  CHECK(parse("PAUSE 0", c) && c.kind == phone::Command::PAUSE && c.arg == 0, "PAUSE 0");
  CHECK(parse("BRIGHT 5", c) && c.kind == phone::Command::BRIGHT && c.arg == 5, "BRIGHT 5");
  CHECK(parse("POWER 0", c) && c.kind == phone::Command::POWER && c.arg == 0, "POWER 0");
  CHECK(parse("power 1\r\n", c) && c.kind == phone::Command::POWER && c.arg == 1, "power 1");

  // --- Typed by hand in a BLE app: lower case, line endings, extra spaces ---
  CHECK(parse("say hi there\r\n", c) && c.kind == phone::Command::SAY && !strcmp(c.text, "hi there"), "say + CRLF: '%s'", c.text);
  CHECK(parse("play   2 ", c) && c.kind == phone::Command::PLAY && c.arg == 2, "play with spaces");

  // --- Refused ---
  const char *bad[] = {"", "SAY", "SAY   ", "PIN", "PLAY", "PLAY x", "PLAY -1", "PLAY 2x", "PAUSE 2", "POWER", "POWER 2", "POWER on", "POWERS 1",
                       "STOP now", "SAYHI", "BRIGHTNESS 3", "HELLO", "PLAY 99999"};
  for (const char *b : bad) CHECK(!parse(b, c), "accepted '%s'", b);

  // Not 0-terminated, as a BLE write arrives.
  const uint8_t raw[] = {'P', 'L', 'A', 'Y', ' ', '1', 'X', 'X'};
  CHECK(phone::parse(raw, 6, c) && c.arg == 1, "length, not terminator");

  // --- A long SAY is cut at MAX_TEXT, never overflows ---
  char longCmd[300] = "SAY ";
  for (int i = 4; i < 299; i++) longCmd[i] = 'A';
  longCmd[299] = 0;
  CHECK(parse(longCmd, c) && strlen(c.text) == (size_t)phone::MAX_TEXT, "long SAY is %d", (int)strlen(c.text));

  // --- PIN ---
  CHECK(phone::pinMatches("4821", "4821"), "right PIN");
  CHECK(!phone::pinMatches("4822", "4821"), "wrong PIN");
  CHECK(!phone::pinMatches("482", "4821"), "short PIN");
  CHECK(!phone::pinMatches("48210", "4821"), "long PIN");
  CHECK(!phone::pinMatches("", ""), "no PIN set lets nobody in");

  // --- JSON ---
  char e[32];
  phone::jsonEscape(e, sizeof(e), "SAY \"HI\" \\ OK");
  CHECK(!strcmp(e, "SAY \\\"HI\\\" \\\\ OK"), "escape: %s", e);

  char buf[24];
  phone::Out o(buf, sizeof(buf));
  o.add("{\"a\":%d", 1);
  o.str("HELLO");
  CHECK(o.ok() && !strcmp(buf, "{\"a\":1\"HELLO\""), "out: %s", buf);
  o.str("THIS ONE DOES NOT FIT AT ALL");   // dropped whole, not cut mid-string
  CHECK(!o.ok() && !strcmp(buf, "{\"a\":1\"HELLO\""), "overflow kept: %s", buf);
  CHECK(strlen(buf) < sizeof(buf), "stays in bounds");

  if (failures) { std::printf("phone protocol: %d failure(s)\n", failures); return 1; }
  std::printf("phone protocol: all passed\n");
  return 0;
}
