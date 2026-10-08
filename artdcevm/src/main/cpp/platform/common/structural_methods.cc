#include "platform/common/structural_methods.h"
#include <cstdlib>
namespace artdcevm::structural_methods {
namespace {
constexpr uint32_t kStatic=0x8,kNative=0x100,kAbstract=0x400;
constexpr uint32_t kObsolete=0x40000,kDontCompile=0x2000000;
}
void* FindReplacement(const Services& services,void* old_method,void* new_class) {
  const auto flags=services.Flags(old_method);
  const auto count=services.Count(new_class);
  for(size_t i=0;i<count;++i) {
    void* candidate=services.At(new_class,i);
    if(((services.Flags(candidate)^flags)&kStatic)==0&&services.SameIdentity(candidate,old_method))
      return candidate;
  }
  return nullptr;
}
void Update(const Services& services,void* old_class,void* new_class) {
  const auto count=services.Count(old_class);
  (void)services.Count(new_class); // Validate the destination table before any mutation.
  for(size_t i=0;i<count;++i) {
    void* method=services.At(old_class,i);
    // Copied interface defaults retain their interface declaring class.
    if(services.DeclaringClass(method)!=old_class)continue;
    const auto flags=services.Flags(method);
    // Deleted natives deliberately retain their old declaring class and entry.
    // They cannot be looked up on a replacement class that no longer has them.
    if((flags&kNative)&&!services.IsRetired(method)) {
      void* replacement=FindReplacement(services,method,new_class);
      if(!replacement||replacement==method)std::abort();
      if(services.Flags(replacement)&kNative)services.TransferNativeBinding(method,replacement);
    }
    services.AddFlags(method,kObsolete);
    services.SetObsoleteEntry(method);
    if(!(services.Flags(method)&kAbstract))services.AddFlags(method,kDontCompile);
  }
}
}
