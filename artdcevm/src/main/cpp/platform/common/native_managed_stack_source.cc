#include "platform/common/source_handle_roots.h"
#include "platform/common/native_source_stop.h"
#include "platform/common/native_cache_service.h"
namespace artdcevm::source_handle_roots {
bool Service::ReadStoppedManagedFragment(const native_source_stop::Session&stop,void*self,
 const abi::arm64::android16::ManagedStack*fragment,const modern_field_metadata::ReadContext&context,
 ManagedStackSnapshot*out)const noexcept {
 if(!out||!Calibrated()||!current_||current_()!=self||!stop.IsStopped(self)||!fragment||sizeof(*fragment)>UINTPTR_MAX-uintptr_t(fragment)||uintptr_t(fragment)%alignof(abi::arm64::android16::ManagedStack)||!context.owner||!context.extent)return false;
 ManagedStackSnapshot value;if(!managed_stack_source::Read(context,fragment,&value)||current_()!=self||!stop.IsStopped(self))return false;
 *out=value;return true;
}
bool Service::ReadStoppedManagedStack(const native_source_stop::Session&stop,void*self,void*thread,
 const native_cache::Service&threads,const modern_field_metadata::ReadContext&context,ManagedStackSnapshot*out)const noexcept {
 if(!out||!Calibrated()||!current_||current_()!=self||!stop.IsStopped(self)||!thread||!threads.PreparedForCurrentThread()||!context.owner||!context.extent)return false;
 struct Registered {void*target;bool found=false,duplicate=false;};Registered registered{thread};
 const auto visit=[](void*target,void*raw){auto&r=*static_cast<Registered*>(raw);if(target==r.target){r.duplicate|=r.found;r.found=true;}return true;};
 size_t count=0;if(!threads.VisitStoppedWorld(visit,&registered,&count,nullptr)||!count||!registered.found||registered.duplicate||current_()!=self||!stop.IsStopped(self))return false;
 return ReadEnumeratedStoppedManagedStack(stop,self,thread,context,out);
}
bool Service::ReadEnumeratedStoppedManagedStack(const native_source_stop::Session&stop,void*self,void*thread,
 const modern_field_metadata::ReadContext&context,ManagedStackSnapshot*out)const noexcept {
 if(!out||!Calibrated()||!current_||current_()!=self||!stop.IsStopped(self)||!thread||!context.owner||!context.extent)return false;
 constexpr size_t stack=offsetof(abi::arm64::android_common::HandleThreadPrefix,managed_stack);
 if(prefix_>SIZE_MAX-stack||prefix_+stack>UINTPTR_MAX-uintptr_t(thread)||sizeof(abi::arm64::android16::ManagedStack)>UINTPTR_MAX-(uintptr_t(thread)+prefix_+stack))return false;
 size_t extent=0;if(!context.extent(context.owner,thread,&extent)||extent<prefix_+stack+sizeof(abi::arm64::android16::ManagedStack))return false;
 auto*head=reinterpret_cast<const abi::arm64::android16::ManagedStack*>(uintptr_t(thread)+prefix_+stack);
 return ReadStoppedManagedFragment(stop,self,head,context,out);
}
}
