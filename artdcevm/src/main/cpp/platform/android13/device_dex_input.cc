#include "platform/android13/device_dex_input.h"
#include "platform/android13/platform_string_abi.h"
#include <cstring>
namespace artdcevm::android13 {
namespace {
using Open=device_dex_abi::OwnedPointer(*)(const void*,const unsigned char*,size_t,
    const BorrowedPlatformString*,uint32_t,const void*,bool,bool,device_dex_abi::PlatformError*,device_dex_abi::OwnedPointer);
Open open_dex=nullptr;
void(*destroy_string)(void*)=nullptr;
// DexFile declares its virtual destructor first. Virtual delete dispatches to
// the device's deleting destructor, including its device operator delete.
class DeviceDexDeclaration {public:virtual ~DeviceDexDeclaration()=default;};

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
bool ConfigureDeviceDexInputs(HookEngine& hooks,const DeviceProfile& profile,std::string* error) {
  if(!profile.Matched()) {if(error)*error="Binding requires a matched device profile";return false;}
  auto open=reinterpret_cast<Open>(hooks.FindSymbol("libdexfile.so",device_dex_abi::kOpen));
  auto destroy=reinterpret_cast<void(*)(void*)>(hooks.FindSymbol("libc++.so",device_dex_abi::kDestroyString));
  if(!open || !destroy) { *error="Missing named device DEX opener or platform error destructor";return false; }
  open_dex=open;destroy_string=destroy;return true;
}
}
