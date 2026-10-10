#include "platform/common/contextual_field_id.h"
#include "platform/common/modern_retirement_cleanup.h"
#include "platform/common/class_loader_cleanup.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/abi/arm64/android15/linear_alloc.h"
#include "platform/abi/arm64/common/suspend_all.h"
#include <atomic>
#include <cstdlib>
namespace artdcevm::retirement {
namespace {
abi::arm64::android15::ArenaContains arena_contains=nullptr;
RetiredFieldRegistry* fields=nullptr;RetiredMethodRegistry* methods=nullptr;
class_loader_cleanup::Cleanup original=nullptr;
class_loader_cleanup::Declaration declaration;
std::atomic<size_t> calls{0},field_count{0},method_count{0};
bool Contains(const void* allocator,void* member) {
 return arena_contains(static_cast<const unsigned char*>(allocator)+abi::arm64::android15::kArenaOffset,member);
}
void Cleanup(void* linker,void* thread,const void* data,bool cleanup_cha) {
 ++calls;
 try {
  if(!class_loader_cleanup::Dispatch(declaration,original,linker,thread,data,cleanup_cha,
      [](void* allocator){field_count+=fields->Prune(allocator,Contains);contextual_field_id::NativeNotifyPrune(*fields);method_count+=methods->Prune(allocator,Contains);}))std::abort();
 } catch(...) {std::abort();}
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
bool InstallRetirementCleanup(HookEngine& hooks,const Membership& proof,RetiredFieldRegistry& f,
    RetiredMethodRegistry& m,std::string* error) {
 if(fields)return fields==&f&&methods==&m?true:Fail(error,"Loader cleanup already has different durable registry owners");
 if(!proof.matched||proof.owned<2||!proof.foreign)return Fail(error,"Loader cleanup needs actual owned/foreign allocator membership calibration");
 auto contains=reinterpret_cast<abi::arm64::android15::ArenaContains>(hooks.FindSymbol("libartbase.so",abi::arm64::android15::kArenaContains));
 // These are distinct fixed native declarations with the shared 24-byte
 // record prefix. Dispatch retains each original destruction schedule.
 auto selected=class_loader_cleanup::Declaration::kPrepareForBatchDelete;
 void* target=hooks.FindSymbol("libart.so",class_loader_cleanup::Symbol(selected));
 if(!target){selected=class_loader_cleanup::Declaration::kDeleteAndDestroy;target=hooks.FindSymbol("libart.so",class_loader_cleanup::Symbol(selected));}
 using namespace abi::arm64::android_common;
 auto construct=reinterpret_cast<SuspendConstruct>(hooks.FindSymbol("libart.so",kSuspendConstruct));
 auto destroy=reinterpret_cast<SuspendDestroy>(hooks.FindSymbol("libart.so",kSuspendDestroy));
 if(!target||!contains||!construct||!destroy)return Fail(error,"Missing named native class-loader cleanup/stop declarations");
 SuspendAll stopped{};construct(&stopped,"artdcevm retirement loader cleanup installation",false);
 struct Stop {SuspendAll* value;SuspendDestroy destroy;~Stop(){destroy(value);}} stop{&stopped,destroy};
 fields=&f;methods=&m;arena_contains=contains;declaration=selected;
 if(!hooks.Install(target,reinterpret_cast<void*>(Cleanup),reinterpret_cast<void**>(&original),error)) {
  fields=nullptr;methods=nullptr;arena_contains=nullptr;return false;
 }
 return true;
}
size_t CleanupCalls(){return calls.load();}size_t CleanupFields(){return field_count.load();}size_t CleanupMethods(){return method_count.load();}
}
