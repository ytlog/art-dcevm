#include "platform/common/native_receiver_entry_source.h"
#include "platform/common/method_identity_reader.h"
#include "platform/abi/arm64/android14/jni_global_decode.h"
#include <cstring>
#include <cstdlib>
namespace artdcevm::receiver_guard {
namespace {
// Fixed runtime/indirect_reference_table.h, Android 11--17. Public reference
// kinds/identities independently calibrate local/global; weak is never decoded.
constexpr uintptr_t kKindMask=3,kTransition=0,kLocal=1,kGlobal=2,kWeak=3;
constexpr uint32_t kPublicFlags=0xffff;
bool Fail(std::string*e,const char*t){if(e)*e=t;return false;}
struct Roots {
 JNIEnv*env; jobject roots[12]{};size_t count=0;
 ~Roots(){for(size_t i=0;i<count;++i)if(roots[i])env->DeleteGlobalRef(roots[i]);}
 jobject Add(jobject local){if(!local||count==12||env->ExceptionCheck())return nullptr;auto r=env->NewGlobalRef(local);if(r)roots[count++]=r;return env->ExceptionCheck()?nullptr:r;}
};
struct Local {JNIEnv*env;jobject value=nullptr;~Local(){if(value)env->DeleteLocalRef(value);}};
}
struct EntrySource::Proof {
 EntrySource* source;JNIEnv*env;
 jobject methods[2]{},fields[2]{},receivers[2]{},method_classes[2]{},field_classes[2]{},object_classes[2]{};
 jmethodID method_ids[2]{};jfieldID field_ids[2]{};uint32_t method_flags[2]{},field_flags[2]{};
 jobject local=nullptr,transition=nullptr;bool passed=false;
};
bool EntrySource::Bind(HookEngine&hooks,Binding b,const modern_field_metadata::FieldMetadata&m,const thread_suspend_service::Service&s,std::string*e){
 if(bound_||!m.Calibrated()||!b.owner||!b.vm||!b.current||!b.strong_oracle||!b.with||!b.bounds||!b.jni_id_manager)return Fail(e,"Receiver entry source lacks exact independently calibrated owners");
 auto d=reinterpret_cast<Decode>(hooks.FindSymbol("libart.so",abi::arm64::android11::kDecodeJObject));
 // Modern source inlines locals/transition slots. Presence is only a source
 // candidate; actual public local identity must independently match below.
 const bool inline_local=!d;
 if(!d)d=reinterpret_cast<Decode>(hooks.FindSymbol("libart.so",abi::arm64::android14::kDecodeGlobalJObject));
 auto root=reinterpret_cast<Root>(hooks.FindSymbol("libart.so","artReadBarrierForRootSlow"));
 auto raise=reinterpret_cast<Throw>(hooks.FindSymbol("libart.so","_ZN3art6Thread17ThrowNewExceptionEPKcS2_"));
 auto field=reinterpret_cast<abi::common::DecodeFieldId>(hooks.FindSymbol("libart.so",abi::common::kDecodeFieldId));
 auto method=reinterpret_cast<abi::common::DecodeMethodId>(hooks.FindSymbol("libart.so",abi::common::kDecodeMethodId));
 if(!d||!root||!raise||!field||!method)return Fail(e,"Receiver entry source fixed named object/root/member/exception leaves missing");
 binding_=b;states_=s;metadata_=&m;inline_local_=inline_local;decode_=d;root_=root;raise_=raise;field_id_=field;method_id_=method;bound_=true;return true;
}
EntrySource::InvocationScope::InvocationScope(EntrySource&s,const runnable_jni::Context&c,modern_field_metadata::ReadContext b)noexcept:source_(s),context_(c),bounds_(b){
 if(invocation_||!s.bound_||!c.self||!c.env||c.vm!=s.binding_.vm||!b.owner||!b.extent||s.binding_.current()!=c.self)return;
 size_t n=0;uint32_t state=UINT32_MAX;
 if(!b.extent(b.owner,c.self,&n)||!s.states_.State(c.self,n,&state)||state!=s.states_.RunnableState()||s.binding_.current()!=c.self)return;
 invocation_=this;valid_=true;
}
EntrySource::InvocationScope::~InvocationScope(){if(valid_){if(invocation_!=this)std::abort();invocation_=nullptr;}}
bool EntrySource::Borrow(void*self)const noexcept{
 auto*s=invocation_;if(!bound_||!s||!s->valid_||&s->source_!=this||s->context_.self!=self||binding_.current()!=self)return false;
 size_t n=0;uint32_t state=UINT32_MAX;return s->bounds_.extent(s->bounds_.owner,self,&n)&&states_.State(self,n,&state)&&state==states_.RunnableState()&&binding_.current()==self;
}
bool EntrySource::Extent(const void*p,size_t required)const noexcept{
 auto*s=invocation_;size_t n=0;return s&&&s->source_==this&&p&&uintptr_t(p)%4==0&&uintptr_t(p)<=UINTPTR_MAX-required&&s->bounds_.extent(s->bounds_.owner,p,&n)&&n>=required;
}
bool EntrySource::Current(void*p,void*self)noexcept{auto&s=*static_cast<EntrySource*>(p);return s.Proven()&&s.Borrow(self);}
bool EntrySource::Member(void*p,void*member,bool field,uint32_t*flags,void**declaring)noexcept{
 auto&s=*static_cast<EntrySource*>(p);auto*scope=invocation_;
 if(!s.Proven()||!scope||!flags||!declaring||!s.Borrow(scope->context_.self)||!s.Extent(member,field?sizeof(field_array::Field):8))return false;
 uint32_t f=field?field_array::Flags(member):method_identity_reader::ReadAccessFlags(member);
 void*c=field?s.metadata_->FieldDeclaringClass(member):s.metadata_->MethodDeclaringClass(member);
 if(!c||!s.Extent(c,8)||!s.Borrow(scope->context_.self))return false;*flags=f;*declaring=c;return true;
}
bool EntrySource::ObjectClass(void*p,void*object,void**out)noexcept{
 auto&s=*static_cast<EntrySource*>(p);auto*scope=invocation_;
 if(!s.Proven()||!scope||!out||!s.Borrow(scope->context_.self)||!s.Extent(object,8))return false;
 void*c=s.metadata_->Reference(object,0);if(!c||!s.Extent(c,8)||!s.Borrow(scope->context_.self))return false;*out=c;return true;
}
bool EntrySource::ReadLocalSlot(void*self,jobject reference,void**out)const noexcept{
 if(!out||!reference||!Borrow(self))return false;
 // Fixed Android14--17 thread-inl.h Thread::DecodeJObject: local and JNI
 // transition handles name a CompressedReference/GcRoot after clearing kind.
 // This is a live original argument/root slot, not an inferred IRT index/table.
 auto*slot=reinterpret_cast<void*>(uintptr_t(reference)&~kKindMask);
 if(!Extent(slot,sizeof(uint32_t)))return false;
 uint32_t compressed=0;std::memcpy(&compressed,slot,sizeof(compressed));
 // Reject cleared/malformed/check-JNI marker storage before the barrier leaf.
 // Original CheckJNI retains reference serial/ownership validation; no local
 // table metadata or Thread/JNIEnv offsets are invented by this reader.
 if(!compressed||!Extent(reinterpret_cast<void*>(uintptr_t(compressed)),8)||!Borrow(self))return false;
 void*value=root_(slot);if(!value||!Extent(value,8)||!Borrow(self))return false;
 *out=value;return true;
}
bool EntrySource::ReadReference(void*self,jobject reference,void**out)const noexcept{
 if(!out||!Borrow(self))return false;if(!reference){*out=nullptr;return true;}
 auto kind=uintptr_t(reference)&kKindMask;if(kind==kWeak||(kind==kTransition&&!transition_))return false;
 if(inline_local_&&kind!=kGlobal)return ReadLocalSlot(self,reference,out);
 // Original strong local/global/transition arguments remain live in this
 // ingress. The modern global-only named leaf never receives a local handle.
 auto result=decode_(self,reference);if(!Borrow(self)||!result.reference)return false;
 void*value=reinterpret_cast<void*>(result.reference);if(!Extent(value,8))return false;*out=value;return true;
}
bool EntrySource::Reference(void*p,void*self,void*r,void**out)noexcept{auto&s=*static_cast<EntrySource*>(p);return s.Proven()&&s.ReadReference(self,static_cast<jobject>(r),out);}
bool EntrySource::Compressed(void*p,void*self,uint32_t value,void**out)noexcept{
 auto&s=*static_cast<EntrySource*>(p);if(!s.Proven()||!out||!s.Borrow(self))return false;if(!value){*out=nullptr;return true;}
 void*object=s.root_(&value);if(!object||!s.Extent(object,8)||!s.Borrow(self))return false;*out=object;return true;
}
bool EntrySource::FieldId(void*p,void*self,void*id,void**out)noexcept{
 auto&s=*static_cast<EntrySource*>(p);if(!s.Proven()||!out||!id||!s.Borrow(self))return false;
 // Fixed DecodeGenericId: odd indices acquire jni_id_lock_. No waiting decoder
 // is called with a borrowed receiver. Even IDs select the pointer branch in
 // both pointer and indices runtime modes. Alignment/bounds are checked first.
 if(uintptr_t(id)&1u||!s.Extent(id,sizeof(field_array::Field)))return false;
 void*f=s.field_id_(s.binding_.jni_id_manager,reinterpret_cast<jfieldID>(id));
 if(f!=id||!s.Borrow(self))return false;*out=f;return true;
}
void EntrySource::Raise(void*p,void*self,const char*type,const char*message)noexcept{
 auto&s=*static_cast<EntrySource*>(p);if(!s.Proven()||!s.Borrow(self)||!type||!message)std::abort();
 // Terminal original ART exception path may allocate/suspend. No borrowed
 // value is accessed afterward; it is not a read permission/barrier callback.
 s.raise_(self,type,message);
}
bool EntrySource::OriginalScopeCurrent(const abi::common::RunnableJniScope&actual)const noexcept{
 // Both fixed Thread declarations place flags at the first uint32_t. That
 // minimum extent comes from the actual original Thread argument, not maps.
 uint32_t state=UINT32_MAX;
 return Proven()&&actual.self&&actual.env&&actual.vm==binding_.vm&&
   binding_.current()==actual.self&&states_.State(actual.self,sizeof(uint32_t),&state)&&
   state==states_.RunnableState()&&binding_.current()==actual.self;
}
bool EntrySource::SeedOriginalRoots(const abi::common::RunnableJniScope&actual,void*method,
 jobject receiver,OriginalRoots*out)const noexcept{return SeedRoots(actual,method,false,receiver,out);}
bool EntrySource::SeedOriginalFieldRoots(const abi::common::RunnableJniScope&actual,void*field,
 jobject receiver,OriginalRoots*out)const noexcept{return SeedRoots(actual,field,true,receiver,out);}
bool EntrySource::DecodeOriginalFieldId(const abi::common::RunnableJniScope&actual,jfieldID id,void**out)const noexcept{
 if(!out)return false;*out=nullptr;if(invocation_||!id||!OriginalScopeCurrent(actual))return false;
 // No receiver has been decoded/borrowed yet. The actual named indexed decoder
 // may acquire jni_id_lock_; the real original ART scope remains its owner.
 void*field=field_id_(binding_.jni_id_manager,id);
 if(!field||uintptr_t(field)%4||!OriginalScopeCurrent(actual))return false;
 *out=field;return true;
}
bool EntrySource::SeedRoots(const abi::common::RunnableJniScope&actual,void*method,bool field,
 jobject receiver,OriginalRoots*out)const noexcept{
 if(!out)return false;*out={};
 if(!method||uintptr_t(method)%4||!OriginalScopeCurrent(actual))return false;
 // The original member is an already decoded live ArtMethod/ArtField. Indexed
 // JNI IDs never reach a pointer read here. The member owns its declaring root.
 OriginalRoots roots;roots.declaring=field?metadata_->FieldDeclaringClass(method):metadata_->MethodDeclaringClass(method);
 if(!roots.declaring||!OriginalScopeCurrent(actual))return false;
 if(receiver){
  const auto kind=uintptr_t(receiver)&kKindMask;
  if(kind==kWeak||(kind==kTransition&&!transition_))return false;
  if(inline_local_&&kind!=kGlobal){
   roots.local_slot=reinterpret_cast<void*>(uintptr_t(receiver)&~kKindMask);
   if(!roots.local_slot||uintptr_t(roots.local_slot)%4)return false;
   uint32_t reference=0;std::memcpy(&reference,roots.local_slot,4);
   // CheckJNI/ownership validation belongs to the actual original JNI ingress.
   // This source-selected root slot is never manufactured from an IRT index.
   if(!reference||reference%4)return false;
   roots.receiver=root_(roots.local_slot);
  }else roots.receiver=reinterpret_cast<void*>(decode_(actual.self,receiver).reference);
  if(!roots.receiver||uintptr_t(roots.receiver)%4||!OriginalScopeCurrent(actual))return false;
 }
 *out=roots;return OriginalScopeCurrent(actual);
}
Access EntrySource::Leaves()noexcept{Access a;a.owner=this;a.current=Current;a.member=Member;a.object_class=ObjectClass;a.reference=Reference;a.compressed=Compressed;a.field_id=FieldId;a.raise=Raise;return a;}
bool EntrySource::Observe(const runnable_jni::Context&c,void*raw){
 auto&p=*static_cast<Proof*>(raw);auto&s=*p.source;
 if(c.env!=p.env||c.vm!=s.binding_.vm||s.binding_.current()!=c.self)return false;
 // The actual named current Thread supplies its calibrated fixed state prefix;
 // validate Runnable before a waiting decoder without capturing a read view.
 uint32_t state=UINT32_MAX;
 if(!c.self||!s.states_.State(c.self,sizeof(uint32_t),&state)||state!=s.states_.RunnableState())return false;
 // Decode IDs before capturing the read window or borrowing managed pointers.
 // Indexed IDs may acquire the original JNI manager lock; they are not
 // permanent leaves.
 void*members[2][2]{};
 for(size_t i=0;i<2;++i){members[i][0]=s.method_id_(s.binding_.jni_id_manager,p.method_ids[i]);members[i][1]=s.field_id_(s.binding_.jni_id_manager,p.field_ids[i]);}
 if(c.env!=p.env||c.vm!=s.binding_.vm||s.binding_.current()!=c.self)return false;
 InvocationScope scope(s,c,s.binding_.bounds(s.binding_.owner,c));if(!scope.Valid())return false;
 if(!s.Borrow(c.self))return false;
 void*objects[2]{};void*classes[2]{};
 for(size_t i=0;i<2;++i){
  if(!s.binding_.strong_oracle(s.binding_.owner,c,p.receivers[i],&objects[i])||!objects[i]||!s.binding_.strong_oracle(s.binding_.owner,c,p.object_classes[i],&classes[i])||!classes[i]||!s.Extent(objects[i],8)||!s.Extent(classes[i],8)||s.metadata_->Reference(objects[i],0)!=classes[i])return false;
  void*decoded=nullptr;if(!s.ReadReference(c.self,p.receivers[i],&decoded)||decoded!=objects[i])return false;
  if(uintptr_t(objects[i])>UINT32_MAX)return false;uint32_t compressed=uint32_t(uintptr_t(objects[i]));if(s.root_(&compressed)!=objects[i])return false;
  for(bool field:{false,true}){
   void*expected=nullptr;if(!s.binding_.strong_oracle(s.binding_.owner,c,field?p.field_classes[i]:p.method_classes[i],&expected)||!expected)return false;
   void*member=members[i][field?1:0];
   if(!s.Extent(member,field?sizeof(field_array::Field):8))return false;
   auto flags=field?field_array::Flags(member):method_identity_reader::ReadAccessFlags(member);
   if((flags&kPublicFlags)!=(field?p.field_flags[i]:p.method_flags[i])||bool(flags&8u)!=bool(i))return false;
   void*declaring=field?s.metadata_->FieldDeclaringClass(member):s.metadata_->MethodDeclaringClass(member);
   if(declaring!=expected||!s.Borrow(c.self))return false;
  }
 }
 if(objects[0]==objects[1]||classes[0]==classes[1])return false;
 void*local=nullptr;if(!s.ReadReference(c.self,p.local,&local)||local!=objects[0])return false;
 if(p.transition){void*result=nullptr;if(s.inline_local_){if(!s.ReadLocalSlot(c.self,p.transition,&result))return false;}else result=reinterpret_cast<void*>(s.decode_(c.self,p.transition).reference);if(result!=objects[0]||!s.Borrow(c.self))return false;}
 p.passed=true;return true;
}
bool EntrySource::Calibrate(JNIEnv*env,const Samples&samples,std::string*e){
 if(!bound_||Proven()||!env||env->ExceptionCheck())return Fail(e,"Receiver entry calibration requires fresh ordinary JNI owner");
 JavaVM*vm=nullptr;if(env->GetJavaVM(&vm)!=JNI_OK||vm!=binding_.vm)return Fail(e,"Receiver entry calibration VM differs");
 Roots roots{env};Proof proof{this,env};
 for(size_t i=0;i<2;++i){
  for(auto r:{samples.methods[i],samples.fields[i],samples.receivers[i]})if(!r||env->GetObjectRefType(r)!=JNIGlobalRefType||env->ExceptionCheck())return Fail(e,"Receiver entry public samples must be retained strong globals");
  proof.methods[i]=samples.methods[i];proof.fields[i]=samples.fields[i];proof.receivers[i]=samples.receivers[i];
  if((uintptr_t(samples.receivers[i])&kKindMask)!=kGlobal)return Fail(e,"Public global reference kind differs from fixed ART source");
  for(bool field:{false,true}){
   auto reflected=field?samples.fields[i]:samples.methods[i];Local type{env,env->GetObjectClass(reflected)};if(!type.value||env->ExceptionCheck())return false;
   auto mods=env->GetMethodID(static_cast<jclass>(type.value),"getModifiers","()I");if(!mods||env->ExceptionCheck())return false;
   auto declaring=env->GetMethodID(static_cast<jclass>(type.value),"getDeclaringClass","()Ljava/lang/Class;");if(!declaring||env->ExceptionCheck())return false;
   jint flags=env->CallIntMethod(reflected,mods);if(env->ExceptionCheck()||flags<0||bool(flags&8)!=bool(i))return false;
   Local klass{env,env->CallObjectMethod(reflected,declaring)};if(!klass.value||env->ExceptionCheck())return false;
   auto root=roots.Add(klass.value);if(!root)return false;
   if(field){proof.field_classes[i]=root;proof.field_flags[i]=uint32_t(flags);proof.field_ids[i]=env->FromReflectedField(reflected);if(!proof.field_ids[i]||env->ExceptionCheck())return false;}
   else{proof.method_classes[i]=root;proof.method_flags[i]=uint32_t(flags);proof.method_ids[i]=env->FromReflectedMethod(reflected);if(!proof.method_ids[i]||env->ExceptionCheck())return false;}
  }
  Local type{env,env->GetObjectClass(samples.receivers[i])};if(!type.value||env->ExceptionCheck())return false;proof.object_classes[i]=roots.Add(type.value);if(!proof.object_classes[i])return false;
 }
 Local local{env,env->NewLocalRef(samples.receivers[0])};if(!local.value||env->ExceptionCheck()||env->GetObjectRefType(local.value)!=JNILocalRefType||env->ExceptionCheck()||!env->IsSameObject(local.value,samples.receivers[0])||env->ExceptionCheck()||(uintptr_t(local.value)&kKindMask)!=kLocal)return Fail(e,"Public local reference kind/identity differs from fixed ART source");proof.local=local.value;
 if(samples.transition_reference){auto t=samples.transition_reference;if((uintptr_t(t)&kKindMask)!=kTransition||env->GetObjectRefType(t)!=JNILocalRefType||env->ExceptionCheck()||!env->IsSameObject(t,samples.receivers[0])||env->ExceptionCheck())return Fail(e,"Original JNI transition reference has no independent public local identity");proof.transition=t;}
 // Native ordinary before/after + actual same-current Runnable inside bridge
 // independently match the supplied Thread declaration; no SDK selects it.
 void*self=binding_.current();
 const auto native_state=[&](){
  runnable_jni::Context actual{binding_.current(),env,binding_.vm};
  if(!self||actual.self!=self)return false;
  auto bounds=binding_.bounds(binding_.owner,actual);size_t n=0;uint32_t state=UINT32_MAX;
  return bounds.owner&&bounds.extent&&bounds.extent(bounds.owner,actual.self,&n)&&
   states_.State(actual.self,n,&state)&&state==abi::arm64::android11::kNative&&binding_.current()==self;
 };
 if(!native_state())return Fail(e,"Receiver entry calibration lacks original Native Thread declaration");
 if(!binding_.with(binding_.owner,env,samples.methods[0],Observe,&proof)||!proof.passed||
    env->ExceptionCheck()||!native_state())
  return Fail(e,"Receiver entry independent native/public Runnable source oracle failed");
 transition_=proof.transition!=nullptr;ready_.store(true,std::memory_order_release);return true;
}
}
