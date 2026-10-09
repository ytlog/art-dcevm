#pragma once
#include <cstdint>
#include <cstddef>
#include <utility>
#include <vector>
namespace artdcevm::method_identity {
// A short-lived read-only mapping snapshot for a single original ART exclusive,
// disabled-GC reflective traversal. No ART mapping/lifetime may change in this
// scope; every new traversal captures again. Never retain this across GC.
class ScopedReadableMappings final {
 public:
  ScopedReadableMappings();
  ~ScopedReadableMappings();
  ScopedReadableMappings(const ScopedReadableMappings&)=delete;
  ScopedReadableMappings& operator=(const ScopedReadableMappings&)=delete;
  size_t OsQueries()const{return os_queries_;}
 private:
  friend bool ReadableMappingExtent(const void*,size_t*);
  bool Find(uintptr_t,size_t*);
  ScopedReadableMappings* previous_=nullptr;
  std::vector<std::pair<uintptr_t,uintptr_t>> regions_;
  size_t os_queries_=0;
  bool complete_=false;
};
}
