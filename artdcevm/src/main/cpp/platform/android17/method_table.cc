#include "platform/android17/method_table.h"
#include <set>
namespace artdcevm::android17 {
bool CalibrateMethodTable(const runnable_jni::Context& context,
    abi::common::FromReflectedMethod leaf,
    const method_identity::Calibration& identity_model, const void* klass,
    const evolution::MappedDexMemberCounts& dex,
    const method_identity::Oracle* methods, size_t count,
    method_array::View* output, std::string* error) {
  const auto fail = [&](const char* message) {
    if (error) *error = message;
    return false;
  };
  if (!context.self || !context.env || !context.vm || !leaf || !klass || !output ||
      identity_model.model != method_identity::Model::kMethod12Onward ||
      identity_model.identities.size() < 3 ||
      identity_model.readable_extents.size() != identity_model.identities.size() ||
      count != uint64_t{dex.direct_methods} + dex.virtual_methods ||
      (count && !methods) || context.env->ExceptionCheck())
    return fail("Invalid Android17 owned method table request");
  size_t extent = 0;
  if (!method_identity::ReadableMappingExtent(klass, &extent) ||
      extent < sizeof(class_metadata::Class17) || uintptr_t(klass) % 4)
    return fail("Android17 class prefix is not readable");
  uint64_t pointer;
  std::memcpy(&pointer, static_cast<const unsigned char*>(klass) +
      offsetof(class_metadata::Class17, methods), sizeof(pointer));
  void* array = reinterpret_cast<void*>(uintptr_t(pointer));
  size_t array_extent = 0;
  if (array && !method_identity::ReadableMappingExtent(array, &array_extent))
    return fail("Android17 method array is not readable");
  method_array::View table;
  // Derive boundaries from independent class_data, never the obsolete 16 tail.
  const method_array::Boundaries boundaries{dex.direct_methods,
      static_cast<uint32_t>(count), method_array::Order::ByFlags};
  if (count > UINT32_MAX || !method_array::Read(method_metadata::kAndroid12Onward, array, array_extent, boundaries, &table))
    return fail("Android17 method array exceeds its mapped extent");
  const abi::common::RunnableJniScope scope{context.self,context.env,context.vm};
  std::set<uint32_t> physical, indices;
  uint32_t direct = 0;
  constexpr uint32_t kModifiers = 0x11fu;
  for (size_t i=0; i<count; ++i) {
    const auto& oracle = methods[i];
    if (!oracle.strong_global_method || oracle.dex_method_index == UINT32_MAX ||
        !indices.insert(oracle.dex_method_index).second ||
        context.env->GetObjectRefType(oracle.strong_global_method) != JNIGlobalRefType ||
        context.env->ExceptionCheck()) return fail("Android17 method oracle is not a distinct owned global");
    void* method = leaf(scope, oracle.strong_global_method);
    if (context.env->ExceptionCheck() || !method)
      return fail("Android17 named reflected-method leaf failed");
    const auto begin = uintptr_t(array) + 8, address = uintptr_t(method);
    if (address < begin || (address-begin)%table.stride ||
        (address-begin)/table.stride >= count)
      return fail("Owned reflected method is outside declared Android17 table");
    const auto index = static_cast<uint32_t>((address-begin)/table.stride);
    method_metadata::Identity identity;
    if (!physical.insert(index).second ||
        !method_metadata::ReadIdentity(method_metadata::kAndroid12Onward,method,table.stride,&identity) ||
        identity.dex_method_index != oracle.dex_method_index ||
        (identity.access_flags & kModifiers) != (oracle.java_modifiers & kModifiers))
      return fail("Android17 method identity does not match independent DEX/reflection");
    direct += table.IsDirect(index);
  }
  if (direct != dex.direct_methods)
    return fail("Android17 source direct flags do not match DEX class_data");
  *output = table;
  if (error) error->clear();
  return true;
}
}  // namespace artdcevm::android17
