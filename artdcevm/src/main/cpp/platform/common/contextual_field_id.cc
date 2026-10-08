#include "platform/common/contextual_field_id.h"
#include "platform/common/runnable_executor.h"
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <vector>
namespace artdcevm::contextual_field_id {
namespace {
using Leaf=void*(*)(void*);
std::atomic<Leaf> leaf{nullptr};
std::mutex aliases_mutex;
bool Fail(std::string* error,const char* message){if(error)*error=message;return false;}
struct Observe {jobject root;void* field=nullptr;};
bool ObserveField(const runnable_jni::Context& scope,void* opaque){
 auto& request=*static_cast<Observe*>(opaque);void* mirror=nullptr;
 auto named=leaf.load(std::memory_order_acquire);
 if(!named||!runnable_executor::DecodeStrongGlobal(scope,request.root,&mirror))return false;
 request.field=named(mirror);return request.field!=nullptr;
}
void* Read(JNIEnv* env,jobject mirror,std::string* error){
 if(!env||!mirror||env->ExceptionCheck())return nullptr;
 jobject root=env->NewGlobalRef(mirror);if(!root||env->ExceptionCheck()){if(root)env->DeleteGlobalRef(root);return nullptr;}
 Observe request{root};auto result=runnable_executor::With(env,root,ObserveField,&request);
 env->DeleteGlobalRef(root);
 if(result!=runnable_jni::Result::kSuccess){Fail(error,"Owned public reflected Field identity unavailable");return nullptr;}
 return request.field;
}
struct Locals {
 JNIEnv* env;std::vector<jobject> refs;
 jobject Own(jobject value){if(value)refs.push_back(value);return value;}
 ~Locals(){for(auto value:refs)env->DeleteLocalRef(value);}
};
std::string Utf(JNIEnv* env,jstring text){
 if(!text)return {};const char* chars=env->GetStringUTFChars(text,nullptr);if(!chars)return {};
 std::string result(chars);env->ReleaseStringUTFChars(text,chars);return result;
}
std::string Descriptor(std::string name){
 if(name=="boolean")return "Z";if(name=="byte")return "B";if(name=="char")return "C";
 if(name=="short")return "S";if(name=="int")return "I";if(name=="long")return "J";
 if(name=="float")return "F";if(name=="double")return "D";
 for(auto& c:name)if(c=='.')c='/';
 return !name.empty()&&name[0]=='['?name:"L"+name+";";
}
}
struct Proof {
 std::atomic<jweak> owner{nullptr};jfieldID id=nullptr;void* native=nullptr;bool is_static=false;
 std::shared_ptr<const retirement::RetiredFieldRegistry::Lifetime> lifetime;uint32_t token=0;uint64_t generation=0;std::atomic<bool> published{false},invalid{false};
};
namespace {
struct Slot {std::shared_ptr<Proof> proof;Slot* next;};
std::atomic<Slot*> alias_head{nullptr};
std::atomic<size_t> native_readers{0};
std::vector<Slot*> deferred_slots;
bool SameRetirement(const Proof& p){
 auto identity=p.lifetime->IdentityFor(p.native);return identity.token==p.token&&identity.generation==p.generation;
}
void DeleteChain(Slot* slot){while(slot){auto* next=slot->next;delete slot;slot=next;}}
// Ordinary Native JNI only. Native publishers never acquire aliases_mutex.
// Immutable slot snapshots stay alive while a GC notification is reading them.
void RebuildSlots(){
 Slot* replacement=nullptr;
 for(auto* slot=alias_head.load(std::memory_order_acquire);slot;slot=slot->next)
  if(slot->proof->owner)replacement=new Slot{slot->proof,replacement};
 auto* old=alias_head.exchange(replacement,std::memory_order_seq_cst);
 if(old)deferred_slots.push_back(old);
 if(native_readers.load(std::memory_order_seq_cst)==0){for(auto* slots:deferred_slots)DeleteChain(slots);deferred_slots.clear();}
}
}

bool Initialize(HookEngine& hooks,std::string* error){
 auto candidate=reinterpret_cast<Leaf>(hooks.FindSymbol("libart.so","_ZN3art6mirror5Field11GetArtFieldEv"));
 if(!candidate||!runnable_executor::Available())return Fail(error,"Missing owned public Field identity service");
 Leaf expected=nullptr;if(!leaf.compare_exchange_strong(expected,candidate,std::memory_order_release,std::memory_order_acquire)&&expected!=candidate)return Fail(error,"Contextual Field identity owner changed");return true;
}
std::shared_ptr<Proof> Prepare(JNIEnv* env,jobject reflected,jfieldID id,std::string* error){
 if(!leaf||!env||!reflected||!id||env->ExceptionCheck()){Fail(error,"Invalid contextual Field oracle");return {};}
 Locals locals{env,{}};
 auto public_field_type=static_cast<jclass>(locals.Own(env->FindClass("java/lang/reflect/Field")));
 if(!public_field_type||env->ExceptionCheck()||!env->IsInstanceOf(reflected,public_field_type)){Fail(error,"Contextual oracle is not a public Field");return {};}
 auto field_type=static_cast<jclass>(locals.Own(env->GetObjectClass(reflected)));
 if(!field_type||env->ExceptionCheck())return {};
 auto declaring=env->GetMethodID(field_type,"getDeclaringClass","()Ljava/lang/Class;");
 if(!declaring||env->ExceptionCheck())return {};
 auto name_id=env->GetMethodID(field_type,"getName","()Ljava/lang/String;");
 if(!name_id||env->ExceptionCheck())return {};
 auto type_id=env->GetMethodID(field_type,"getType","()Ljava/lang/Class;");
 if(!type_id||env->ExceptionCheck())return {};
 auto modifiers=env->GetMethodID(field_type,"getModifiers","()I");
 if(!modifiers||env->ExceptionCheck())return {};
 auto equals=env->GetMethodID(field_type,"equals","(Ljava/lang/Object;)Z");
 if(!equals||env->ExceptionCheck())return {};
 if(!declaring||!name_id||!type_id||!modifiers||!equals||env->ExceptionCheck())return {};
 auto owner=static_cast<jclass>(locals.Own(env->CallObjectMethod(reflected,declaring)));
 if(!owner||env->ExceptionCheck())return {};
 auto name=static_cast<jstring>(locals.Own(env->CallObjectMethod(reflected,name_id)));
 if(!name||env->ExceptionCheck())return {};
 auto type=static_cast<jclass>(locals.Own(env->CallObjectMethod(reflected,type_id)));
 if(!type||env->ExceptionCheck())return {};
 const bool is_static=(env->CallIntMethod(reflected,modifiers)&8)!=0;
 if(env->ExceptionCheck())return {};
 auto class_type=static_cast<jclass>(locals.Own(env->FindClass("java/lang/Class")));
 if(!class_type||env->ExceptionCheck())return {};
 auto class_name=class_type?env->GetMethodID(class_type,"getName","()Ljava/lang/String;"):nullptr;
 if(!class_name||env->ExceptionCheck())return {};
 auto type_name=static_cast<jstring>(locals.Own(class_name?env->CallObjectMethod(type,class_name):nullptr));
 if(!owner||!name||!type_name||env->ExceptionCheck())return {};
 auto field_name=Utf(env,name);if(env->ExceptionCheck())return {};
 auto descriptor=Descriptor(Utf(env,type_name));if(env->ExceptionCheck())return {};
 auto public_id=is_static?env->GetStaticFieldID(owner,field_name.c_str(),descriptor.c_str()):env->GetFieldID(owner,field_name.c_str(),descriptor.c_str());
 if(env->ExceptionCheck())return {};
 if(public_id!=id||env->FromReflectedField(reflected)!=id){Fail(error,"Public Field name/type/owner ID disagrees with FromReflectedField");return {};}
 auto returned=locals.Own(env->ToReflectedField(owner,id,is_static?JNI_TRUE:JNI_FALSE));
 if(!returned||env->ExceptionCheck())return {};
 if(!env->CallBooleanMethod(reflected,equals,returned)||env->FromReflectedField(returned)!=id||env->ExceptionCheck()){
  Fail(error,"Public ToReflectedField round trip differs from original Field");return {};
 }
 void* first=Read(env,reflected,error);if(!first||env->ExceptionCheck())return {};
 void* second=Read(env,returned,error);
 if(!first||first!=second||env->ExceptionCheck()){Fail(error,"Owned native Field round trip differs");return {};}
 {
  std::lock_guard<std::mutex> lock(aliases_mutex);
  for(auto* slot=alias_head.load(std::memory_order_acquire);slot;slot=slot->next){const auto& existing=slot->proof;
   if(existing->published&&!existing->invalid&&existing->id==id&&existing->is_static==is_static&&
      SameRetirement(*existing)&&env->IsSameObject(existing->owner,owner)){
    Fail(error,"Live Field ID collides with an existing retired namespace/generation");return {};
   }
  }
 }
 auto proof=std::make_shared<Proof>();proof->owner=env->NewWeakGlobalRef(owner);
 if(!proof->owner||env->ExceptionCheck()){if(proof->owner)env->DeleteWeakGlobalRef(proof->owner);proof->owner=nullptr;return {};}
 proof->id=id;proof->native=first;proof->is_static=is_static;
 {std::lock_guard<std::mutex> lock(aliases_mutex);auto* slot=new Slot{proof,alias_head.load(std::memory_order_relaxed)};alias_head.store(slot,std::memory_order_release);}return proof;
}
void* NativeField(const std::shared_ptr<Proof>& p){return p?p->native:nullptr;}
jfieldID Id(const std::shared_ptr<Proof>& p){return p?p->id:nullptr;}
bool IsStatic(const std::shared_ptr<Proof>& p){return p&&p->is_static;}
void* DecodeLive(JNIEnv* env,jobject receiver,bool is_static,jfieldID id,std::string* error){
 if(!env||!receiver||!id||env->ExceptionCheck())return nullptr;
 Locals locals{env,{}};auto owner=is_static?static_cast<jclass>(receiver):static_cast<jclass>(locals.Own(env->GetObjectClass(receiver)));
 if(!owner||env->ExceptionCheck())return nullptr;
 auto reflected=locals.Own(env->ToReflectedField(owner,id,is_static?JNI_TRUE:JNI_FALSE));
 if(!reflected||env->ExceptionCheck())return nullptr;
 if(env->FromReflectedField(reflected)!=id||env->ExceptionCheck()){Fail(error,"Live public Field ID round trip differs");return nullptr;}
 return Read(env,reflected,error);
}
void PublishRetired(const std::shared_ptr<Proof>& p,retirement::RetiredFieldRegistry& registry){
 if(!p||!p->owner||!p->native)std::abort();
 auto identity=registry.IdentityFor(p->native);uint32_t token=identity.token;uint64_t generation=identity.generation;if(!token||!generation)std::abort();
 if(p->published){if(p->lifetime!=registry.LifetimeState()||p->token!=token||p->generation!=generation)std::abort();return;}
 p->lifetime=registry.LifetimeState();p->token=token;p->generation=generation;p->published.store(true,std::memory_order_release);
}
bool Retired(JNIEnv* env,jobject receiver,bool is_static,jfieldID id,retirement::RetiredFieldRegistry& registry){
 if(!env||!receiver||!id||env->ExceptionCheck())return false;
 Locals locals{env,{}};auto owner=is_static?static_cast<jclass>(receiver):static_cast<jclass>(locals.Own(env->GetObjectClass(receiver)));
 if(!owner||env->ExceptionCheck())return false;
 std::lock_guard<std::mutex> lock(aliases_mutex);
 for(auto* slot=alias_head.load(std::memory_order_acquire);slot;slot=slot->next){const auto& p=slot->proof;
  if(!p->published||p->invalid||p->lifetime!=registry.LifetimeState()||p->is_static!=is_static||p->id!=id||!SameRetirement(*p))continue;
  auto declaring=static_cast<jclass>(locals.Own(env->NewLocalRef(p->owner)));
  if(env->ExceptionCheck())return false;
  if(declaring&&env->IsAssignableFrom(owner,declaring))return true;
  if(env->ExceptionCheck())return false;
 }
 return false;
}
void NativeNotifyPrune(retirement::RetiredFieldRegistry& registry){
 // Never wait for ordinary JNI readers or take their mutex under SuspendAll.
 native_readers.fetch_add(1,std::memory_order_seq_cst);
 for(auto* slot=alias_head.load(std::memory_order_seq_cst);slot;slot=slot->next){const auto& p=slot->proof;
  if(p->published.load(std::memory_order_acquire)&&(!p->lifetime->Alive()||(p->lifetime==registry.LifetimeState()&&!SameRetirement(*p))))p->invalid.store(true,std::memory_order_release);
 }
 native_readers.fetch_sub(1,std::memory_order_seq_cst);
}
void Prune(JNIEnv* env,retirement::RetiredFieldRegistry& registry){
 if(!env||env->ExceptionCheck())return;
 std::lock_guard<std::mutex> lock(aliases_mutex);
 for(auto* slot=alias_head.load(std::memory_order_acquire);slot;slot=slot->next){const auto& p=slot->proof;
  if(p->owner&&p->published.load(std::memory_order_acquire)&&
      (!p->lifetime->Alive()||(p->lifetime==registry.LifetimeState()&&(p->invalid.load(std::memory_order_acquire)||!SameRetirement(*p)||env->IsSameObject(p->owner,nullptr))))){
   p->invalid.store(true,std::memory_order_release);env->DeleteWeakGlobalRef(p->owner);p->owner=nullptr;
  }
 }
 RebuildSlots();
}
void Release(JNIEnv* env,std::shared_ptr<Proof>* p){
 if(!p||!*p)return;std::lock_guard<std::mutex> lock(aliases_mutex);
 if(!(*p)->published){if((*p)->owner){env->DeleteWeakGlobalRef((*p)->owner);(*p)->owner=nullptr;}
  RebuildSlots();}p->reset();
}
}
