#pragma once

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

// ============================================================================
// PHONE CONTROL: THE COMMANDS (no hardware here, so the host tests cover it)
// ============================================================================
//
// The phone page (docs/remote/index.html) writes one short line of text to
// the totem's command characteristic per button press:
//
//   PIN 1234          unlock this connection (nothing else works until then)
//   SAY HAPPY BDAY    scroll this text now
//   PLAY 0            play the spin now; PLAY 1.. plays that phrase from SHOW_PHRASES
//   STOP              end the show that's on, back to heart rate
//   PAUSE 1 / PAUSE 0 stop / restart the every-5-minutes shows (playing by hand still works)
//   BRIGHT 3          brightness level, 1 = dimmest, same levels as the wheel
//
// Plain text rather than bytes so the serial log and a BLE app like LightBlue
// can show and send them as they are.

namespace phone {

const int MAX_TEXT = 60;   // show::MAX_CHARS: longer is cut there anyway

struct Command {
  enum Kind : uint8_t { NONE, PIN, SAY, PLAY, STOP, PAUSE, BRIGHT };
  Kind kind = NONE;
  int arg = 0;
  char text[MAX_TEXT + 1] = {0};   // PIN: the code tried; SAY: the text
};

// "WORD rest": true and the rest (spaces after the word skipped) if s starts
// with word, case-insensitive, followed by a space or the end.
inline bool startsWord(const char *s, const char *word, const char *&rest) {
  size_t n = strlen(word);
  for (size_t i = 0; i < n; i++) {
    char c = s[i];
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c != word[i]) return false;
  }
  if (s[n] != 0 && s[n] != ' ') return false;
  rest = s + n;
  while (*rest == ' ') rest++;
  return true;
}

// A whole number and nothing after it but spaces.
inline bool parseInt(const char *s, int &out) {
  if (*s < '0' || *s > '9') return false;
  char *end;
  long v = strtol(s, &end, 10);
  while (*end == ' ') end++;
  if (*end != 0 || v > 1000) return false;
  out = (int)v;
  return true;
}

// At most max characters, 0-terminated, trailing whitespace dropped.
inline void copyText(char *dst, const char *src, size_t len, size_t max = MAX_TEXT) {
  if (len > max) len = max;
  memcpy(dst, src, len);
  dst[len] = 0;
  // Trailing spaces and line endings (a BLE app may send "\r\n").
  while (len > 0 && (dst[len - 1] == ' ' || dst[len - 1] == '\r' || dst[len - 1] == '\n')) dst[--len] = 0;
}

// data/len: what was written, not necessarily 0-terminated. False for anything
// that isn't a whole, valid command.
inline bool parse(const uint8_t *data, size_t len, Command &out) {
  char buf[MAX_TEXT + 16];
  copyText(buf, (const char *)data, len, sizeof(buf) - 1);
  out = Command();
  const char *rest;
  if (startsWord(buf, "PIN", rest)) {
    if (!*rest) return false;
    out.kind = Command::PIN;
    copyText(out.text, rest, strlen(rest));
    return true;
  }
  if (startsWord(buf, "SAY", rest)) {
    if (!*rest) return false;
    out.kind = Command::SAY;
    copyText(out.text, rest, strlen(rest));
    return true;
  }
  if (startsWord(buf, "PLAY", rest)) { out.kind = Command::PLAY; return parseInt(rest, out.arg); }
  if (startsWord(buf, "STOP", rest)) { out.kind = Command::STOP; return *rest == 0; }
  if (startsWord(buf, "PAUSE", rest)) {
    out.kind = Command::PAUSE;
    return parseInt(rest, out.arg) && out.arg <= 1;
  }
  if (startsWord(buf, "BRIGHT", rest)) { out.kind = Command::BRIGHT; return parseInt(rest, out.arg); }
  return false;
}

// Same length and same characters, without stopping at the first wrong one.
inline bool pinMatches(const char *tried, const char *pin) {
  size_t a = strlen(tried), b = strlen(pin);
  if (b == 0) return false;   // no PIN set: nobody gets in
  uint8_t diff = a != b;
  for (size_t i = 0; i < a && i < b; i++) diff |= (uint8_t)(tried[i] ^ pin[i]);
  return diff == 0;
}

// Copies s into a JSON string body, escaping what needs it. Returns the
// characters written (out always 0-terminated, never more than cap - 1).
inline int jsonEscape(char *out, int cap, const char *s) {
  int n = 0;
  for (; *s && n < cap - 3; s++) {
    if (*s == '"' || *s == '\\') out[n++] = '\\';
    if ((uint8_t)*s < 0x20) continue;
    out[n++] = *s;
  }
  out[n] = 0;
  return n;
}

// Builds a line of JSON in a fixed buffer. Anything that doesn't fit is
// dropped and ok() goes false, rather than writing past the end.
struct Out {
  char *buf;
  int cap, n = 0;
  bool full = false;
  Out(char *b, int c) : buf(b), cap(c) { buf[0] = 0; }
  void add(const char *fmt, ...) __attribute__((format(printf, 2, 3))) {
    if (full) return;
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(buf + n, cap - n, fmt, ap);
    va_end(ap);
    if (w < 0 || w >= cap - n) { buf[n] = 0; full = true; return; }   // undo the partial piece
    n += w;
  }
  // A JSON string: quoted and escaped.
  void str(const char *s) {
    char e[2 * MAX_TEXT + 8];
    jsonEscape(e, sizeof(e), s);
    add("\"%s\"", e);
  }
  bool ok() const { return !full; }
  int room() const { return cap - n; }
};

} // namespace phone
