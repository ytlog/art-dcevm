#include "platform/common/native_lazy_field_mirror.h"
namespace artdcevm::native_lazy_field_mirror {
namespace {
constexpr const char* kFactory="_ZN3art6mirror5Field18CreateFromArtFieldEPNS_6ThreadEPNS_8ArtFieldEb";
constexpr const char* kFour="_ZN3art6mirror5Field18CreateFromArtFieldEPNS_6ThreadENS_6HandleINS0_5ClassEEEPNS_8ArtFieldEb";
constexpr const char* kLegacy="_ZN3art6mirror5Field18CreateFromArtFieldILNS_11PointerSizeE8ELb0EEENS_6ObjPtrIS1_EEPNS_6ThreadEPNS_8ArtFieldEb";
constexpr const char* kLocal="_ZN3art9JNIEnvExt11NewLocalRefEPNS_6mirror6ObjectE";
bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
struct LocalRoot {JNIEnv* env;jobject value;~LocalRoot(){if(value)env->DeleteLocalRef(value);}};
struct GlobalRoot {JNIEnv* env;jobject value;~GlobalRoot(){if(value)env->DeleteGlobalRef(value);}};
}
struct Service::Request {Service* service;JNIEnv* env;jobject owner;jfieldID id;void* expected;jobject output=nullptr;};
bool Service::Bind(HookEngine& hooks,const Bindings& b,std::string* error){
 if(bound_||!b.owner||!b.matched_release_carrier||!b.scope||!b.oracle||!b.decode||!b.decode_field||!b.field||!b.declaring)return Fail(error,"Lazy Field factory lacks independently matched original codec/root owner");
 four_=false;
 auto factory=hooks.FindSymbol("libart.so",kFactory);
 if(!factory)factory=hooks.FindSymbol("libart.so",kLegacy);
 auto local=hooks.FindSymbol("libart.so",kLocal);
 bindings_=b;
 if(!factory){factory=hooks.FindSymbol("libart.so",kFour);four_=factory!=nullptr;if(four_&&!FourProven())return Fail(error,"Extra-Handle lazy factory lacks original public episodes and calibrated live roots");}
 if(!factory||!local)return Fail(error,"Source-declared lazy Field factory/local-reference leaf unavailable");
 bindings_=b;factory_=factory;local_=reinterpret_cast<Local>(local);bound_=true;return true;
}
bool Service::FourProven()const{
 const auto&b=bindings_;
 if(!b.observer||!b.observer->FourArgument()||!b.observer->NormalizerInstalled()||b.observer->Poisoned()||!b.roots||!b.roots->Calibrated()||!b.public_roots||!b.primitive_trace||!b.optional_trace)return false;
 for(bool proven:*b.public_roots)if(!proven)return false;
 for(auto*t:{b.primitive_trace,b.optional_trace})for(const auto&e:t->entries){
  if(!e.field||!e.calls||e.false_calls!=e.calls||!e.root_before_valid||!e.root_after_valid||!e.result||!e.root_after||e.root_before!=e.root_after||e.normalized_field!=e.field)return false;
 }
 return true;
}
bool Service::Produce(const runnable_jni::Context& context,void* raw){
 auto& r=*static_cast<Request*>(raw);auto& s=*r.service;auto& b=s.bindings_;
 if(!context.self||context.env!=r.env||!context.env||context.env->ExceptionCheck())return false;
 void *oracle=nullptr,*owner=nullptr;
 if(!b.oracle(b.owner,context,r.owner,&oracle)||context.env->ExceptionCheck()||!b.decode(b.owner,r.owner,&owner)||!owner||owner!=oracle)return false;
 void* field=b.decode_field(b.owner,context,owner,r.id);
 if(!field||(r.expected&&field!=r.expected)||b.declaring(b.owner,field)!=owner)return false;
 // ART may resolve a type and suspend here. Exact declaring Class is strongly
 // rooted; no borrowed managed identity is reused after the original factory.
 uintptr_t result=0;
 std::unique_ptr<source_handle_roots::Scope> handle;
 if(s.four_){
  if(!s.FourProven()||!s.field_class_)return false;
  void* field_class=nullptr;
  if(!b.roots->BorrowOwnedStrongGlobal(context,s.field_class_,&field_class)||!field_class)return false;
  handle=b.roots->CreateScope(1,nullptr);if(!handle)return false;
  handle->Set(0,field_class);
  using Four=uintptr_t(*)(void*,native_field_factory_observer::HandleCarrier,void*,bool);
  result=reinterpret_cast<Four>(s.factory_)(context.self,{handle->Slot(0)},field,false);
 }else result=reinterpret_cast<Factory>(s.factory_)(context.self,field,false);
 if(context.env->ExceptionCheck()||!result)return false;
 jobject local=s.local_(context.env,reinterpret_cast<void*>(result));LocalRoot guard{context.env,local};
 if(context.env->ExceptionCheck()||!local)return false;
 jobject strong=context.env->NewGlobalRef(local);GlobalRoot root{context.env,strong};
 if(context.env->ExceptionCheck()||!strong)return false;
 void *mirror_oracle=nullptr,*mirror=nullptr,*fresh_owner=nullptr;
 if(!b.oracle(b.owner,context,strong,&mirror_oracle)||context.env->ExceptionCheck()||!b.decode(b.owner,strong,&mirror)||mirror!=mirror_oracle||!mirror||
    !b.decode(b.owner,r.owner,&fresh_owner)||!fresh_owner||b.field(b.owner,mirror)!=field||b.declaring(b.owner,field)!=fresh_owner||
    b.decode_field(b.owner,context,fresh_owner,r.id)!=field)return false;
 r.output=strong;root.value=nullptr;return true;
}
bool Service::ProducePublic(JNIEnv* env,jclass owner,jfieldID id,bool statik,void* expected,jobject* output,std::string* error){
 if(!env||!owner||!id||!output||env->ExceptionCheck())return Fail(error,"Lazy Field request invalid or entering with pending exception");
 *output=nullptr;GlobalRoot exact{env,env->NewGlobalRef(owner)};
 if(!exact.value||env->ExceptionCheck())return Fail(error,"Lazy Field declaring Class strong root unavailable");
 Request request{this,env,exact.value,id,expected};
 const bool produced=bindings_.scope(bindings_.owner,env,carrier_,Produce,&request);
 GlobalRoot mirror{env,request.output};
 if(!produced||env->ExceptionCheck()||!mirror.value)return Fail(error,"Original lazy Field factory/codec identity proof failed");
 if(env->FromReflectedField(mirror.value)!=id||env->ExceptionCheck())return Fail(error,"Lazy Field original opaque ID round trip differs");
 LocalRoot type{env,env->GetObjectClass(mirror.value)};
 if(!type.value||env->ExceptionCheck())return Fail(error,"Lazy Field public mirror Class unavailable");
 auto declaring=env->GetMethodID(static_cast<jclass>(type.value),"getDeclaringClass","()Ljava/lang/Class;");
 if(!declaring||env->ExceptionCheck())return Fail(error,"Lazy Field public declaring accessor unavailable");
 auto modifiers=env->GetMethodID(static_cast<jclass>(type.value),"getModifiers","()I");
 if(!modifiers||env->ExceptionCheck())return Fail(error,"Lazy Field public modifier accessor unavailable");
 LocalRoot declared{env,env->CallObjectMethod(mirror.value,declaring)};
 if(!declared.value||env->ExceptionCheck())return Fail(error,"Lazy Field public declaring Class unavailable");
 const bool same=env->IsSameObject(declared.value,owner);
 if(!same||env->ExceptionCheck())return Fail(error,"Lazy Field public declaring namespace differs");
 const jint flags=env->CallIntMethod(mirror.value,modifiers);
 if(env->ExceptionCheck()||((flags&8)!=0)!=statik)return Fail(error,"Lazy Field public storage differs");
 *output=mirror.value;mirror.value=nullptr;return true;
}
bool Service::Calibrate(JNIEnv* env,const std::vector<Oracle>& oracles,std::string* error){
 if(!bound_||calibrated_||carrier_||!env||env->ExceptionCheck()||oracles.size()<2)return Fail(error,"Lazy Field needs two independent original public oracles");
 for(size_t i=0;i<oracles.size();++i){const auto& o=oracles[i];
  if(!o.strong_field||!o.exact_owner||!o.id||!o.native_field)return Fail(error,"Lazy Field independent oracle incomplete");
  for(size_t j=0;j<i;++j)if(oracles[j].native_field==o.native_field)return Fail(error,"Lazy Field oracle targets must be distinct");
 }
 if(four_){
  if(!FourProven())return Fail(error,"Original extra-Handle public proof is no longer live");
  LocalRoot type{env,env->GetObjectClass(oracles.front().strong_field)};
  if(!type.value||env->ExceptionCheck())return false;
  field_class_=env->NewGlobalRef(type.value);
  if(!field_class_||env->ExceptionCheck()){Clear(env);return false;}
  for(const auto&o:oracles){LocalRoot other{env,env->GetObjectClass(o.strong_field)};if(!other.value||env->ExceptionCheck()||!env->IsSameObject(other.value,field_class_)||env->ExceptionCheck()){Clear(env);return Fail(error,"Original Field factory public mirror classes differ");}}
 }
 carrier_=env->NewGlobalRef(oracles.front().strong_field);
 if(!carrier_||env->ExceptionCheck()){Clear(env);return false;}
 for(const auto& o:oracles){
  if(env->FromReflectedField(o.strong_field)!=o.id||env->ExceptionCheck()){Clear(env);return Fail(error,"Lazy Field public calibration ID differs");}
  // The callback compares the ORIGINAL public Field's named leaf and codec,
  // independently of the newly produced mirror.
  struct Proof {Service* service;JNIEnv* env;const Oracle* oracle;};Proof proof{this,env,&o};
  const auto verify=[](const runnable_jni::Context& c,void* raw){auto& p=*static_cast<Proof*>(raw);auto& b=p.service->bindings_;if(c.env!=p.env||!c.self||!c.env||c.env->ExceptionCheck())return false;void *mirror=nullptr,*actual=nullptr,*owner=nullptr;
   if(!b.oracle(b.owner,c,p.oracle->strong_field,&actual)||c.env->ExceptionCheck()||!b.decode(b.owner,p.oracle->strong_field,&mirror)||mirror!=actual||!b.decode(b.owner,p.oracle->exact_owner,&owner))return false;
   return b.field(b.owner,mirror)==p.oracle->native_field&&b.declaring(b.owner,p.oracle->native_field)==owner&&b.decode_field(b.owner,c,owner,p.oracle->id)==p.oracle->native_field;
  };
  if(!bindings_.scope(bindings_.owner,env,carrier_,verify,&proof)||env->ExceptionCheck()){Clear(env);return Fail(error,"Original public Field/codec calibration identity differs");}
  jobject generated=nullptr;
  if(!ProducePublic(env,o.exact_owner,o.id,o.is_static,o.native_field,&generated,error)){Clear(env);return false;}
  env->DeleteGlobalRef(generated);
 }
 calibrated_=true;return true;
}
bool Service::Create(JNIEnv* env,jclass owner,jfieldID id,bool statik,jobject* output,std::string* error){
 if(!calibrated_)return Fail(error,"Lazy Field factory has no actual public/native calibration");
 return ProducePublic(env,owner,id,statik,nullptr,output,error);
}
void Service::Clear(JNIEnv* env){if(field_class_&&env)env->DeleteGlobalRef(field_class_);field_class_=nullptr;if(carrier_&&env)env->DeleteGlobalRef(carrier_);carrier_=nullptr;calibrated_=false;}
}
