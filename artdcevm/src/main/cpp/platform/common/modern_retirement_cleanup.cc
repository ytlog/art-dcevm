#include "platform/common/contextual_field_id.h"
#include "platform/common/modern_retirement_cleanup.h"
#include "platform/common/native_source_stop.h"
#include "platform/common/native_interpretation_registry.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/abi/arm64/android15/linear_alloc.h"
#include "platform/abi/common/suspend_all.h"
#include <atomic>
#include <cstdlib>
namespace artdcevm::retirement {
namespace {
abi::arm64::android15::ArenaContains arena_contains=nullptr;
RetiredFieldRegistry* fields=nullptr;RetiredMethodRegistry* methods=nullptr;
native_interpretation_registry::Registry* interpreted=nullptr;
using DestroyArena=void(*)(void*);
DestroyArena original=nullptr;
constexpr char kDestroyArena[]="_ZN3art14ArenaAllocatorD1Ev";
constexpr char kDestroyArenaAlias[]="_ZN3art14ArenaAllocatorD2Ev";
std::atomic<size_t> calls{0},field_count{0},method_count{0};
std::atomic<size_t> interpreted_count{0};
std::atomic<void*>cleanup_token{nullptr};
bool Contains(const void* arena,void* member) {
 return arena_contains(arena,member);
}
void Cleanup(void* arena) {
 ++calls;
 try {
  if(!arena||!original||!arena_contains||!fields||!methods)std::abort();
  // LinearAlloc owns this ArenaAllocator as a native member. The actual
  // destructor supplies its exact address; Contains needs no prefix arithmetic.
  // Its original body frees the arena chain after the last possible lookup.
  field_count+=fields->Prune(arena,Contains);contextual_field_id::NativeNotifyPrune(*fields);
  method_count+=methods->Prune(arena,Contains);
  if(interpreted)interpreted_count+=interpreted->Prune(arena,Contains);
 }catch(...){std::abort();}
 original(arena);
}
bool Fail(std::string* error,const char* value){if(error)*error=value;return false;}
}
bool CalibrateAllocatorMembership(HookEngine& hooks,void* linker,
    const exclusive_owned_roots::Context& context,const std::vector<void*>& owned,
    const std::vector<void*>& foreign,Membership* output,std::string* error) {
 if(!output||!linker||!context.self||context.count<2||!context.roots[0]||owned.size()<2||foreign.empty())
  return Fail(error,"Allocator membership requires real exclusive rooted loader/class and complete native member oracles");
 using namespace abi::arm64::android15;
 auto get=reinterpret_cast<GetAllocator>(hooks.FindSymbol("libart.so",kGetAllocator));
 auto contains=reinterpret_cast<ArenaContains>(hooks.FindSymbol("libartbase.so",kArenaContains));
 if(!get||!contains)return Fail(error,"Missing named loader allocator/Arena Contains declarations");
 void* allocator=get(linker,{reinterpret_cast<uintptr_t>(context.roots[0])});
 size_t extent=0;
 if(!allocator||!method_identity::ReadableMappingExtent(allocator,&extent)||extent<kArenaOffset+5*sizeof(void*))
  return Fail(error,"Owned loader allocator source prefix is unreadable");
 const void* arena=static_cast<unsigned char*>(allocator)+kArenaOffset;
 for(void* member:owned)if(!member||!contains(arena,member))return Fail(error,"Owned complete native member is outside source arena");
 for(void* member:foreign)if(!member||contains(arena,member))return Fail(error,"Foreign native member incorrectly matches source arena");
 int stack=0;if(contains(arena,&stack))return Fail(error,"Native stack incorrectly matches source arena");
 *output={allocator,owned.size(),foreign.size(),true};return true;
}
bool CleanupBinding::Bind(HookEngine&hooks,std::string*error){
 auto target=hooks.FindSymbol("libartbase.so",kDestroyArena);
 auto alias=hooks.FindSymbol("libartbase.so",kDestroyArenaAlias);
 auto contains=reinterpret_cast<abi::arm64::android15::ArenaContains>(
   hooks.FindSymbol("libartbase.so",abi::arm64::android15::kArenaContains));
 if(!target||alias!=target||!contains)
  return Fail(error,"Missing named allocation-arena destructor/alias/Contains declarations");
 if(target_&&(target_!=target||contains_!=contains))
  return Fail(error,"Allocation arena cleanup binding changed");
 target_=target;contains_=contains;return true;
}
bool CleanupBinding::Install(HookEngine&hooks,const Membership&proof,RetiredFieldRegistry&f,
 RetiredMethodRegistry&m,native_interpretation_registry::Registry*interpretation,std::string*error)const{
 if(fields)return cleanup_token.load(std::memory_order_acquire)&&fields==&f&&methods==&m&&interpreted==interpretation?
  true:Fail(error,"Arena cleanup already has different durable registry owners");
 if(!target_||!contains_||!proof.matched||!proof.allocator||proof.owned<2||!proof.foreign)
  return Fail(error,"Arena cleanup needs actual owned/foreign allocator membership calibration");
 fields=&f;methods=&m;interpreted=interpretation;arena_contains=contains_;
 struct Publication {
  ~Publication(){if(!cleanup_token.load(std::memory_order_acquire)){
   if(original)std::abort();
   fields=nullptr;methods=nullptr;interpreted=nullptr;arena_contains=nullptr;
  }}
 }publication;
 void*token=hooks.Install(target_,reinterpret_cast<void*>(Cleanup),reinterpret_cast<void**>(&original),error);
 if(!token)return false;
 if(!original||reinterpret_cast<void*>(original)==target_)std::abort();
 cleanup_token.store(token,std::memory_order_release);return true;
}
bool CleanupBinding::InstallStopped(HookEngine&hooks,const native_source_stop::Session&stop,void*self,
 const Membership&proof,RetiredFieldRegistry&f,RetiredMethodRegistry&m,
 native_interpretation_registry::Registry*interpretation,std::string*error)const{
 if(!stop.IsStopped(self))return Fail(error,"Arena cleanup installation requires the actual source stop");
 const bool installed=Install(hooks,proof,f,m,interpretation,error);
 if(!stop.IsStopped(self))std::abort();return installed;
}
bool InstallRetirementCleanup(HookEngine&hooks,const Membership&proof,RetiredFieldRegistry&f,
 RetiredMethodRegistry&m,std::string*error,native_interpretation_registry::Registry*interpretation){
 CleanupBinding binding;if(!binding.Bind(hooks,error))return false;
 using namespace abi::common;
 auto construct=reinterpret_cast<SuspendConstruct>(hooks.FindSymbol("libart.so",kSuspendConstruct));
 auto destroy=reinterpret_cast<SuspendDestroy>(hooks.FindSymbol("libart.so",kSuspendDestroy));
 if(!construct||!destroy)return Fail(error,"Missing named native cleanup installation stop declarations");
 SuspendAll stopped{};construct(&stopped,"artdcevm retirement arena cleanup installation",false);
 struct Stop {SuspendAll*value;SuspendDestroy destroy;~Stop(){destroy(value);}}stop{&stopped,destroy};
 return binding.Install(hooks,proof,f,m,interpretation,error);
}
size_t CleanupCalls(){return calls.load();}size_t CleanupFields(){return field_count.load();}size_t CleanupMethods(){return method_count.load();}
size_t CleanupInterpretedMethods(){return interpreted_count.load();}
bool HasInterpretationCleanup(const native_interpretation_registry::Registry&owner)noexcept{
 return cleanup_token.load(std::memory_order_acquire)&&interpreted==&owner&&fields&&methods&&arena_contains&&original;
}
}
