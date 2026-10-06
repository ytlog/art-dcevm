#include "class_linker.h"
#include "retired_fields.h"
#include "retired_methods.h"
#include "thread.h"
#include <atomic>

namespace {
void* original_delete_loader = nullptr;
std::atomic<uint64_t> delete_loader_calls{0};
std::atomic<uint64_t> pruned_fields{0};
std::atomic<uint64_t> pruned_methods{0};
}
extern "C" void** ArtDcevmSourceConfigureClassLoaderDeletion() {
  return &original_delete_loader;
}
extern "C" uint64_t ArtDcevmSourceClassLoaderDeleteCount() {
  return delete_loader_calls.load(std::memory_order_relaxed);
}
extern "C" uint64_t ArtDcevmSourceRetiredPrunedCount(int kind) {
  return kind == 0 ? pruned_fields.load(std::memory_order_relaxed)
                   : pruned_methods.load(std::memory_order_relaxed);
}
namespace art {
void ClassLinker::DeleteClassLoader(Thread* self, const ClassLoaderData& data, bool cleanup_cha) {
  delete_loader_calls.fetch_add(1, std::memory_order_relaxed);
  // The class table's LinearAlloc still owns retired members here. Clear all
  // native pointer registries before ART deletes that allocator below.
  if (data.allocator != nullptr) {
    pruned_fields.fetch_add(artdcevm::android13::source::PruneRetiredFields(data.allocator),
                            std::memory_order_relaxed);
    pruned_methods.fetch_add(artdcevm::android13::source::PruneRetiredMethods(data.allocator),
                             std::memory_order_relaxed);
  }
  using Original = void (*)(ClassLinker*, Thread*, const ClassLoaderData&, bool);
  reinterpret_cast<Original>(original_delete_loader)(this, self, data, cleanup_cha);
}
}
