#pragma once

// Picks real alerts out of the warning channel.
//
// A warning bot posts alerts as "[!] DWD | Sturmböen | Landkreis | /details".
// Only text that starts with "[!] " counts; status replies, tests and queries
// do not. The home screen shows "DWD · Sturmböen · Landkreis".

#include <stddef.h>
#include <string.h>
#include <helpers/UTF8Helpers.h>

namespace homemesh {

static const char WARNING_CHANNEL[] = "warnings";

// Channel names from the app may carry a leading '#'.
inline const char* plainChannelName(const char* name) {
  return name[0] == '#' ? name + 1 : name;
}

inline bool isWarningChannel(const char* name) {
  const char* n = plainChannelName(name);
  for (size_t i = 0;; i++) {
    char a = n[i], b = WARNING_CHANNEL[i];
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (a != b) return false;
    if (!a) return true;
  }
}

// body = message text after "Sender: ". Returns false if it is not an alert.
inline bool extractWarning(const char* body, char* out, size_t cap) {
  if (!body || cap < 2 || strncmp(body, "[!] ", 4) != 0) return false;
  const char* s = body + 4;
  while (*s == ' ') s++;
  size_t len = strlen(s);
  // cut a trailing "| /details" hint
  const char* hint = strstr(s, "/details");
  if (hint && hint[8] == 0) {
    const char* e = hint;
    while (e > s && (e[-1] == ' ' || e[-1] == '|')) e--;
    len = (size_t)(e - s);
  }
  while (len > 0 && s[len - 1] == ' ') len--;
  if (len == 0) return false;

  size_t o = 0;
  for (size_t i = 0; i < len && o + 1 < cap;) {
    if (strncmp(s + i, " | ", 3) == 0 && i + 3 <= len) {
      // U+00B7 middle dot, UTF-8 C2 B7
      if (o + 5 >= cap) break;
      out[o++] = ' '; out[o++] = (char)0xC2; out[o++] = (char)0xB7; out[o++] = ' ';
      i += 3;
    } else {
      out[o++] = s[i++];
    }
  }
  out[o] = 0;
  out[mesh::validUtf8PrefixLength(out, o)] = 0;
  return o > 0;
}

}  // namespace homemesh
