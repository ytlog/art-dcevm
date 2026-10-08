#include "platform/android13/device_profile.h"
#include <cstring>
#include <utility>
namespace artdcevm::android13 {
namespace {
constexpr const char* kBase[]={"libart.so","libopenjdkjvmti.so","libdexfile.so","libc++.so"};
constexpr char kDeclarationId[]="android13-source-family-arm64-release";
bool Fail(std::string* error,std::string message) {if(error)*error=std::move(message);return false;}
bool Check(const char* expected,const LoadedModule& actual,std::string* error) {
  const auto slash=actual.path.find_last_of('/');
  const auto basename=actual.path.substr(slash==std::string::npos?0:slash+1);
  if(basename!=expected)return Fail(error,std::string("Missing loaded runtime module: ")+expected);
  return true;
}
}
const char* DeviceProfile::DeclarationId() const {return matched_?kDeclarationId:nullptr;}
bool MatchDeviceProfile(DeviceProfile* output,std::string* error,ModuleFinder finder) {
  if(output)*output=DeviceProfile{};
  if(error)error->clear();
  if(!output||!finder)return Fail(error,"Missing runtime candidate output or module finder");
#if defined(__ANDROID__) && !defined(__aarch64__)
  return Fail(error,"Runtime source candidate requires Android arm64");
#endif
  DeviceProfile candidate;
  for(size_t i=0;i<candidate.modules_.size();++i) {
    candidate.modules_[i]=finder(kBase[i]);
    if(!Check(kBase[i],candidate.modules_[i],error))return false;
  }
  candidate.matched_=true;
  *output=std::move(candidate);
  return true;
}
bool MatchCompilerDeviceProfile(const DeviceProfile& base,std::string* error,ModuleFinder finder) {
  if(error)error->clear();
  if(!base.Matched())return Fail(error,"Compiler requires a discovered runtime candidate");
  if(!finder)return Fail(error,"Missing compiler module finder");
  return Check("libart-compiler.so",finder("libart-compiler.so"),error);
}
}
