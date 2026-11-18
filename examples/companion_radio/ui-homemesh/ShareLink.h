#pragma once

// Contact link for the QR code, in the format the MeshCore app scans
// (docs/qr_codes.md):
//   meshcore://contact/add?name=Example+Contact&public_key=<64 hex>&type=1

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace homemesh {

static const int CONTACT_TYPE_COMPANION = 1;

// Form-style URL encoding: space becomes '+', everything except
// letters, digits and "-_.~" becomes %XX (UTF-8 bytes one by one).
inline size_t urlEncodeName(const char* in, char* out, size_t cap) {
  static const char HEX_DIGITS[] = "0123456789ABCDEF";
  size_t o = 0;
  if (cap == 0) return 0;
  for (; *in; in++) {
    uint8_t c = (uint8_t)*in;
    bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                 c == '-' || c == '_' || c == '.' || c == '~';
    size_t need = (plain || c == ' ') ? 1 : 3;
    if (o + need >= cap) break;
    if (plain) {
      out[o++] = (char)c;
    } else if (c == ' ') {
      out[o++] = '+';
    } else {
      out[o++] = '%';
      out[o++] = HEX_DIGITS[c >> 4];
      out[o++] = HEX_DIGITS[c & 15];
    }
  }
  out[o] = 0;
  return o;
}

// Returns false if the link does not fit.
inline bool buildContactLink(const char* name, const uint8_t pub_key[32], int type, char* out, size_t cap) {
  static const char HEX_DIGITS[] = "0123456789abcdef";
  char enc[3 * 32 + 1];
  urlEncodeName(name, enc, sizeof(enc));
  char hex[65];
  for (int i = 0; i < 32; i++) {
    hex[2 * i] = HEX_DIGITS[pub_key[i] >> 4];
    hex[2 * i + 1] = HEX_DIGITS[pub_key[i] & 15];
  }
  hex[64] = 0;
  int n = snprintf(out, cap, "meshcore://contact/add?name=%s&public_key=%s&type=%d", enc, hex, type);
  return n > 0 && (size_t)n < cap;
}

}  // namespace homemesh
