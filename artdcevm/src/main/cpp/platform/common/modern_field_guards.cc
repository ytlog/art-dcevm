#include "platform/common/modern_field_guards.h"
#include "platform/common/runnable_executor.h"
#include "platform/abi/arm64/android-common/suspend_all.h"
#include <atomic>
#include <cstdlib>
#include <type_traits>
#include <vector>
namespace artdcevm::retirement {
namespace {
using Decode=void* (*)(void*,jfieldID);
using Throw=void (*)(void*,const char*,const char*);
struct State {RetiredFieldRegistry* registry=nullptr;jobject anchor=nullptr;
  void* manager=nullptr;Decode decode=nullptr;Throw throw_exception=nullptr;};
State state;
bool installed=false;
thread_local bool probing=false;
std::atomic<size_t> rejected{0};
void* original_field=nullptr;
void* original_visit=nullptr;
void* GetField(void* mirror) {
  reflection_metadata::FieldIdentity identity;
  if(!reflection_metadata::ReadField(field_mirror::kDeclaration,mirror,
      field_mirror::kDeclaration.field_size,&identity))std::abort();
  if(identity.array_index&kRetiredFieldTag) {
    void* field=state.registry->Resolve(identity.array_index);
    if(!field)std::abort(); // A live tagged mirror roots the owning obsolete Class.
    return field;
  }
  return reinterpret_cast<field_mirror::NamedField>(original_field)(mirror);
}
void VisitField(void* mirror,void* visitor) {
  void* before=GetField(mirror);
  reinterpret_cast<void(*)(void*,void*)>(original_visit)(mirror,visitor);
  // The original supplies the native HeapReflectiveSourceInfo and performs
  // surviving-member metadata/barrier updates. The real mapper can retire a
  // field during its callback; mark afterward, before managed root replacement.
  const uint32_t token=state.registry->TokenFor(before);
  if(token)std::memcpy(static_cast<unsigned char*>(mirror)+field_mirror::kDeclaration.field_index,&token,4);
}
struct Request {jfieldID id=nullptr;jobject mirror=nullptr;bool guarded=false;};
bool Observe(const runnable_jni::Context& context,void* data) {
  auto& request=*static_cast<Request*>(data);
  void* field=nullptr;
  if(request.mirror) {
    void* mirror=nullptr;
    if(!runnable_executor::DecodeStrongGlobal(context,request.mirror,&mirror))return false;
    field=GetField(mirror);
  } else field=state.decode(state.manager,request.id);
  if(!field)return false;
  request.guarded=state.registry->TokenFor(field)!=0;
  if(request.guarded) {
    ++rejected;
    state.throw_exception(context.self,"Ljava/lang/NoSuchFieldError;","Field removed by ART DCEVM");
  }
  return true;
}
bool Guard(JNIEnv* env,jfieldID id,jobject reflected) {
  if(probing||state.registry->Count()==0)return false;
  if(env->ExceptionCheck())return true; // Keep the caller's existing exception.
  struct Probe {Probe(){probing=true;}~Probe(){probing=false;}} probe;
  jobject global=reflected?env->NewGlobalRef(reflected):nullptr;
  Request request{id,global,false};
  auto result=reflected&&!global?runnable_jni::Result::kInvalidRequest:
      runnable_executor::With(env,global?global:state.anchor,Observe,&request);
  if(global)env->DeleteGlobalRef(global);
  if(request.guarded)return true;
  if(result==runnable_jni::Result::kSuccess)return false;
  // Never delegate a potentially stale ID when owned access fails. Preserve a
  // bridge/native exception; otherwise report the unavailable guard explicitly.
  if(!env->ExceptionCheck()) {
    auto error=env->FindClass("java/lang/IllegalStateException");
    if(error)env->ThrowNew(error,"ART DCEVM retired field guard lost owned access");
  }
  return true;
}
void *original_handle_get=nullptr,*original_handle_put=nullptr,*original_handle_exact=nullptr,*original_handle_invoke=nullptr;
// handle.h defaulted copy declaration: forwarding an opaque native root slot
// by value preserves ARM64 C++ argument ABI; we never construct/dereference it.
struct NativeHandleArgument {void* slot;};
static_assert(sizeof(NativeHandleArgument)==8);
struct HandleExit {bool getter_rejected=false;HandleExit* previous;};
thread_local HandleExit* active_handle_exit=nullptr;
bool handles_installed=false,handles_incomplete=false;
bool GuardNativeField(void* self,void* field,void* result=nullptr) {
  if(!state.registry->TokenFor(field))return false;
  if(result){const uint64_t zero=0;std::memcpy(result,&zero,8);}
  ++rejected;state.throw_exception(self,"Ljava/lang/NoSuchFieldError;","Field removed by ART DCEVM");return true;
}
// Fixed15 method_handles.cc: Get returns void, Put returns bool. Every frame,
// receiver reference and JValue is passed through opaquely with its native ABI.
void HandleGet(void* self,const void* frame,void* receiver,void* field,int primitive,void* result) {
  auto* exit=active_handle_exit;
  if(GuardNativeField(self,field,result)){if(exit)exit->getter_rejected=true;return;}
  reinterpret_cast<void(*)(void*,const void*,void*,void*,int,void*)>(original_handle_get)(self,frame,receiver,field,primitive,result);
}
bool HandlePut(void* self,void* frame,void* receiver,void* field,int primitive,void* value) {
  if(GuardNativeField(self,field))return false;
  return reinterpret_cast<bool(*)(void*,void*,void*,void*,int,void*)>(original_handle_put)(self,frame,receiver,field,primitive,value);
}
template<bool exact> bool HandleInvoke(void* self,void* frame,NativeHandleArgument handle,
    NativeHandleArgument type,const void* operands,void* result) {
  HandleExit exit{false,active_handle_exit};active_handle_exit=&exit;
  struct Restore {HandleExit* prior;~Restore(){active_handle_exit=prior;}} restore{exit.previous};
  using Fn=bool (*)(void*,void*,NativeHandleArgument,NativeHandleArgument,const void*,void*);
  const bool success=reinterpret_cast<Fn>(exact?original_handle_exact:original_handle_invoke)(self,frame,handle,type,operands,result);
  // The native getter helper returns void and its caller unconditionally says
  // success. A pending exception must reach invoke-polymorphic as false so the
  // interpreter dispatches it at the correct bytecode/catch handler.
  return exit.getter_rejected?false:success;
}
struct Entry {std::string symbol;void* proxy;void** original;void* target=nullptr;};
std::string JniSymbol(int indexed,bool is_static,bool setter,const char* type,const char* code,bool object) {
  std::string name=std::string(setter?"Set":"Get")+(is_static?"Static":"")+type+"Field";
  std::string receiver=is_static?"EP7_JNIEnvP7_jclassP9_jfieldID":"EP7_JNIEnvP8_jobjectP9_jfieldID";
  std::string argument=object?(is_static?"P8_jobject":"S5_"):code;
  return "_ZN3art3JNIILb"+std::to_string(indexed)+"EE"+std::to_string(name.size())+name+receiver+(setter?argument:"");
}
std::string ReflectedSymbol(bool setter,const char* type,const char* code,bool object) {
  std::string name=std::string(setter?"Field_set":"Field_get")+(object?"":type);
  return "_ZN3artL"+std::to_string(name.size())+name+"EP7_JNIEnvP8_jobjectS3_"+(setter?(object?"S3_":code):"");
}
template<int indexed,class T,bool is_static> struct JniField {
  using Receiver=std::conditional_t<is_static,jclass,jobject>;
  static inline void *get=nullptr,*set=nullptr;
  static T Get(JNIEnv* env,Receiver receiver,jfieldID id) {
    if(Guard(env,id,nullptr))return T{};
    return reinterpret_cast<T(*)(JNIEnv*,Receiver,jfieldID)>(get)(env,receiver,id);
  }
  static void Set(JNIEnv* env,Receiver receiver,jfieldID id,T value) {
    if(Guard(env,id,nullptr))return;
    reinterpret_cast<void(*)(JNIEnv*,Receiver,jfieldID,T)>(set)(env,receiver,id,value);
  }
  static void Add(std::vector<Entry>& entries,const char* type,const char* code) {
    constexpr bool object=std::is_same_v<T,jobject>;
    entries.push_back({JniSymbol(indexed,is_static,false,type,code,object),reinterpret_cast<void*>(Get),&get});
    entries.push_back({JniSymbol(indexed,is_static,true,type,code,object),reinterpret_cast<void*>(Set),&set});
  }
};
template<class T> struct ReflectionField {
  static inline void *get=nullptr,*set=nullptr;
  static T Get(JNIEnv* env,jobject field,jobject receiver) {
    if(Guard(env,nullptr,field))return T{};
    return reinterpret_cast<T(*)(JNIEnv*,jobject,jobject)>(get)(env,field,receiver);
  }
  static void Set(JNIEnv* env,jobject field,jobject receiver,T value) {
    if(Guard(env,nullptr,field))return;
    reinterpret_cast<void(*)(JNIEnv*,jobject,jobject,T)>(set)(env,field,receiver,value);
  }
  static void Add(std::vector<Entry>& entries,const char* type,const char* code) {
    constexpr bool object=std::is_same_v<T,jobject>;
    entries.push_back({ReflectedSymbol(false,type,code,object),reinterpret_cast<void*>(Get),&get});
    entries.push_back({ReflectedSymbol(true,type,code,object),reinterpret_cast<void*>(Set),&set});
  }
};
}
bool InstallFieldEntryGuards(HookEngine& hooks,JNIEnv* env,jobject anchor,void* manager,
    const field_mirror::Calibration& mirror,RetiredFieldRegistry& registry,std::string* error) {
  if(state.registry) {
    if(installed&&state.registry==&registry&&state.manager==manager&&state.anchor==anchor)return true;
    *error="Field entry guards already owned or rollback incomplete";return false;
  }
  if(!env||!anchor||!manager||!mirror.matched||mirror.checked<2||!runnable_executor::Available()||env->ExceptionCheck()) {
    *error="Field entry guards require actual complete mirror/owned JNI calibration";return false;
  }
  using namespace abi::arm64::android_common;
  State candidate{&registry,anchor,manager,
      reinterpret_cast<Decode>(hooks.FindSymbol("libart.so","_ZN3art3jni12JniIdManager13DecodeFieldIdEP9_jfieldID")),
      reinterpret_cast<Throw>(hooks.FindSymbol("libart.so","_ZN3art6Thread17ThrowNewExceptionEPKcS2_"))};
  auto construct=reinterpret_cast<SuspendConstruct>(hooks.FindSymbol("libart.so",kSuspendConstruct));
  auto destroy=reinterpret_cast<SuspendDestroy>(hooks.FindSymbol("libart.so",kSuspendDestroy));
  if(!candidate.decode||!candidate.throw_exception||!construct||!destroy) {
    *error="Missing named field decoder/exception/exclusive hook installation declarations";return false;
  }
  std::vector<Entry> entries{
    {"_ZN3art6mirror5Field11GetArtFieldEv",reinterpret_cast<void*>(GetField),&original_field},
    {"_ZN3art6mirror5Field11VisitTargetEPNS_22ReflectiveValueVisitorE",reinterpret_cast<void*>(VisitField),&original_visit}};
#define ADD(TYPE,T,CODE) JniField<0,T,false>::Add(entries,#TYPE,CODE); JniField<1,T,false>::Add(entries,#TYPE,CODE); \
  JniField<0,T,true>::Add(entries,#TYPE,CODE); JniField<1,T,true>::Add(entries,#TYPE,CODE); ReflectionField<T>::Add(entries,#TYPE,CODE)
  ADD(Boolean,jboolean,"h");ADD(Byte,jbyte,"a");ADD(Char,jchar,"t");ADD(Short,jshort,"s");
  ADD(Int,jint,"i");ADD(Long,jlong,"l");ADD(Float,jfloat,"f");ADD(Double,jdouble,"d");ADD(Object,jobject,"");
#undef ADD
  for(auto& entry:entries) {
    entry.target=hooks.FindSymbol("libart.so",entry.symbol.c_str());
    if(!entry.target){*error="Missing field guard entry: "+entry.symbol;return false;}
  }
  SuspendAll stopped{};construct(&stopped,"artdcevm field guard installation",false);
  struct Stop {SuspendAll* value;SuspendDestroy destroy;~Stop(){destroy(value);}} stop{&stopped,destroy};
  state=candidate;
  std::vector<void*> tokens;
  for(auto& entry:entries) {
    void* token=hooks.Install(entry.target,entry.proxy,entry.original,error);
    if(!token) {
      bool removed=true;
      for(auto it=tokens.rbegin();it!=tokens.rend();++it) {
        std::string rollback;
        if(!hooks.Remove(*it,&rollback)){removed=false;*error+="; "+rollback;}
      }
      if(removed)state={};
      return false;
    }
    tokens.push_back(token);
  }
  installed=true;return true;
}
bool InstallMethodHandleFieldGuards(HookEngine& hooks,std::string* error) {
  if(handles_installed)return true;
  if(!installed||handles_incomplete){*error="MethodHandle field guards require a complete durable field owner";return false;}
  const char* get_name="_ZN3art12_GLOBAL__N_120MethodHandleFieldGetEPNS_6ThreadERKNS_11ShadowFrameERNS_6ObjPtrINS_6mirror6ObjectEEEPNS_8ArtFieldENS_9Primitive4TypeEPNS_6JValueE";
  const char* put_name="_ZN3art12_GLOBAL__N_120MethodHandleFieldPutEPNS_6ThreadERNS_11ShadowFrameERNS_6ObjPtrINS_6mirror6ObjectEEEPNS_8ArtFieldENS_9Primitive4TypeERNS_6JValueE";
  Entry entries[]={
    {get_name,reinterpret_cast<void*>(HandleGet),&original_handle_get},
    {put_name,reinterpret_cast<void*>(HandlePut),&original_handle_put},
    {"_ZN3art23MethodHandleInvokeExactEPNS_6ThreadERNS_11ShadowFrameENS_6HandleINS_6mirror12MethodHandleEEENS4_INS5_10MethodTypeEEEPKNS_19InstructionOperandsEPNS_6JValueE",reinterpret_cast<void*>(HandleInvoke<true>),&original_handle_exact},
    {"_ZN3art18MethodHandleInvokeEPNS_6ThreadERNS_11ShadowFrameENS_6HandleINS_6mirror12MethodHandleEEENS4_INS5_10MethodTypeEEEPKNS_19InstructionOperandsEPNS_6JValueE",reinterpret_cast<void*>(HandleInvoke<false>),&original_handle_invoke}};
  for(auto& entry:entries){entry.target=hooks.FindSymbol("libart.so",entry.symbol.c_str());
    if(!entry.target){*error="Missing MethodHandle field dispatch declaration: "+entry.symbol;return false;}}
  using namespace abi::arm64::android_common;
  auto construct=reinterpret_cast<SuspendConstruct>(hooks.FindSymbol("libart.so",kSuspendConstruct));
  auto destroy=reinterpret_cast<SuspendDestroy>(hooks.FindSymbol("libart.so",kSuspendDestroy));
  if(!construct||!destroy){*error="Missing native MethodHandle guard installation stop";return false;}
  SuspendAll stopped{};construct(&stopped,"artdcevm MethodHandle field guards",false);
  struct Stop {SuspendAll* value;SuspendDestroy destroy;~Stop(){destroy(value);}} stop{&stopped,destroy};
  std::vector<void*> tokens;
  for(auto& entry:entries) {
    void* token=hooks.Install(entry.target,entry.proxy,entry.original,error);
    if(!token) {for(auto it=tokens.rbegin();it!=tokens.rend();++it){std::string rollback;
        if(!hooks.Remove(*it,&rollback)){handles_incomplete=true;*error+="; "+rollback;}}return false;}
    tokens.push_back(token);
  }
  handles_installed=true;return true;
}
size_t RejectedFieldEntries(){return rejected.load();}
}  // namespace artdcevm::retirement
