#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace artdcevm::evolution {
inline constexpr uint32_t kStatic = 0x0008;
inline constexpr uint32_t kNative = 0x0100;
inline constexpr uint32_t kAbstract = 0x0400;

struct Member {
  std::string name;
  // Field type or complete method descriptor. Names alone never identify members.
  std::string descriptor;
  uint32_t access = 0;
  uint32_t dex_index = UINT32_MAX;
  bool IsStatic() const { return (access & kStatic) != 0; }
};
struct ClassSchema {
  std::string descriptor;
  std::string superclass;
  std::vector<std::string> interfaces;
  uint32_t access = 0;
  std::vector<Member> fields;
  std::vector<Member> methods;
};

// Reads metadata only. ART must still verify method bodies, annotations and encoded values.
// On error the output is unchanged. No ART layout or OS-specific symbol is used here.
bool ReadDexSchema(const std::vector<unsigned char>& dex, ClassSchema* output, std::string* error);
bool IsTypeDescriptor(const std::string& value, bool allow_void);
bool IsMethodDescriptor(const std::string& value);
}
