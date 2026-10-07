#include "platform/android13/device_diagnostics.h"
#include "platform/android13/device_diagnostics_abi.h"
#include "platform/android13/frame_recovery.h"
#include "platform/android13/native_object_access.h"
#include "platform/android13/member_decode_abi.h"
#include "platform/android13/art_method_abi.h"
#include "platform/android13/runtime_access.h"
#include "platform/android13/heap_reference.h"
#include "platform/android13/handle_root_scope.h"
#include <cstring>
#include <cstdlib>
namespace artdcevm::android13 {
namespace {
decltype(JNIEnv::functions) (*normal)();decltype(JNIEnv::functions) (*checked)();void (*set_check)(JNIEnv*,bool);
bool (*contains_method)(void*,void*);bool (*contains_pc)(const void*,const void*);
const void* (*oat_code)(void*,size_t);bool (*bridge)(const void*,const void*);const void* (*nterp)();const void* (*invoke)(void*,void*);
void (*invalidate)(void*);void** jit_lock;void (*lock)(void*,void*);void (*unlock)(void*,void*);
void (*visit)(void*,const diagnostics_abi::BorrowedFunction*);
template<class T>T Read(const void* p,size_t offset){T value;std::memcpy(&value,static_cast<const unsigned char*>(p)+offset,sizeof(value));return value;}
void* Method(NativeObjectAccess& access,jobject executable){return reinterpret_cast<void*>(Read<uint64_t>(access.Decode(executable),offsetof(member_decode_abi::Executable,art_method)));}
const void* Entry(void* method){return reinterpret_cast<void*>(Read<uint64_t>(method,offsetof(art_method_abi::ArtMethod,entry_point)));}
struct CacheCallback final:diagnostics_abi::CallbackBase {
 void* target;const void* entry;int state=0,entry_kind=0;
 CacheCallback(void* m,const void* e):target(m),entry(e){}
 CallbackBase* Clone()const override{std::abort();}void Clone(CallbackBase*)const override{std::abort();}void Destroy()noexcept override{std::abort();}void DestroyDeallocate()noexcept override{std::abort();}
 void Invoke(const void*&& code,void*&& owner)override{
  if(owner!=target || !code)return;
  state|=1;
  if(code==entry)state|=16;
  // VisitAllMethods establishes this code's actual carrier. A region lookup
  // alone also accepts stubs or another method's entry and cannot authorize
  // reading CodeInfo. JNI/runtime stubs have no managed CodeInfo.
  if((Read<uint32_t>(target,offsetof(art_method_abi::ArtMethod,access_flags))&0x100u) ||
     Read<uint32_t>(target,offsetof(art_method_abi::ArtMethod,dex_method_index))==UINT32_MAX ||
     !contains_pc(RuntimeJitCodeCache(),code))return;
  stack_abi::CodeInfo info{};
  const auto* header=static_cast<const unsigned char*>(code)-stack_abi::kCodeOffset;
  if(!ReadEvolutionCodeInfo(target,header,reinterpret_cast<uintptr_t>(code),&info))return;
  // Matched device ctor oracle: baseline(kind1) flags=6/inline=0,
  // optimized(kind2) flags=3/inline>0. Baseline is bit 2, not bit 1.
  // Bit 1 does not establish a debuggable artifact or recovery qualification.
  state|=2;
  if(info.flags&stack_abi::kIsBaseline)state|=4;
  if(info.flags&stack_abi::kHasInlineInfo)state|=8;
  if(code==entry)entry_kind=(info.flags&stack_abi::kIsBaseline)?1:(info.flags&stack_abi::kHasInlineInfo)?3:2;
 }
};
CacheCallback InspectCache(NativeObjectAccess& access,void* cache,void* method){
 CacheCallback callback(method,Entry(method));
 diagnostics_abi::BorrowedFunction function{};function.function=&callback;
 lock(*jit_lock,access.Self());visit(cache,&function);unlock(*jit_lock,access.Self());
 return callback;
}
}
int DiagnosticCheckJniMode(JNIEnv* env,int requested){int previous=env->functions==checked()?1:env->functions==normal()?0:-1;if(requested==0||requested==1)set_check(env,requested!=0);return previous;}
uint64_t DiagnosticReadBarrierCalls(){
#ifdef ARTDCEVM_TESTING
 return HeapReferenceMarkCalls();
#else
 return 0;
#endif
}
int DiagnosticJitMethodState(JNIEnv* env,jobject executable){NativeObjectAccess access(env);if(!access.Supported())return -1;auto* cache=RuntimeJitCodeCache();return !cache?0:contains_method(cache,Method(access,executable))?2:1;}
bool DiagnosticAllMethodsDeoptimized(){return Read<uint32_t>(RuntimeInstrumentation(),offsetof(diagnostics_abi::InstrumentationPrefix,level))==2;}
int DiagnosticAotMethodState(JNIEnv* env,jobject executable){NativeObjectAccess access(env);if(!access.Supported())return -1;void* m=Method(access,executable);const void* oat=oat_code(m,size_t{8});const void* entry=Entry(m);void* cache=RuntimeJitCodeCache();void* instrumentation=RuntimeInstrumentation();return (oat?1:0)|(oat&&entry==oat?2:0)|(cache&&contains_method(cache,m)?4:0)|(bridge(RuntimeClassLinker(),entry)?8:0)|(entry==nterp()?16:0)|(DiagnosticAllMethodsDeoptimized()?64:0)|(oat&&invoke(instrumentation,m)==oat?128:0);}
int DiagnosticJitCodeKind(JNIEnv* env,jobject executable){NativeObjectAccess access(env);if(!access.Supported())return -1;void* cache=RuntimeJitCodeCache();if(!cache)return 0;return InspectCache(access,cache,Method(access,executable)).entry_kind;}
int DiagnosticJitInlineState(JNIEnv* env,jobject executable){NativeObjectAccess access(env);if(!access.Supported())return -1;void* cache=RuntimeJitCodeCache();void* method=Method(access,executable);if(!cache||!contains_method(cache,method))return 0;int kind=InspectCache(access,cache,method).entry_kind;return !kind?1:kind==3?3:2;}
int DiagnosticJitCacheDetail(JNIEnv* env,jobject executable){NativeObjectAccess access(env);if(!access.Supported())return -1;void* cache=RuntimeJitCodeCache();if(!cache)return -1;return InspectCache(access,cache,Method(access,executable)).state;}
int DiagnosticInvalidateAllJit(JNIEnv* env){NativeObjectAccess access(env);if(!access.Supported())return -1;void* cache=RuntimeJitCodeCache();if(!cache)return -1;invalidate(cache);return 1;}

bool ConfigureDeviceDiagnostics(HookEngine& hooks,const DeviceProfile& profile,std::string* error){
  if(!profile.Matched()) {if(error)*error="Binding requires a matched device profile";return false;}
#define GET(dst,name) dst=reinterpret_cast<decltype(dst)>(hooks.FindSymbol("libart.so",name));if(!dst){*error="Missing named device diagnostic: " name;return false;}
 GET(normal,"_ZN3art21GetJniNativeInterfaceEv");GET(checked,"_ZN3art26GetCheckJniNativeInterfaceEv");GET(set_check,"_ZN3art9JNIEnvExt18SetCheckJniEnabledEb");
 GET(contains_method,"_ZN3art3jit12JitCodeCache14ContainsMethodEPNS_9ArtMethodE");GET(contains_pc,"_ZNK3art3jit12JitCodeCache10ContainsPcEPKv");GET(oat_code,"_ZN3art9ArtMethod21GetOatMethodQuickCodeENS_11PointerSizeE");GET(bridge,"_ZNK3art11ClassLinker26IsQuickToInterpreterBridgeEPKv");GET(nterp,"_ZN3art11interpreter18GetNterpEntryPointEv");GET(invoke,"_ZN3art15instrumentation15Instrumentation16GetCodeForInvokeEPNS_9ArtMethodE");GET(invalidate,"_ZN3art3jit12JitCodeCache25InvalidateAllCompiledCodeEv");GET(jit_lock,"_ZN3art5Locks9jit_lock_E");GET(lock,"_ZN3art5Mutex13ExclusiveLockEPNS_6ThreadE");GET(unlock,"_ZN3art5Mutex15ExclusiveUnlockEPNS_6ThreadE");GET(visit,"_ZN3art3jit12JitCodeCache15VisitAllMethodsERKNSt3__18functionIFvPKvPNS_9ArtMethodEEEE");
#undef GET
 return true;
}
}
