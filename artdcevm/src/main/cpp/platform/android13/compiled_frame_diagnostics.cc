#include "platform/android13/compiled_frame_diagnostics.h"
#include "platform/android13/compiled_frame_diagnostics_abi.h"
#include "platform/android13/async_deopt_hooks.h"
#include "platform/android13/art_method_abi.h"
#include "platform/android13/member_decode_abi.h"
#include "platform/android13/native_object_access.h"
#include "platform/android13/handle_root_scope.h"
#include "platform/android13/runtime_access.h"
#include "platform/android13/runtime_access_abi.h"
#include "platform/android13/stack_access.h"
#include "platform/android13/thread_suspend_scope.h"
#include "platform/android13/suspend_deopt_hooks.h"
#include <cstdlib>
#include <cstring>
namespace artdcevm::android13 {
namespace {
using Reason=compiled_frame_abi::SuspendReason;
void* (*suspend)(void*,jobject,Reason,bool*);
bool (*resume)(void*,void*,Reason);
bool (*contains)(const void*,uintptr_t);
bool (*cache_contains)(const void*,const void*);
const void* (*oat_code)(void*,size_t);
template<class T>T Read(const void* p,size_t offset){T value;std::memcpy(&value,static_cast<const unsigned char*>(p)+offset,sizeof(value));return value;}
void Require(bool value){if(!value)std::abort();}
void* List(){return Read<void*>(RuntimeInstance(),offsetof(runtime_access_abi::RuntimePrefix,thread_list));}
void* Method(NativeObjectAccess& access,jobject executable){return reinterpret_cast<void*>(Read<uint64_t>(access.Decode(executable),offsetof(member_decode_abi::Executable,art_method)));}
const void* Header(const void* code){return code?static_cast<const unsigned char*>(code)-stack_abi::kCodeOffset:nullptr;}
enum class Kind {kAot,kJit,kSuspend};
struct FrameVisitor final:stack_abi::Visitor {
 FrameVisitor(void* thread,Kind kind,void* caller,void* callee,const void* code):Visitor(thread,nullptr,kind==Kind::kJit?stack_abi::WalkKind::kIncludeInlined:stack_abi::WalkKind::kSkipInlined),operation(kind),caller(caller),callee(callee),code(code){}
 bool VisitFrame()override{
  void* method=Method();
  if(operation==Kind::kSuspend){if(method==caller&&quick_frame&&header==code){state=1;return false;}return true;}
  if(method!=caller&&(operation!=Kind::kJit||method!=callee))return true;
  if(operation==Kind::kAot){state|=1;if(quick_frame)state|=2;if(code&&pc&&contains(code,pc))state|=4;return false;}
  bool in_code=code&&quick_frame&&pc&&contains(code,pc);
  if(method==caller)state|=1|(in_code?2:0);
  if(method==callee)state|=4|(in_code?8:0);
  return true;
 }
 Kind operation;void* caller;void* callee;const void* code;int state=0;
};
int Scan(JNIEnv* env,jobject peer,jobject executable,jobject callee_executable,Kind operation){
 void* self=CurrentHierarchyThread();NativeSuspendScope native(self);Require(native.Supported());
 void* list=List();bool timed_out=false;void* target=suspend(list,peer,Reason::kInternal,&timed_out);
 if(!target)return timed_out?-2:-1;
 int state=0;
 {
  NativeObjectAccess access(env);Require(access.Supported());
  void* caller=Method(access,executable);void* callee=operation==Kind::kJit?Method(access,callee_executable):nullptr;
  const void* code;
  if(operation==Kind::kJit){const void* entry=reinterpret_cast<void*>(Read<uint64_t>(caller,offsetof(art_method_abi::ArtMethod,entry_point)));void* cache=RuntimeJitCodeCache();code=cache&&cache_contains(cache,entry)?Header(entry):nullptr;}
  else code=Header(oat_code(caller,8));
  FrameVisitor visitor(target,operation,caller,callee,code);visitor.Walk();state=visitor.state;
  if(operation==Kind::kSuspend&&state){SetAsyncDeoptProbe(true);state=MarkEvolutionSuspendDeopt(target)?1:0;}
 }
 bool resumed=resume(list,target,Reason::kInternal);
 return resumed?state:-3;
}
}
int DiagnosticAotFrameState(JNIEnv* env,jobject peer,jobject executable){return Scan(env,peer,executable,nullptr,Kind::kAot);}
int DiagnosticJitActiveFrameState(JNIEnv* env,jobject peer,jobject caller,jobject callee){return Scan(env,peer,caller,callee,Kind::kJit);}
int DiagnosticAotSuspendDeoptProbe(JNIEnv* env,jobject peer,jobject executable){return Scan(env,peer,executable,nullptr,Kind::kSuspend);}
uint64_t DiagnosticAotSuspendDeoptHits(bool reset){if(reset){ResetEvolutionSuspendDeopt();SetAsyncDeoptProbe(false);}return EvolutionSuspendDeoptHits();}
bool ConfigureCompiledFrameDiagnostics(HookEngine& hooks,const DeviceProfile& profile,std::string* error){
  if(!profile.Matched()) {if(error)*error="Binding requires a matched device profile";return false;}
#define GET(dst,name) dst=reinterpret_cast<decltype(dst)>(hooks.FindSymbol("libart.so",name));if(!dst){*error=std::string("Missing named compiled-frame diagnostic: ")+name;return false;}
 GET(suspend,compiled_frame_abi::kSuspend);GET(resume,compiled_frame_abi::kResume);GET(contains,compiled_frame_abi::kContains);
 GET(cache_contains,"_ZNK3art3jit12JitCodeCache10ContainsPcEPKv");GET(oat_code,"_ZN3art9ArtMethod21GetOatMethodQuickCodeENS_11PointerSizeE");
#undef GET
 if(!RuntimeInstance()){*error="Missing compiled-frame diagnostic Runtime";return false;}return true;
}
}
