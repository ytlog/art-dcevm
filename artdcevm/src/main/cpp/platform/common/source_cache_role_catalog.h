#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace artdcevm::source_cache_role_catalog {
// Owned identities from the original immutable StandardDEX, independent of ART
// cache occupancy. External references have no locally declared access flags.
struct Member {
  std::string declaring_class;
  std::string name;
  std::string descriptor;
  bool declared = false;
  uint32_t access = 0;
};
struct Catalog {
  std::vector<std::string> types;
  std::vector<Member> fields;
  std::vector<Member> methods;
};
// Read every ID, including primitive/array types and external member references.
// Caller authenticates the original source bytes; this is not a checksum or
// mutable runtime-DexFile reader. Prepare before stopped/final verification.
// Output is cleared on failure, including allocation failure; no partial grant.
bool Read(const unsigned char* bytes, size_t size, Catalog* out,
          std::string* error = nullptr) noexcept;
}  // namespace artdcevm::source_cache_role_catalog
