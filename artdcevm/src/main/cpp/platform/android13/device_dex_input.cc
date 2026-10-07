#include "platform/android13/device_dex_input.h"
#include "platform/android13/platform_string_abi.h"
#include <cstring>
#include <dlfcn.h>
namespace artdcevm::android13 {
namespace {
using Open=device_dex_abi::OwnedPointer(*)(const void*,const unsigned char*,size_t,
    const BorrowedPlatformString*,uint32_t,const void*,bool,bool,device_dex_abi::PlatformError*,device_dex_abi::OwnedPointer);
Open open_dex=nullptr;
void(*destroy_string)(void*)=nullptr;
// DexFile declares its virtual destructor first. Virtual delete dispatches to
// the device's deleting destructor, including its device operator delete.
class DeviceDexDeclaration {public:virtual ~DeviceDexDeclaration()=default;};
bool ProbeRead(const unsigned char* bytes,size_t size,validation_abi::DexMappingPrefix* output) {
  DeviceDexInput input(bytes,size);
  if(!input.Valid()) return false;
  *output=input.Mapping();return true;
}
}
namespace device_dex_abi {
OwnedPointer::~OwnedPointer() { if(object) delete reinterpret_cast<DeviceDexDeclaration*>(object); }
}
DeviceDexInput::DeviceDexInput(const unsigned char* bytes,size_t size) {
  device_dex_abi::Loader loader;
  device_dex_abi::PlatformError error; // canonical platform default empty representation
  const BorrowedPlatformString location("artdcevm-receiver-preflight");
  auto result=open_dex(&loader,bytes,size,&location,0,nullptr,true,true,&error,{});
  dex_.object=result.object;result.object=nullptr;
  destroy_string(&error); // empty, short and allocated long errors all remain platform-owned
}
validation_abi::DexMappingPrefix DeviceDexInput::Mapping() const {
  validation_abi::DexMappingPrefix result{};
  if(dex_.object) std::memcpy(&result,dex_.object,sizeof(result));
  return result;
}
bool ConfigureDeviceDexInputs(HookEngine& hooks,void* implementation,std::string* error) {
  auto open=reinterpret_cast<Open>(hooks.FindSymbol("libdexfile.so",device_dex_abi::kOpen));
  auto destroy=reinterpret_cast<void(*)(void*)>(hooks.FindSymbol("libc++.so",device_dex_abi::kDestroyString));
  using Probe=bool(*)(bool(*)(const unsigned char*,size_t,validation_abi::DexMappingPrefix*),void(*)(void*));
  auto probe=reinterpret_cast<Probe>(dlsym(implementation,"ArtDcevmSourceProbeDeviceDexInput"));
  if(!open || !destroy || !probe) { *error="Missing named device DEX opener, platform error destructor or calibration";return false; }
  open_dex=open;destroy_string=destroy;
  if(!probe(ProbeRead,destroy_string)) { *error="Device DEX input ABI did not match platform ownership declarations";return false; }
  return true;
}
}
