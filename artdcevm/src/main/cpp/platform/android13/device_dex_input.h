#pragma once
#include "platform/android13/device_profile.h"
#include "api/hook_engine.h"
#include "platform/android13/validation_metadata_abi.h"
#include <cstddef>
#include <string>
namespace artdcevm::android13 {
namespace device_dex_abi {
// Platform unique_ptr is one pointer, nontrivial by-value argument/return.
// The wrapper preserves AAPCS64's indirect argument and x8 return convention.
struct OwnedPointer {
  void* object=nullptr;
  OwnedPointer()=default;
  explicit OwnedPointer(void* p):object(p) {}
  OwnedPointer(OwnedPointer&& p):object(p.object) {p.object=nullptr;}
  OwnedPointer(const OwnedPointer&)=delete;
  ~OwnedPointer();
};
struct PlatformError { alignas(8) unsigned char bytes[24]{}; };
struct Loader { const void* vptr=nullptr; };
inline constexpr char kOpen[]="_ZNK3art13DexFileLoader4OpenEPKhmRKNSt3__112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEEjPKNS_10OatDexFileEbbPS9_NS3_10unique_ptrINS_16DexFileContainerENS3_14default_deleteISH_EEEE";
inline constexpr char kDestroyString[]="_ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEED2Ev";
static_assert(sizeof(OwnedPointer)==8 && sizeof(PlatformError)==24 && sizeof(Loader)==8);
}
// Owns a device-created DexFile. The original virtual deleting destructor and
// platform string destructor retain all device allocation/deallocation ownership.
class DeviceDexInput {
 public:
  DeviceDexInput(const unsigned char* bytes,size_t size);
  bool Valid() const { return dex_.object!=nullptr; }
  validation_abi::DexMappingPrefix Mapping() const;
 private:
  device_dex_abi::OwnedPointer dex_;
};
bool ConfigureDeviceDexInputs(HookEngine&,const DeviceProfile& profile,std::string* error);
}
