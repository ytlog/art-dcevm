#define main SourceHandleRegressionMain
#include "source_handle_roots_test.cc"
#undef main
#include "platform/common/native_lazy_field_mirror.h"
namespace {
namespace lazy=native_lazy_field_mirror;namespace obs=native_field_factory_observer;
struct PublicField {void* native;};
std::vector<std::unique_ptr<PublicField>> produced;
obs::Factory factory_proxy=nullptr;bool factory_pending=false,wrong_mirror_class=false,wrong_native=false,factory_oom=false;
size_t factory_calls=0;void* mirror_class_root=nullptr;const runnable_jni::Context* actual_context=nullptr;
obs::Return OriginalDeclaring(void* self,obs::HandleCarrier handle,void* field,bool force){
 assert(self==current&&!force&&handle.slot);++factory_calls;
 if(factory_oom){factory_pending=true;return {0};}
 assert(reinterpret_cast<void*>(uintptr_t(Read32(handle.slot)))==Root(field));
 auto result=std::make_unique<PublicField>();result->native=wrong_native?old_fields[1]:field;
 auto* raw=result.get();produced.push_back(std::move(result));return {uintptr_t(raw)};
}
bool NativeHandle(void*,void* self,const void* slot,void** out){assert(self==current);*out=reinterpret_cast<void*>(uintptr_t(Read32(slot)));return *out!=nullptr;}
bool NoChange(void*,void*,obs::HandleCarrier,void* field,bool,void** out)noexcept{*out=field;return true;}
class FactoryHooks final:public HookEngine {
 public:
 bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char*,const char* name)override{
  if(std::strstr(name,"NewLocalRef"))return reinterpret_cast<void*>(Local);
  if(std::strstr(name,"HandleINS0_5Class"))return factory_proxy?reinterpret_cast<void*>(factory_proxy):reinterpret_cast<void*>(OriginalDeclaring);
  return nullptr;
 }
 static jobject Local(JNIEnv*,void* value){return reinterpret_cast<jobject>(value);}
 void* Install(void* target,void* proxy,void** original,std::string*)override{assert(target==reinterpret_cast<void*>(OriginalDeclaring));*original=target;factory_proxy=reinterpret_cast<obs::Factory>(proxy);return this;}
 bool Remove(void*,std::string*)override{factory_proxy=nullptr;return true;}
};
jboolean FieldException(JNIEnv*){return factory_pending;}
jfieldID PublicId(JNIEnv*,jobject field){assert(!factory_pending);return reinterpret_cast<jfieldID>(reinterpret_cast<PublicField*>(field)->native);}
jclass PublicClass(JNIEnv*,jobject){assert(!factory_pending);return reinterpret_cast<jclass>(wrong_mirror_class?parent_class:mirror_class_root);}
jmethodID PublicMethod(JNIEnv*,jclass,const char* name,const char*){assert(!factory_pending);return reinterpret_cast<jmethodID>(!std::strcmp(name,"getDeclaringClass")?1:2);}
jobject PublicOwner(JNIEnv*,jobject field,jmethodID id,va_list){assert(id==reinterpret_cast<jmethodID>(1));return reinterpret_cast<jobject>(Root(reinterpret_cast<PublicField*>(field)->native));}
jint PublicFlags(JNIEnv*,jobject field,jmethodID id,va_list){assert(id==reinterpret_cast<jmethodID>(2));return jint(Read32(static_cast<unsigned char*>(reinterpret_cast<PublicField*>(field)->native)+4));}
bool FieldScope(void*,JNIEnv* env,jobject,runnable_jni::Operation operation,void* data){assert(actual_context&&env==actual_context->env);return operation(*actual_context,data);}
bool FieldOracle(void*,const runnable_jni::Context& context,jobject field,void** out){assert(context.self==current);*out=field;return true;}
bool FieldDecode(void*,jobject field,void** out){*out=field;return true;}
void* ContextId(void*,const runnable_jni::Context&,void* owner,jfieldID id){return Root(id)==owner?id:nullptr;}
void* PublicNative(void*,void* field){return static_cast<PublicField*>(field)->native;}
void* NativeOwner(void*,void* field){return Root(field);}
bool TestDeclaring(const runnable_jni::Context& context,modern_field_metadata::FieldMetadata&,source_handle_roots::Service& roots,modern_field_storage::Service&){
 actual_context=&context;const auto saved=table;mirror_class_root=meta_class;
 table.ExceptionCheck=FieldException;table.FromReflectedField=PublicId;table.GetObjectClass=PublicClass;
 table.GetMethodID=PublicMethod;table.CallObjectMethodV=PublicOwner;table.CallIntMethodV=PublicFlags;
 FactoryHooks hooks;obs::Service observer;std::string error;assert(observer.Bind(hooks,{&runtime,Current,NativeHandle},&error));
 void* native_roots[]{old_class,new_class};exclusive_owned_roots::Context exclusive{current,native_roots,2};
 assert(observer.InstallAtExclusive(hooks,exclusive,&error));assert(observer.SetNormalizerAtExclusive({&runtime,NoChange},exclusive,&error));
 obs::Trace traces[2];std::array<bool,4> public_roots{};PublicField public_fields[4]{{old_fields[0]},{old_fields[1]},{new_fields[0]},{new_fields[1]}};
 for(size_t episode=0;episode<2;++episode){std::array<void*,2> fields{public_fields[episode*2].native,public_fields[episode*2+1].native};assert(observer.Begin(current,fields,&error));
  auto handle=roots.CreateScope(1,&error);assert(handle);handle->Set(0,Root(fields[0]));
  uintptr_t returns[2]{};for(size_t i=0;i<2;++i)returns[i]=factory_proxy(current,{handle->Slot(0)},fields[i],false).reference;
  assert(observer.End(&traces[episode],&error));
  for(size_t i=0;i<2;++i){auto& entry=traces[episode].entries[i];public_roots[episode*2+i]=obs::DeclaringHandleProof(entry,Root(fields[i]),reinterpret_cast<void*>(returns[i]));assert(public_roots[episode*2+i]);assert(!obs::DeclaringHandleProof(entry,mirror_class_root,reinterpret_cast<void*>(returns[i])));assert(!obs::DeclaringHandleProof(entry,Root(fields[i])==old_class?new_class:old_class,reinterpret_cast<void*>(returns[i])));}
 }
 lazy::Bindings binding{&runtime,true,FieldScope,FieldOracle,FieldDecode,ContextId,PublicNative,NativeOwner,&observer,&traces[0],&traces[1],&public_roots,&roots};
 lazy::Service service;assert(service.Bind(hooks,binding,&error));std::vector<lazy::Oracle> samples;
 for(size_t i=0;i<4;++i)samples.push_back({reinterpret_cast<jobject>(&public_fields[i]),reinterpret_cast<jclass>(Root(public_fields[i].native)),reinterpret_cast<jfieldID>(public_fields[i].native),public_fields[i].native,(Read32(static_cast<unsigned char*>(public_fields[i].native)+4)&8)!=0});
 assert(service.Calibrate(context.env,samples,&error)&&service.Ready());jobject output=nullptr;
 assert(service.Create(context.env,samples[2].exact_owner,samples[2].id,samples[2].is_static,&output,&error)&&output);context.env->DeleteGlobalRef(output);
 const auto before=factory_calls;assert(!service.Create(context.env,samples[0].exact_owner,samples[2].id,samples[2].is_static,&output,&error)&&factory_calls==before);
 factory_oom=true;assert(!service.Create(context.env,samples[0].exact_owner,samples[0].id,samples[0].is_static,&output,&error)&&factory_pending);factory_oom=false;
 const auto pending_calls=factory_calls;assert(!service.Create(context.env,samples[0].exact_owner,samples[0].id,samples[0].is_static,&output,&error)&&factory_calls==pending_calls&&factory_pending);factory_pending=false;
 wrong_mirror_class=true;assert(!service.Create(context.env,samples[0].exact_owner,samples[0].id,samples[0].is_static,&output,&error));wrong_mirror_class=false;
 wrong_native=true;assert(!service.Create(context.env,samples[0].exact_owner,samples[0].id,samples[0].is_static,&output,&error));wrong_native=false;
 service.Clear(context.env);assert(observer.RemoveAtExclusive(hooks,exclusive,&error));table=saved;actual_context=nullptr;return true;
}
}
int main(){source_probe=TestDeclaring;return SourceHandleRegressionMain();}
