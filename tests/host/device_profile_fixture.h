#pragma once
#include "platform/android13/device_profile.h"
#include <cassert>
#include <cstring>
namespace artdcevm {
inline LoadedModule TestProfileModule(const char* name) {
 const char* names[]={"libart.so","libopenjdkjvmti.so","libdexfile.so","libc++.so","libart-compiler.so"};
 const char* ids[]={"14fc77b24db0b351b919c97009dc86bf","2fe21c45581b9c8b0519b71cbde32938","e6aaffcef8ef63baf438267cda44d400","1f426797e505c9b841f55cc49d32b3f4","578147d9e59c98c6bc3c6faf9d27c9e6"};
 for(size_t i=0;i<5;++i) if(!std::strcmp(name,names[i])) return {std::string("/test/")+name,ids[i]};
 return {};
}
LoadedModule FindLoadedModule(const char* name) { return TestProfileModule(name); }
}
inline artdcevm::android13::DeviceProfile MatchedProfileForTest() {
 artdcevm::android13::DeviceProfile profile; std::string error;
 assert(artdcevm::android13::MatchDeviceProfile(&profile,&error,artdcevm::TestProfileModule));
 return profile;
}
