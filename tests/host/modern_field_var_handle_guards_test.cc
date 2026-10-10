#include "platform/common/contextual_field_id.cc"
#include "platform/common/modern_field_guards.cc"
#include <cassert>
#include <cstdarg>
#include <iostream>
namespace art {class ReflectionSourceInfo {public:virtual ~ReflectionSourceInfo()=default;uint32_t kind=4,pad=0;void* object=nullptr;};}
using namespace artdcevm;
namespace {
struct Field {reflection_metadata::Field12 mirror{};int flags=0;void* target=nullptr;jobject owner=nullptr;jfieldID id=nullptr;};
struct Handle {var_handle_field::Prefix prefix{};void* target=nullptr;int coordinates=0;jobject coordinate=nullptr;};
int base_class,field_class,list_class,owner_a,owner_b,current_thread,wrong_thread,int_class,static_handle_class,other_handle_class,oracle_class;
abi::arm64::Field field_a{},field_b{},field_c{},field_d{};
struct Other {uint32_t klass=0x3000,monitor=0;} other;
uintptr_t instance_native_class=0x1000,static_native_class=0x2000,other_native_class=0x3000;
int roots_created=0,roots_deleted=0,array_verifications=0;bool shared_class=false,in_hot=false,decode_failure=false,field_class_failure=false;
Field instance{},statik{},next_instance{},next_static{};Handle first{},second{},third{},fourth{};JNIEnv env{};JNINativeInterface_ table{};
bool pending=false,stopped=false,modifier_throws=false,named=true,wrong_context=false,original_result=true,change_thread=false;void* actual_thread=&current_thread;int thrown=0;int installs=0,static_lookups=0,original_calls=0;uint32_t source_kind=4;
retirement::RetiredFieldRegistry registry;
jobject Obj(void* p){return reinterpret_cast<jobject>(p);}jclass Cls(void* p){return reinterpret_cast<jclass>(p);}
jboolean Exception(JNIEnv*){assert(!in_hot);return pending;}
jclass Find(JNIEnv*,const char* name){std::string s=name;if(s=="java/lang/invoke/StaticFieldVarHandle"){++static_lookups;pending=true;return nullptr;}
 if(s=="java/lang/Class")return Cls(&int_class);
 if(s=="com/github/ytlog/artdcevm/internal/VarHandleClassOracle")return Cls(&oracle_class);
 if(s=="java/lang/invoke/FieldVarHandle")return Cls(&base_class);if(s=="java/lang/reflect/Field")return Cls(&field_class);if(s=="java/util/List")return Cls(&list_class);assert(false);return nullptr;}
void Delete(JNIEnv*,jobject){}
void DeleteRoot(JNIEnv*,jobject){assert(!in_hot);++roots_deleted;}
jboolean IsInstance(JNIEnv*,jobject o,jclass c){assert(!in_hot);if(c==Cls(&base_class))return o==Obj(&first)||o==Obj(&second)||o==Obj(&third)||o==Obj(&fourth);if(c==Cls(&field_class))return o==Obj(&instance)||o==Obj(&statik)||o==Obj(&next_instance)||o==Obj(&next_static);if(c==Cls(&list_class))return o==Obj(&first)||o==Obj(&second)||o==Obj(&third)||o==Obj(&fourth);return false;}
jboolean Same(JNIEnv*,jobject a,jobject b){assert(!in_hot);return a==b;}
jmethodID Method(JNIEnv*,jclass,const char* name,const char*){const std::string s=name;int id=s=="coordinateTypes"?1:s=="size"?2:s=="get"?3:s=="getModifiers"?4:s=="getDeclaringClass"?5:s=="getName"?6:s=="getType"?7:s=="equals"?8:0;assert(id);return reinterpret_cast<jmethodID>(uintptr_t(id));}
jint Int(JNIEnv*,jobject o,jmethodID id,va_list){assert(!pending);if(uintptr_t(id)==2)return reinterpret_cast<Handle*>(o)->coordinates;assert(uintptr_t(id)==4);if(modifier_throws)pending=true;return reinterpret_cast<Field*>(o)->flags;}
jobject Object(JNIEnv*,jobject o,jmethodID id,va_list args){assert(!pending);if(uintptr_t(id)==1)return o;if(uintptr_t(id)==3){assert(va_arg(args,int)==0);return reinterpret_cast<Handle*>(o)->coordinate;}if(uintptr_t(id)==6){const char* name=o==Obj(&int_class)?"int":(o==Obj(&statik)||o==Obj(&next_static))?"s":"i";return reinterpret_cast<jobject>(const_cast<char*>(name));}
 if(uintptr_t(id)==7)return Obj(&int_class);assert(uintptr_t(id)==5);return reinterpret_cast<Field*>(o)->owner;}
jfieldID From(JNIEnv*,jobject o){return reinterpret_cast<Field*>(o)->id;}
void* NativeField(void* o){return static_cast<Field*>(o)->target;}
jclass ObjectClass(JNIEnv*,jobject object){assert(!in_hot);
 if(object==Obj(&first)||object==Obj(&third))return Cls(&base_class);
 if(object==Obj(&second)||object==Obj(&fourth))return Cls(shared_class?&base_class:&static_handle_class);
 if(object==Obj(&other))return Cls(&other_handle_class);
 return Cls(&field_class);
}
jboolean Bool(JNIEnv*,jobject object,jmethodID id,va_list args){assert(uintptr_t(id)==8);return object==va_arg(args,jobject);}
const char* Utf(JNIEnv*,jstring value,jboolean*){return reinterpret_cast<const char*>(value);}
void ReleaseUtf(JNIEnv*,jstring,const char*){}
jobject NewRef(JNIEnv*,jobject value){assert(!in_hot);++roots_created;return value;}
jweak NewWeak(JNIEnv*,jobject value){return value;}
Field* PublicField(jclass owner,bool stat){return owner==Cls(&owner_a)?(stat?&statik:&instance):(stat?&next_static:&next_instance);}
jfieldID PublicId(JNIEnv*,jclass owner,const char* name,const char* sig){assert(std::string(name)=="i"&&std::string(sig)=="I");return reinterpret_cast<jfieldID>(owner==Cls(&owner_a)?&field_a:&field_c);}
jfieldID PublicStaticId(JNIEnv*,jclass owner,const char* name,const char* sig){assert(std::string(name)=="s"&&std::string(sig)=="I");return reinterpret_cast<jfieldID>(owner==Cls(&owner_a)?&field_b:&field_d);}
jobject PublicMirror(JNIEnv*,jclass owner,jfieldID id,jboolean stat){auto* f=PublicField(owner,stat);return f->id==id?Obj(f):nullptr;}

void* Current(){return actual_thread;}
void ThrowRetired(void* self,const char* type,const char*){assert(self==&current_thread);assert(std::string(type)=="Ljava/lang/NoSuchFieldError;"||std::string(type)=="Ljava/lang/IllegalStateException;"||std::string(type)=="Ljava/lang/UnsupportedOperationException;");++thrown;}
abi::arm64::android11::ReleaseObjectPointer DecodeClass(const void* self,jobject object){
 assert(self==Current());
 uintptr_t value=object==Obj(&base_class)?instance_native_class:object==Obj(&static_handle_class)?static_native_class:object==Obj(&other_handle_class)?other_native_class:0;
 return {field_class_failure?0:decode_failure?value+4:value};
}
void* Mark(void* raw){uintptr_t value=reinterpret_cast<uintptr_t>(raw);
 if(value==0x1000)return reinterpret_cast<void*>(instance_native_class);
 if(value==0x2000)return reinterpret_cast<void*>(static_native_class);
 if(value==0x3000)return reinterpret_cast<void*>(other_native_class);return raw;
}
const char* Descriptor(void*,void*){return "LFixture;";}
jmethodID FactoryMethod(JNIEnv*,jclass type,const char* name,const char* sig){assert(type==Cls(&oracle_class)&&std::string(name)=="create"&&std::string(sig)=="()Ljava/lang/invoke/VarHandle;");return reinterpret_cast<jmethodID>(9);}
jobject Factory(JNIEnv*,jclass type,jmethodID id,va_list){assert(type==Cls(&oracle_class)&&uintptr_t(id)==9&&!in_hot);++array_verifications;return Obj(&other);}
void Visit(void* o,art::ReflectiveValueVisitor* visitor){art::ReflectionSourceInfo source;source.kind=source_kind;source.object=o;auto* target=reinterpret_cast<art::ArtField*>(static_cast<Handle*>(o)->target);assert(visitor->VisitField(target,source)==target);}
void Stop(void*,const char*,bool){assert(!stopped);stopped=true;}void Resume(void*){assert(stopped);stopped=false;}
bool Access(void*,int mode,void* frame,const void* operands,void* result){assert(mode>=0&&mode<=30&&frame==reinterpret_cast<void*>(11)&&operands==reinterpret_cast<void*>(13));++original_calls;*static_cast<uint64_t*>(result)=42;if(change_thread)actual_thread=&wrong_thread;return original_result;}
struct Hooks:HookEngine {bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char*,const char* name)override{std::string s=name;if(s=="_ZN3art6mirror5Field11GetArtFieldEv")return reinterpret_cast<void*>(NativeField);if(s=="artReadBarrierMark")return reinterpret_cast<void*>(Mark);
 if(s=="artReadBarrierForRootSlow")return reinterpret_cast<void*>(Mark);
 if(s=="_ZN3art6mirror5Class13GetDescriptorEPNSt3__112basic_stringIcNS2_11char_traitsIcEENS2_9allocatorIcEEEE")return reinterpret_cast<void*>(Descriptor);
 if(s=="_ZNK3art6Thread19DecodeGlobalJObjectEP8_jobject")return reinterpret_cast<void*>(DecodeClass);
 if(s=="_ZN3art6mirror9VarHandle6AccessENS1_10AccessModeEPNS_11ShadowFrameEPKNS_19InstructionOperandsEPNS_6JValueE")return reinterpret_cast<void*>(Access);if(s==var_handle_field::kCurrent)return reinterpret_cast<void*>(Current);if(s==var_handle_field::kVisit)return named?reinterpret_cast<void*>(Visit):nullptr;if(s==abi::common::kSuspendConstruct)return reinterpret_cast<void*>(Stop);if(s==abi::common::kSuspendDestroy)return reinterpret_cast<void*>(Resume);return nullptr;}
 void* Install(void* target,void*,void** original,std::string*)override{assert(stopped);++installs;*original=target;return this;}bool Remove(void*,std::string*)override{return true;}};
}
namespace artdcevm::method_identity {bool ReadableMappingExtent(const void*,size_t*){return false;}}
namespace artdcevm::retirement {
bool RegisterMethodHandleFieldOwner(RetiredFieldRegistry&,std::string*){return true;}
bool MethodHandleFieldGuardsReady(const RetiredFieldRegistry&){return false;}
}
namespace artdcevm::runnable_executor {
bool Available(){return true;}
bool DecodeStrongGlobal(const runnable_jni::Context&,jobject object,void** out){
 if(object==Obj(&base_class))*out=reinterpret_cast<void*>(instance_native_class);
 else if(object==Obj(&static_handle_class))*out=reinterpret_cast<void*>(static_native_class);
 else if(object==Obj(&other_handle_class))*out=reinterpret_cast<void*>(other_native_class);
 else *out=reinterpret_cast<void*>(object);return object;
}
runnable_jni::Result With(JNIEnv* e,jobject,runnable_jni::Operation op,void* data){return op({wrong_context?&wrong_thread:Current(),e,nullptr},data)?runnable_jni::Result::kSuccess:runnable_jni::Result::kOperationRejected;}
runnable_jni::Result WithOriginalScope(JNIEnv*,jobject,runnable_jni::Operation,void*){std::abort();} // Unowned baseline never needs original ingress.

}
int main(int argc,char** argv){using namespace retirement;
 // Binding validates all named modes, not merely get/set. Preserve output
 // when an update-mode declaration disagrees; never silently disable guarding.
 var_handle_access_mode::NativeTemplate mode_callback=var_handle_access_mode::Template;
 assert(var_handle_access_mode::Bind(nullptr,&mode_callback));
 const auto previous_callback=mode_callback;
 auto bad_update=+[](int mode)->uint32_t{return mode==16?0:var_handle_access_mode::Template(mode);};
 assert(!var_handle_access_mode::Bind(bad_update,&mode_callback)&&mode_callback==previous_callback);
 auto native=+[](int mode)->uint32_t{
   // Independent source-fixture return values, including each signature family.
   constexpr uint32_t expected[]={0,1,0,1,0,1,0,1,2,3,3,3,2,2,2,2,
                                 4,4,4,4,4,4,4,4,4,4,4,4,4,4,4};
   assert(mode>=0&&mode<=30);return expected[mode];
 };
 assert(var_handle_access_mode::Bind(native,&mode_callback)&&mode_callback==native);
 assert(!var_handle_access_mode::Bind(native,nullptr));
 const std::string mode=argc>1?argv[1]:"named";
 named=mode.find("prefix")==std::string::npos;shared_class=mode.find("same-class")!=std::string::npos;
 table.ExceptionCheck=Exception;table.FindClass=Find;table.DeleteLocalRef=Delete;table.IsInstanceOf=IsInstance;table.IsSameObject=Same;table.GetMethodID=Method;table.CallIntMethodV=Int;table.CallObjectMethodV=Object;table.FromReflectedField=From;table.GetObjectClass=ObjectClass;table.CallBooleanMethodV=Bool;table.GetStringUTFChars=Utf;table.ReleaseStringUTFChars=ReleaseUtf;table.NewGlobalRef=NewRef;table.DeleteGlobalRef=DeleteRoot;table.NewWeakGlobalRef=NewWeak;table.DeleteWeakGlobalRef=Delete;table.GetFieldID=PublicId;table.GetStaticFieldID=PublicStaticId;table.ToReflectedField=PublicMirror;table.GetStaticMethodID=FactoryMethod;table.CallStaticObjectMethodV=Factory;env.functions=&table;
 instance.target=&field_a;instance.owner=Obj(&owner_a);instance.id=reinterpret_cast<jfieldID>(&field_a);statik.flags=8;statik.target=&field_b;statik.owner=Obj(&owner_a);statik.id=reinterpret_cast<jfieldID>(&field_b);
 first.prefix.klass=0x1000;second.prefix.klass=shared_class?0x1000:0x2000;third.prefix.klass=0x1000;fourth.prefix.klass=shared_class?0x1000:0x2000;
 first.target=&field_a;first.coordinates=1;first.coordinate=Obj(&owner_a);second.target=&field_b;second.coordinates=0;
 const auto instance_mirror=instance.mirror,static_mirror=statik.mirror;
 installed=true;state.registry=&registry;state.manager=&registry;state.throw_exception=ThrowRetired;original_field=reinterpret_cast<void*>(NativeField);
 Hooks hooks;std::string error;assert(contextual_field_id::Initialize(hooks,&error));std::vector<FieldVarHandleOracle> oracles{{Obj(&first),&field_a,Obj(&instance)},{Obj(&second),&field_b,Obj(&statik)}};
 auto sync=[&](){for(auto* handle:{&first,&second,&third,&fourth})handle->prefix.field=reinterpret_cast<uintptr_t>(handle->target);};
 auto prepare=[&](const std::vector<FieldVarHandleOracle>& values){sync();return InstallFieldVarHandleGuards(hooks,&env,values,registry,&error);};
 auto reject=[&](){assert(!prepare(oracles));assert(installs==0&&!stopped&&!FieldVarHandleGuardsReady(registry));};
 first.coordinates=0;reject();first.coordinates=2;reject();first.coordinates=1;
 statik.flags=0;reject();statik.flags=8;
 first.coordinate=Obj(&owner_b);reject();first.coordinate=Obj(&owner_a);
 instance.id=reinterpret_cast<jfieldID>(&field_b);reject();instance.id=reinterpret_cast<jfieldID>(&field_a);
 instance.target=&field_b;reject();instance.target=&field_a;
 if(named){source_kind=3;reject();source_kind=4;}
 decode_failure=true;reject();decode_failure=false;
 first.prefix.klass=0x2000;reject();first.prefix.klass=0x1000;
 other.klass=0x1000;reject();other.klass=0x3000;
 statik.target=&field_a;statik.id=reinterpret_cast<jfieldID>(&field_a);second.target=&field_a;oracles[1].observed_field=&field_a;reject();
 statik.target=&field_b;statik.id=reinterpret_cast<jfieldID>(&field_b);second.target=&field_b;oracles[1].observed_field=&field_b;
 oracles[0].reflected_field=nullptr;reject();oracles[0].reflected_field=Obj(&instance);
 modifier_throws=true;reject();assert(pending);pending=false;modifier_throws=false;
 if(!prepare(oracles)){std::cerr<<error<<"\n";assert(false);}assert(installs==1&&static_lookups==0&&!pending&&!FieldVarHandleGuardsReady(registry));
 auto access=[&](void* handle,int mode,void* frame,const void* operands,void* result){
  const int before_create=roots_created,before_delete=roots_deleted;in_hot=true;
  const bool passed=VarAccess(handle,mode,frame,operands,result);in_hot=false;
  assert(roots_created==before_create&&roots_deleted==before_delete);return passed;
 };
 uint64_t result=0;assert(access(&first,7,reinterpret_cast<void*>(11),reinterpret_cast<void*>(13),&result));assert(result==42&&!FieldVarHandleGuardsReady(registry));
 assert(access(&second,7,reinterpret_cast<void*>(11),reinterpret_cast<void*>(13),&result));assert(FieldVarHandleGuardsReady(registry)&&original_calls==2);
 // Reprepare a different class using the same durable registry/hook.
 next_instance.target=&field_c;next_instance.owner=Obj(&owner_b);next_instance.id=reinterpret_cast<jfieldID>(&field_c);
 next_static.flags=8;next_static.target=&field_d;next_static.owner=Obj(&owner_b);next_static.id=reinterpret_cast<jfieldID>(&field_d);
 third.target=&field_c;third.coordinates=1;third.coordinate=Obj(&owner_b);fourth.target=&field_d;fourth.coordinates=0;
 const auto next_mirror=next_instance.mirror;
 std::vector<FieldVarHandleOracle> next{{Obj(&third),&field_c,Obj(&next_instance)},{Obj(&fourth),&field_d,Obj(&next_static)}};
 const auto previous=InspectFieldVarHandleGuards(registry);const auto* previous_reader=vars.reader.get();
 auto preserve=[&](){assert(!prepare(next));const auto proof=InspectFieldVarHandleGuards(registry);
  assert(vars.reader.get()==previous_reader&&proof.observed_mask==previous.observed_mask&&proof.oracle_generation==previous.oracle_generation&&FieldVarHandleGuardsReady(registry)&&installs==1&&!stopped);};
 RetiredFieldRegistry another_registry;
 assert(!InstallFieldVarHandleGuards(hooks,&env,next,another_registry,&error));
 assert(vars.reader.get()==previous_reader&&FieldVarHandleGuardsReady(registry)&&installs==1);
 auto single=next;single.resize(1);assert(!prepare(single));
 assert(vars.reader.get()==previous_reader&&FieldVarHandleGuardsReady(registry)&&installs==1);
 third.coordinate=Obj(&owner_a);preserve();third.coordinate=Obj(&owner_b);
 next_instance.id=reinterpret_cast<jfieldID>(&field_a);preserve();next_instance.id=reinterpret_cast<jfieldID>(&field_c);
 wrong_context=true;preserve();wrong_context=false;
 assert(prepare(next)&&installs==1&&!FieldVarHandleGuardsReady(registry));
 auto fresh=InspectFieldVarHandleGuards(registry);assert(fresh.oracle_generation==previous.oracle_generation+1&&fresh.expected_mask==3&&fresh.observed_mask==0&&fresh.named_visitor==named);
 assert(access(&first,7,reinterpret_cast<void*>(11),reinterpret_cast<void*>(13),&result));
 assert(InspectFieldVarHandleGuards(registry).observed_mask==0); // Old targets cannot prove new pair.
 actual_thread=nullptr;const auto before_null_thread=original_calls;
 assert(!access(&third,7,reinterpret_cast<void*>(11),reinterpret_cast<void*>(13),&result));
 assert(original_calls==before_null_thread&&InspectFieldVarHandleGuards(registry).observed_mask==0);actual_thread=&current_thread;
 original_result=false;assert(!access(&third,7,reinterpret_cast<void*>(11),reinterpret_cast<void*>(13),&result));original_result=true;
 assert(InspectFieldVarHandleGuards(registry).observed_mask==0);
 change_thread=true;assert(!access(&third,7,reinterpret_cast<void*>(11),reinterpret_cast<void*>(13),&result));change_thread=false;actual_thread=&current_thread;
 assert(InspectFieldVarHandleGuards(registry).observed_mask==0);
 assert(access(&third,7,reinterpret_cast<void*>(11),reinterpret_cast<void*>(13),&result)&&result==42&&!FieldVarHandleGuardsReady(registry));
 assert(access(&fourth,7,reinterpret_cast<void*>(11),reinterpret_cast<void*>(13),&result)&&FieldVarHandleGuardsReady(registry));
 // Even identical current inputs require new successful original observations.
 const auto proved_generation=InspectFieldVarHandleGuards(registry).oracle_generation;
 assert(prepare(next)&&installs==1&&!FieldVarHandleGuardsReady(registry));
 assert(InspectFieldVarHandleGuards(registry).oracle_generation==proved_generation+1);
 assert(access(&third,7,reinterpret_cast<void*>(11),reinterpret_cast<void*>(13),&result));
 assert(!FieldVarHandleGuardsReady(registry));
 assert(access(&fourth,7,reinterpret_cast<void*>(11),reinterpret_cast<void*>(13),&result)&&FieldVarHandleGuardsReady(registry));
 // Exercise the actual outer guard against a cached handle whose native
 // target became final after its original successful calibration/invocation.
 // Missing Android11 named helper must retain the fixed source classifier.
 assert(var_handle_access_mode::Bind(nullptr,&mode_template));
 field_c.access_flags=0x10;const int before_final=original_calls;result=99;
 assert(!access(&third,7,reinterpret_cast<void*>(11),reinterpret_cast<void*>(13),&result));
 assert(result==0&&original_calls==before_final&&thrown==1);
 assert(access(&third,0,reinterpret_cast<void*>(11),reinterpret_cast<void*>(13),&result)&&result==42);
 // All cached modes preserve read-only operations and reject every write,
 // including compare/exchange, updates and acquire/release/opaque variants.
 for(int mode=0;mode<=30;++mode){
   const bool getter=mode==0||mode==2||mode==4||mode==6;
   result=99;const int before=original_calls,exceptions=thrown;
   assert(access(&third,mode,reinterpret_cast<void*>(11),reinterpret_cast<void*>(13),&result)==getter);
   assert(original_calls==before+(getter?1:0)&&thrown==exceptions+(getter?0:1));
   assert(result==(getter?42u:0u));
 }
 for(int mode:{-1,31}){const int before=original_calls;result=99;
   assert(!access(&third,mode,reinterpret_cast<void*>(11),reinterpret_cast<void*>(13),&result));
   assert(original_calls==before&&result==0);
 }
 thrown=1;
 // Removing final immediately restores the original cached writer. No sticky
 // handle mask or process-wide "some final field" bit decides access.
 field_c.access_flags=0;assert(access(&third,7,reinterpret_cast<void*>(11),reinterpret_cast<void*>(13),&result));
 field_d.access_flags=0x18;result=99;const int before_static_final=original_calls;
 assert(!access(&fourth,7,reinterpret_cast<void*>(11),reinterpret_cast<void*>(13),&result));
 assert(result==0&&original_calls==before_static_final&&thrown==2);
 field_d.access_flags=8;thrown=0;
 registry.Retire(&field_a);const auto originals_before=original_calls;result=99;
 assert(!access(&first,7,reinterpret_cast<void*>(11),reinterpret_cast<void*>(13),&result));
 assert(thrown==1&&result==0&&original_calls==originals_before&&FieldVarHandleGuardsReady(registry));
 // Non-field object is only eight bytes: field24 reads fail under ASan.
 assert(access(&other,7,reinterpret_cast<void*>(11),reinterpret_cast<void*>(13),&result)&&result==42);
 pending=true;assert(access(&other,7,reinterpret_cast<void*>(11),reinterpret_cast<void*>(13),&result)&&pending);pending=false;
 original_result=false;assert(!access(&other,7,reinterpret_cast<void*>(11),reinterpret_cast<void*>(13),&result));original_result=true;
 // Moving Class roots are re-decoded on every call; read barrier refreshes the
 // corresponding Object klass reference. No stale naked Class address survives.
 instance_native_class=0x5000;static_native_class=0x6000;other_native_class=0x7000;
 first.prefix.monitor=second.prefix.monitor=third.prefix.monitor=fourth.prefix.monitor=1u<<28;other.monitor=1u<<28;
 assert(access(&third,7,reinterpret_cast<void*>(11),reinterpret_cast<void*>(13),&result));
 assert(access(&fourth,7,reinterpret_cast<void*>(11),reinterpret_cast<void*>(13),&result));
 assert(access(&other,7,reinterpret_cast<void*>(11),reinterpret_cast<void*>(13),&result));
 field_class_failure=true;const int before_class_fail=original_calls;
 assert(!access(&third,7,reinterpret_cast<void*>(11),reinterpret_cast<void*>(13),&result)&&original_calls==before_class_fail);field_class_failure=false;
 const auto final=InspectFieldVarHandleGuards(registry);
 assert(final.field_entries&&final.other_entries==4&&final.target_seen_mask==3&&final.original_true&&final.original_false&&final.class_failures==1);
 assert(roots_created-roots_deleted==2&&array_verifications>=3);
 assert(!std::memcmp(&next_instance.mirror,&next_mirror,sizeof(next_mirror)));
 assert(DescribeFieldVarHandleGuards(registry).find("expected=3,observed=3")!=std::string::npos);
 assert(!std::memcmp(&instance.mirror,&instance_mirror,sizeof(instance_mirror))&&!std::memcmp(&statik.mirror,&static_mirror,sizeof(static_mirror)));
 std::cout<<"Field VarHandle production installer: shared class, public coordinates/modifiers/owner and independent JNI/native target refusal; sealed reader and actual ABI baseline readiness, readonly same-owner new-class refresh, failed-state preservation and old retirement passed\n";
}
