#pragma once
#include <cstddef>
#include <cstdint>

namespace artdcevm::named_symbol_extent {
// Preparation-only service. A successful proof has no pointers to ELF tables.
class Service final {
 public:
  Service() = default;
  Service(const Service&) = delete;
  Service& operator=(const Service&) = delete;
  bool Prepare(const char* loaded_module, const char* exact_name);
  bool ContainsReturnPc(uintptr_t pc) const {
    return begin_ != 0 && pc > begin_ && pc < end_;
  }
  bool MatchesEntry(const void* entry) const {
    return begin_ != 0 && begin_ == reinterpret_cast<uintptr_t>(entry);
  }
 private:
  uintptr_t begin_ = 0;
  uintptr_t end_ = 0;
};
}  // namespace artdcevm::named_symbol_extent
