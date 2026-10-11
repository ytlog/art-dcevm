#pragma once
#include <cstdint>
#include <cstddef>
#include <utility>
#include <vector>
#include <array>
namespace artdcevm::method_identity {
class ReadableMappingSnapshot;
// Ordinary-only explicit read view for ONE immediate native traversal. Construct
// after the last JNI/safepoint and destroy before the next one. The caller must
// independently own stable rooted allocations and mutator access; readable VMAs
// are never an identity, GC, stop or publication permission. Misses query the
// actual platform immediately, so this view MUST NOT serve final no-OS readers.
// No process-wide/TLS cache is installed and no raw object identity is retained.
class ImmediateReadableRegions final {
 public:
  static constexpr size_t kCapacity=16;
  ImmediateReadableRegions() noexcept=default;
  ImmediateReadableRegions(const ImmediateReadableRegions&)=delete;
  ImmediateReadableRegions& operator=(const ImmediateReadableRegions&)=delete;
  bool Extent(const void*,size_t*) noexcept;
  size_t OsQueries()const noexcept{return os_queries_;}
 private:
  std::array<std::pair<uintptr_t,uintptr_t>,kCapacity> regions_{};
  size_t count_=0,next_=0,os_queries_=0;
};
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
  // Shared typed callback for explicit ReadContexts. The owner is this live
  // snapshot; no OS discovery, root identity or mutator permission is added.
  static bool ReadExtent(void*owner,const void*,size_t*)noexcept;
  bool Ready()const noexcept{return complete_;}
 private:
  friend class ReadableMappingSnapshot;
  friend bool ReadableMappingExtent(const void*,size_t*);
  bool Find(uintptr_t,size_t*);
  ScopedReadableMappings* previous_=nullptr;
  std::vector<std::pair<uintptr_t,uintptr_t>> regions_;
  size_t os_queries_=0;
  bool complete_=false;
};
// Explicit bounded VMA facts without an ambient/TLS lookup scope. Captured in
// ordinary context; the caller separately gates every later read on its actual
// GC-stable source window. It never lends stale mappings to unrelated callees,
// and an extent miss never queries the OS or installs a fallback scope.
class ReadableMappingSnapshot final {
 public:
  ReadableMappingSnapshot()noexcept;
  ReadableMappingSnapshot(const ReadableMappingSnapshot&)=delete;
  ReadableMappingSnapshot&operator=(const ReadableMappingSnapshot&)=delete;
  bool Ready()const noexcept{return complete_;}
  bool Extent(const void*,size_t*)const noexcept;
 private:
  std::vector<std::pair<uintptr_t,uintptr_t>>regions_;
  bool complete_=false;
};
}
