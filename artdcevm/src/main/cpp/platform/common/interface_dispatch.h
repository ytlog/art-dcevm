#pragma once
#include <jni.h>
#include "platform/common/runtime_thread.h"
#include <string>
#include <string_view>
#include <vector>
#include "platform/abi/arm64/common/redefinition.h"
#include "platform/abi/arm64/common/runtime_objects.h"

// Read-only validation of ART's selected interface targets. Rebuilding and
// cleanup remain in interface_redefinition; the commit remains ART's own.
namespace artdcevm::interface_dispatch {
struct Services {
  abi::arm64::ArtField* (*find_instance_field)(void*, std::string_view, std::string_view);
  void* (*mirror_class)(abi::arm64::ClassRedefinitionPrefix*);
  jclass class_type;  // Borrowed process-lived global root.
};
void Prepare(JNIEnv* jni, const Services& services);
bool Available(const runtime_thread::VerifiedThread& thread,
               abi::arm64::ClassRedefinitionPrefix* self, bool early_link_errors_available);
std::string State();
// Linked groups: validate every pending obligation owned by this data item.
bool Check(const runtime_thread::VerifiedThread& thread, abi::arm64::ClassRedefinitionPrefix* self,
           const abi::arm64::RedefinitionDataIter& data, std::string* rejection);

// ART 13's named NO_INLINE error service reports this actual selected pair.
// Pointers are native metadata, rooted by the allocation attempt's class roots.
struct NonPublicSelection {
  abi::arm64::ArtMethod* implementation = nullptr;
  abi::arm64::ArtMethod* declaration = nullptr;
};
// Format only an observed selection matching a pending candidate interface.
// Called outside LinkClass locks, after ART has preserved/cleared its exception.
bool DescribeNonPublicSelection(const runtime_thread::VerifiedThread& thread, abi::arm64::ClassRedefinitionPrefix* self,
    const std::vector<abi::arm64::Handle>& originals, abi::arm64::Handle candidates,
    int32_t index, NonPublicSelection selection, const std::string& concrete_descriptor,
    bool submitted, std::string* rejection);
}  // namespace artdcevm::interface_dispatch
