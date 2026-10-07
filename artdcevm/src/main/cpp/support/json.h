#pragma once
#include <string>
namespace artdcevm {
// Inputs from DEX/JVMTI use modified UTF-8; output is ASCII JSON with UTF-16 escapes.
inline std::string QuoteJsonMutf8(const std::string& value) {
  const char hex[] = "0123456789abcdef";
  std::string output = "\"";
  for (size_t i = 0; i < value.size();) {
    unsigned c = static_cast<unsigned char>(value[i++]);
    if (c >= 0xc0 && c < 0xe0 && i < value.size()) {
      c = ((c & 0x1f) << 6) | (static_cast<unsigned char>(value[i++]) & 0x3f);
    } else if (c >= 0xe0 && c < 0xf0 && i + 1 < value.size()) {
      c = ((c & 0x0f) << 12) | ((static_cast<unsigned char>(value[i]) & 0x3f) << 6) |
          (static_cast<unsigned char>(value[i + 1]) & 0x3f);
      i += 2;
    }
    if (c == '"' || c == '\\') { output += '\\'; output += static_cast<char>(c); }
    else if (c >= 32 && c < 127) output += static_cast<char>(c);
    else { output += "\\u"; for (int shift = 12; shift >= 0; shift -= 4) output += hex[(c >> shift) & 15]; }
  }
  return output + '"';
}
}
