#pragma once
#include <cstdint>
#include <cstddef>
#include <utility>
#include <vector>
namespace artdcevm::method_identity {
// A fresh mapping snapshot for one immediate native read traversal. The caller
// must separately own stable rooted objects and prohibit JNI/safepoints for the
// interval. A snapshot grants no mutator or GC permission; recapture after
// every possible safepoint. Exclusive disabled-GC traversals also use it.
class ScopedReadableMappings final {
 public:
  ScopedReadableMappings() noexcept;
  ~ScopedReadableMappings();
  ScopedReadableMappings(const ScopedReadableMappings&)=delete;
  ScopedReadableMappings& operator=(const ScopedReadableMappings&)=delete;
  size_t OsQueries()const{return os_queries_;}
  // Explicit views never query the OS on a miss or incomplete capture.
  bool Extent(const void*,size_t*)const noexcept;
  bool Ready()const noexcept{return complete_;}
 private:
  friend bool ReadableMappingExtent(const void*,size_t*);
  bool Find(uintptr_t,size_t*);
  ScopedReadableMappings* previous_=nullptr;
  std::vector<std::pair<uintptr_t,uintptr_t>> regions_;
  size_t os_queries_=0;
  bool complete_=false;
};
}
