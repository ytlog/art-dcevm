#include "platform/android13/device_dex_input.h"
#include "platform/android13/platform_string_abi.h"
#include "dex/dex_file_loader.h"
#include "dex/dex_file.h"
#include "class_root-inl.h"
#include "mirror/class-inl.h"
#include "scoped_thread_state_change-inl.h"
#include <cstring>
#include <memory>
#include <new>
#include <type_traits>
namespace fixed=artdcevm::android13::device_dex_abi;
using Mapping=artdcevm::android13::validation_abi::DexMappingPrefix;
static_assert(sizeof(std::unique_ptr<const art::DexFile>)==sizeof(fixed::OwnedPointer));
static_assert(alignof(std::unique_ptr<const art::DexFile>)==alignof(fixed::OwnedPointer));
static_assert(!std::is_trivially_copyable_v<std::unique_ptr<const art::DexFile>>);
static_assert(!std::is_trivially_copyable_v<fixed::OwnedPointer>);
static_assert(sizeof(std::unique_ptr<art::DexFileContainer>)==sizeof(fixed::OwnedPointer));
static_assert(sizeof(art::DexFileLoader)==sizeof(fixed::Loader));
static_assert(alignof(art::DexFileLoader)==alignof(fixed::Loader));
static_assert(sizeof(std::string)==sizeof(fixed::PlatformError));
static_assert(alignof(std::string)==alignof(fixed::PlatformError));
#ifdef _LIBCPP_ABI_ALTERNATE_STRING_LAYOUT
#error DEX input requires Android 13 platform string ABI v1
#endif
#ifdef _LIBCPP_ABI_ENABLE_UNIQUE_PTR_TRIVIAL_ABI
#error DEX input requires nontrivial platform unique_ptr calling convention
#endif
extern "C" bool ArtDcevmSourceProbeDeviceDexInput(
    bool(*read)(const unsigned char*,size_t,Mapping*),void(*destroy)(void*)) {
  if(!read || !destroy) return false;
  fixed::PlatformError storage;
  std::string empty;
  if(std::memcmp(storage.bytes,&empty,sizeof(empty))!=0) return false;
  for(size_t length:{0u,3u,128u}) {
    auto* text=new(storage.bytes) std::string(length,'x');
    if(text->size()!=length) return false;
    destroy(storage.bytes); // exact platform destructor, no second destruction
  }
  // Small malformed input exercises the real indirect unique_ptr return and
  // allocated error path, then proves all platform ownership was released.
  const unsigned char bad[8]{};Mapping mapping{};
  if(read(bad,sizeof(bad),&mapping)) return false;
  // Exercise a non-null indirect return and the device deleting destructor.
  // Boot DEX storage remains alive after the temporary owned view is destroyed.
  art::ScopedObjectAccess access(art::Thread::Current());
  const art::DexFile& dex=art::GetClassRoot<art::mirror::Object>()->GetDexFile();
  if(!read(dex.Begin(),dex.Size(),&mapping)) return false;
  if(mapping.begin!=dex.Begin() || mapping.size!=dex.Size() ||
     mapping.data_begin!=dex.DataBegin() || mapping.data_size!=dex.DataSize()) return false;
  return true;
}
