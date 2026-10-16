#pragma once
#include "platform/common/art_dex_view.h"
#include "platform/common/class_layout_table.h"

namespace artdcevm::class_layout {
template <typename Class, typename Method>
bool MethodIdentity(Method* method, evolution::Member* identity, std::string* why) {
  auto* declaring = Heap<Class>(method->declaring_class);
  const auto* dex = declaring ? DexOf(declaring->dex_cache) : nullptr;
  if (!dex || !dex->begin) return Fail(why, "mapped method has no declaring DEX");
  return evolution::ReadMethodIdentity(DexViewOf(*dex), method->dex_method_index, identity, why);
}

template <typename Class> std::string ClassDescriptor(const Class& klass) {
  const auto* dex = DexOf(klass.dex_cache);
  std::string descriptor, error;
  if (!dex || !dex->begin || klass.dex_type_index < 0 ||
      !evolution::ReadTypeDescriptor(DexViewOf(*dex), static_cast<uint32_t>(klass.dex_type_index),
                                    &descriptor, &error)) return "<unnamed class>";
  return descriptor;
}
}  // namespace artdcevm::class_layout
