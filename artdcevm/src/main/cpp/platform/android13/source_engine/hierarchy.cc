#include "hierarchy.h"
#include "platform/android13/hierarchy_validation_abi.h"
#include "platform/symbols/android13/xiaomi/source_engine_abi.h"
#include "class_linker-inl.h"
#include "dex/dex_file-inl.h"
#include "mirror/class-inl.h"
#include "mirror/dex_cache-inl.h"
#include "mirror/iftable-inl.h"
#include "runtime.h"
#include "thread-current-inl.h"
#include "verifier/class_verifier.h"
#include <atomic>
#include <cstddef>
#include <cstring>
#include <new>
#include <type_traits>
#include <vector>
#include <android/log.h>

namespace {
artdcevm::android13::BeginHierarchyState begin_state = nullptr;
artdcevm::android13::EndHierarchyState end_state = nullptr;
artdcevm::android13::HierarchyStateQuery changed_state = nullptr;
artdcevm::android13::HierarchyStateQuery narrowed_state = nullptr;
}
extern "C" void ArtDcevmSourceConfigureHierarchyState(
    artdcevm::android13::BeginHierarchyState begin, artdcevm::android13::EndHierarchyState end,
    artdcevm::android13::HierarchyStateQuery changed, artdcevm::android13::HierarchyStateQuery narrowed) {
  begin_state = begin; end_state = end; changed_state = changed; narrowed_state = narrowed;
}
namespace artdcevm::android13::source {
HierarchyTransactionScope::HierarchyTransactionScope() : previous_(begin_state()) {}
HierarchyTransactionScope::~HierarchyTransactionScope() { end_state(previous_); }
bool HasHierarchyChange() { return changed_state(); }
bool HasHierarchyNarrowing() { return narrowed_state(); }
}
