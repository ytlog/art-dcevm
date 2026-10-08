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
size_t RejectedFieldEntries(){return rejected.load();}
}  // namespace artdcevm::retirement
