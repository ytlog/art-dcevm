#include "platform/android13/device_profile.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace artdcevm;
using namespace artdcevm::android13;
namespace {
const char* names[]{"libart.so", "libopenjdkjvmti.so", "libdexfile.so", "libc++.so", "libart-compiler.so"};
const char* ids[]{"14fc77b24db0b351b919c97009dc86bf", "2fe21c45581b9c8b0519b71cbde32938",
    "e6aaffcef8ef63baf438267cda44d400", "1f426797e505c9b841f55cc49d32b3f4",
    "578147d9e59c98c6bc3c6faf9d27c9e6"};
LoadedModule modules[5];
std::vector<std::string> calls;
LoadedModule Find(const char* name) {
  calls.emplace_back(name);
  for (size_t i=0;i<5;++i) if (calls.back()==names[i]) return modules[i];
  assert(false && "unexpected module lookup");
  return {};
}
void Reset() {
  calls.clear();
  for (size_t i=0;i<5;++i) modules[i]={std::string("/apex/com.android.art/lib64/")+names[i], ids[i]};
}
void Order(size_t count, bool compiler=false) {
  assert(calls.size()==count);
  for (size_t i=0;i<count;++i) assert(calls[i]==names[compiler?4:i]);
}
}
// Link the production default finder reference without any Android/private ABI.
namespace artdcevm { LoadedModule FindLoadedModule(const char* name) { return Find(name); } }

int main() {
  DeviceProfile profile;
  std::string error="stale";
  assert(!profile.Matched() && !profile.DeclarationId());
  Reset();assert(MatchDeviceProfile(&profile,&error));Order(4);
  assert(profile.Matched() && error.empty());
  assert(std::string(profile.DeclarationId())=="android13-source-family-arm64-release");
  for(size_t i=0;i<4;++i)assert(profile.Modules()[i].path==modules[i].path&&profile.Modules()[i].build_id==ids[i]);
  // A compiler mismatch cannot disable the independently matched base profile.
  calls.clear();assert(MatchCompilerDeviceProfile(profile,&error));Order(1,true);
  // Artifact identities never influence candidate discovery.
  for(size_t i=0;i<5;++i) {
    Reset();modules[i].build_id.clear();
    assert(MatchDeviceProfile(&profile,&error,Find));
    assert(MatchCompilerDeviceProfile(profile,&error,Find));
    Reset();modules[i].build_id="unknown-or-changed-mainline";
    assert(MatchDeviceProfile(&profile,&error,Find));
    assert(MatchCompilerDeviceProfile(profile,&error,Find));
  }
  for(size_t failure=0;failure<4;++failure) {
    for(unsigned kind=0;kind<2;++kind) {
      Reset();assert(MatchDeviceProfile(&profile,&error,Find));calls.clear();
      modules[failure].path=kind==0?"":"/other/wrong.so";
      assert(!MatchDeviceProfile(&profile,&error,Find));Order(failure+1);
      assert(!profile.Matched() && !profile.DeclarationId());
      for(const auto& module:profile.Modules())assert(module.path.empty());
      calls.clear();assert(!MatchCompilerDeviceProfile(profile,&error,Find));assert(calls.empty());
    }
  }
  Reset();assert(MatchDeviceProfile(&profile,&error,Find));
  modules[4].path.clear();calls.clear();
  assert(!MatchCompilerDeviceProfile(profile,&error,Find));Order(1,true);
  assert(profile.Matched());
  Reset();assert(!MatchDeviceProfile(nullptr,&error,Find));assert(calls.empty());
  assert(!MatchDeviceProfile(&profile,&error,nullptr)&&!profile.Matched());assert(calls.empty());
  assert(!MatchCompilerDeviceProfile(profile,nullptr,nullptr));
  Reset();assert(MatchDeviceProfile(&profile,nullptr,Find));Order(4);
  puts("runtime candidates: changed/missing artifact IDs accepted, module basenames fail closed, reset and compiler discovery passed");
}
