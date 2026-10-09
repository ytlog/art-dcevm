#pragma once
#include "platform/common/modern_field_metadata.h"
#include <cstdint>
namespace artdcevm::native_frame_async_recovery {
// Exact original declarations: legacy takes PC; newer source adds ArtMethod.
// Presence binds a reader only. Actual stopped calls, never a symbol/SDK flag,
// determine eligibility. Runtime receiver comes from its named ELF singleton.
class Service {
 public:
 template<class Finder>static Service Bind(Finder finder){
  Service s;s.runtime_=reinterpret_cast<void*const*>(finder(kRuntime));
  s.method_=reinterpret_cast<bool(*)(const void*,void*,uintptr_t)>(finder(kMethod));
  s.pc_=reinterpret_cast<bool(*)(const void*,uintptr_t)>(finder(kPc));return s;
 }
 bool Query(void*method,uintptr_t pc,const modern_field_metadata::ReadContext&context)const noexcept {
  if(!runtime_||!method||!pc||(!method_&&!pc_)||!context.owner||!context.extent)return false;
  size_t extent=0;if(!context.extent(context.owner,runtime_,&extent)||extent<sizeof(void*))return false;
  const auto runtime=*runtime_;if(!runtime)return false;
  const bool accepted=method_?method_(runtime,method,pc):pc_(runtime,pc);
  extent=0;return accepted&&context.extent(context.owner,runtime_,&extent)&&extent>=sizeof(void*)&&*runtime_==runtime;
 }
 inline static constexpr char kRuntime[]="_ZN3art7Runtime9instance_E";
 inline static constexpr char kMethod[]="_ZNK3art7Runtime21IsAsyncDeoptimizeableEPNS_9ArtMethodEm";
 inline static constexpr char kPc[]="_ZNK3art7Runtime21IsAsyncDeoptimizeableEm";
 private:
 void*const* runtime_=nullptr;bool(*method_)(const void*,void*,uintptr_t)=nullptr;bool(*pc_)(const void*,uintptr_t)=nullptr;
};
}
