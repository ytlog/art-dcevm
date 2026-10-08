#include "platform/common/method_identity_calibration.h"
#include <cstdio>
#include <limits>
#include <set>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif

namespace artdcevm::method_identity {
bool ReadableMappingExtent(const void* address, size_t* extent) {
  if (!address || !extent) return false;
  uintptr_t pointer = reinterpret_cast<uintptr_t>(address);
#if defined(__ANDROID__) && defined(__aarch64__)
  // Android arm64 native allocations may carry a TBI/MTE tag in the top
  // byte, while /proc/self/maps describes untagged virtual address ranges.
  // Normalize only this integer lookup key: keep the original tagged pointer
  // for every memory access, identity comparison and allocator operation.
  pointer &= UINT64_C(0x00ffffffffffffff);
#endif
#if defined(__APPLE__)
  mach_vm_address_t start = pointer;
  mach_vm_size_t size = 0;
  vm_region_basic_info_data_64_t info{};
  mach_msg_type_number_t length = VM_REGION_BASIC_INFO_COUNT_64;
  mach_port_t object = MACH_PORT_NULL;
  const auto status = mach_vm_region(mach_task_self(), &start, &size,
      VM_REGION_BASIC_INFO_64, reinterpret_cast<vm_region_info_t>(&info), &length, &object);
  if (object != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), object);
  if (status != KERN_SUCCESS || !(info.protection & VM_PROT_READ) ||
      pointer < start || pointer - start >= size) return false;
  *extent = static_cast<size_t>(size - (pointer - start));
  return true;
#else
  FILE* maps = std::fopen("/proc/self/maps", "r");
  if (!maps) return false;
  char line[1024];
  bool found = false;
  while (std::fgets(line, sizeof(line), maps)) {
    unsigned long long start = 0, end = 0;
    char protection[5]{};
    if (std::sscanf(line, "%llx-%llx %4s", &start, &end, protection) != 3 ||
        protection[0] != 'r' || end <= start || pointer < start || pointer >= end) continue;
    if (end - pointer > std::numeric_limits<size_t>::max()) break;
    *extent = static_cast<size_t>(end - pointer);
    found = true;
    break;
  }
  std::fclose(maps);
  return found;
#endif
}

bool Calibrate(const runnable_jni::Context& context,
               abi::arm64::android_common::FromReflectedMethod named_leaf,
               const Oracle* oracles, size_t count, Calibration* output,
               std::string* error) {
  const auto fail = [&](const char* message) {
    if (error) *error = message;
    return false;
  };
  if (!context.self || !context.env || !context.vm || !named_leaf || !oracles ||
      !output || count < 3 || count > 32) return fail("Invalid owned method calibration request");
  if (context.env->ExceptionCheck()) return fail("Pending JNI exception during method calibration");
  bool has_native = false, has_instance = false, has_static = false;
  std::set<uint32_t> indices;
  for (size_t i = 0; i < count; ++i) {
    const auto& oracle = oracles[i];
    if (!oracle.strong_global_method || oracle.dex_method_index == UINT32_MAX ||
        !indices.insert(oracle.dex_method_index).second ||
        context.env->GetObjectRefType(oracle.strong_global_method) != JNIGlobalRefType)
      return fail("Method calibration requires distinct owned strong globals and DEX indices");
    if (context.env->ExceptionCheck()) return fail("JNI reference validation raised an exception");
    if (oracle.java_modifiers & 0x100u) has_native = true;
    else if (oracle.java_modifiers & 8u) has_static = true;
    else has_instance = true;
  }
  if (!has_native || !has_static || !has_instance)
    return fail("Method calibration requires native, instance and static fixture methods");
  bool matches11 = true, matches12 = true;
  Calibration candidate{};
  candidate.identities.reserve(count);
  candidate.readable_extents.reserve(count);
  std::set<const void*> methods;
  // Stable Java visibility, static, final and native bits. ART's verifier/JIT
  // flags occupy additional bits and are neither identity nor a reflection oracle.
  constexpr uint32_t kModifierMask = 0x11fu;
  const abi::arm64::android_common::RunnableJniScope scope{context.self, context.env, context.vm};
  for (size_t i = 0; i < count; ++i) {
    const auto& oracle = oracles[i];
    void* method = named_leaf(scope, oracle.strong_global_method);
    if (context.env->ExceptionCheck()) return fail("Named reflected-method leaf raised an exception");
    size_t extent = 0;
    if (!method || !methods.insert(method).second ||
        reinterpret_cast<uintptr_t>(method) % alignof(uint32_t) ||
        !ReadableMappingExtent(method, &extent) || extent < sizeof(method_metadata::Method12))
      return fail("Named method has no sufficient live readable mapping");
    const uint32_t flags = method_metadata::ReadAccessFlags(method);
    uint32_t index11 = UINT32_MAX, index12 = UINT32_MAX;
    if (extent >= sizeof(method_metadata::Method11))
      std::memcpy(&index11, static_cast<const unsigned char*>(method) +
          offsetof(method_metadata::Method11, dex_method_index), sizeof(index11));
    std::memcpy(&index12, static_cast<const unsigned char*>(method) +
        offsetof(method_metadata::Method12, dex_method_index), sizeof(index12));
    const bool modifiers_match = (flags & kModifierMask) == (oracle.java_modifiers & kModifierMask);
    matches11 = matches11 && modifiers_match && index11 == oracle.dex_method_index;
    matches12 = matches12 && modifiers_match && index12 == oracle.dex_method_index;
    candidate.identities.push_back({flags, oracle.dex_method_index});
    candidate.readable_extents.push_back(extent);
  }
  if (matches11 == matches12) return fail(matches11
      ? "Ambiguous ArtMethod identity declarations" : "No ArtMethod declaration matches independent oracles");
  candidate.model = matches11 ? Model::kMethod11 : Model::kMethod12Onward;
  *output = std::move(candidate);
  if (error) error->clear();
  return true;
}
}  // namespace artdcevm::method_identity
