#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "provider_fixture.inc"
#pragma clang diagnostic pop
#include "platform/common/native_declared_field_lookup.h"
#include "platform/common/native_lazy_field_mirror.h"
#include "platform/common/rooted_field_identities.h"
namespace {
struct LookupOwner {modern_field_metadata::FieldMetadata* metadata=nullptr;native_lazy_field_mirror::Service lazy; jvmtiEnv* ti=nullptr;};
LookupOwner lookup_owner;
std::array<int,4> opaque_ids{};int declaring_method,flags_method;
size_t lookup_globals=0,lookup_factory_calls=0,lookup_calls=0,lookup_inspected=0,lookup_os=0;
size_t lookup_jni_during_reads=0;bool lookup_move=false,lookup_moved=false,lookup_wrong_factory=false,lookup_wrong_owner=false,lookup_duplicate=false;
void* lookup_moved_class=nullptr;std::vector<void*> lookup_arrays;
void* Fresh(void* value){return lookup_moved&&value==old_class?lookup_moved_class:value;}
size_t FieldOrdinal(void* field){for(size_t i=0;i<4;++i)if(field==old_fields[i]||field==new_fields[i])return i;std::abort();}
jfieldID Opaque(void* field){return reinterpret_cast<jfieldID>(&opaque_ids[FieldOrdinal(field)]);}
jclass PublicOwner(void* field){auto* native=lookup_owner.metadata->FieldDeclaringClass(field);return reinterpret_cast<jclass>(native==lookup_moved_class?old_class:native);}
const char* LookupName(void* field){return lookup_duplicate&&field==new_fields[2]?names[3]:ResolutionName(field);}
void* LookupMark(void* value){return Fresh(Mark(value));}
void* LookupRoot(void* slot){return LookupMark(Root(slot));}
native_field_factory_observer::Return LookupFactory(void* self,void* field,bool force){assert(self==current&&!force&&!stopped);++lookup_factory_calls;
 if(lookup_move&&!lookup_moved){lookup_moved_class=heap.New(sizeof(class_metadata::Class17));std::memcpy(lookup_moved_class,old_class,sizeof(class_metadata::Class17));
  for(auto* member:old_fields)member->declaring_class=uint32_t(uintptr_t(lookup_moved_class));lookup_moved=true;
 }
 auto* mirror=static_cast<Mirror*>(heap.New(sizeof(Mirror)));mirror->field=lookup_wrong_factory?new_fields[FieldOrdinal(field)]:field;return {uintptr_t(mirror)};
}
jobject LookupLocal(JNIEnv*,void* value){Jni();return reinterpret_cast<jobject>(value);}
class LookupHooks final:public ProviderHooks {public:
 void* FindSymbol(const char* lib,const char* name)override{
  if(!std::strcmp(name,"_ZN3art6mirror5Field18CreateFromArtFieldEPNS_6ThreadEPNS_8ArtFieldEb"))return reinterpret_cast<void*>(LookupFactory);
  if(!std::strcmp(name,"_ZN3art9JNIEnvExt11NewLocalRefEPNS_6mirror6ObjectE"))return reinterpret_cast<void*>(LookupLocal);
  if(std::strstr(name,"ArtField7GetName"))return reinterpret_cast<void*>(LookupName);
  if(!std::strcmp(name,"artReadBarrierMark"))return reinterpret_cast<void*>(LookupMark);
  if(!std::strcmp(name,"artReadBarrierForRootSlow"))return reinterpret_cast<void*>(LookupRoot);
  return ProviderHooks::FindSymbol(lib,name);
 }
};
bool LookupScope(void*,JNIEnv* e,jobject root,runnable_jni::Operation operation,void* data){return runnable_executor::With(e,root,operation,data)==runnable_jni::Result::kSuccess;}
bool LookupOracle(void*,const runnable_jni::Context& c,jobject root,void** out){if(!runnable_executor::DecodeStrongGlobal(c,root,out))return false;*out=Fresh(*out);return true;}
bool LookupDecode(void*,jobject root,void** out){assert(!stopped&&Read32(current)==0);*out=Fresh(reinterpret_cast<void*>(root));return *out;}
void* LookupMirrorField(void*,void* mirror){return FieldLeaf(mirror);}
void* LookupDeclaring(void*,void* field){return lookup_owner.metadata->FieldDeclaringClass(field);}
void* LookupCalibratedId(void*,const runnable_jni::Context&,void* owner,jfieldID id){
 auto& fields=owner==Fresh(old_class)?old_fields:new_fields;for(auto* field:fields)if(Opaque(field)==id&&LookupDeclaring(nullptr,field)==owner)return field;return nullptr;
}
void* LookupDeclared(void*,const runnable_jni::Context& c,void* owner,const original_field_declaration::Declaration& declaration,std::string* error){
 const auto jni=jni_calls;const auto os=method_identity::MappingOsQueries();native_declared_field_lookup::Observation observed;
 void* result=native_declared_field_lookup::Read(*lookup_owner.metadata,c.self,Current,owner,declaration,error,&observed);
 ++lookup_calls;lookup_inspected+=observed.inspected_fields;lookup_os+=observed.mapping_os_queries;
 assert(method_identity::MappingOsQueries()-os==observed.mapping_os_queries);lookup_jni_during_reads+=size_t(jni_calls-jni);assert(jni_calls==jni);return result;
}
void* LookupElement(void*,void* array,size_t index){return lookup_owner.metadata->Reference(array,12+index*4);}
bool LookupProduce(void*,JNIEnv* e,jclass c,jfieldID id,bool statik,jobject* out,std::string* error){return lookup_owner.lazy.CreateDeclared(e,lookup_owner.ti,c,id,statik,out,error);}
jobject LookupGlobal(JNIEnv* e,jobject object){++lookup_globals;return Global(e,object);}
void LookupDeleteGlobal(JNIEnv* e,jobject object){assert(lookup_globals);--lookup_globals;Delete(e,object);}
jfieldID LookupFrom(JNIEnv*,jobject mirror){Jni();return Opaque(static_cast<Mirror*>(static_cast<void*>(mirror))->field);}
jclass LookupObjectClass(JNIEnv*,jobject){Jni();return reinterpret_cast<jclass>(meta_class);}
jmethodID LookupMethod(JNIEnv*,jclass,const char* name,const char*){Jni();return reinterpret_cast<jmethodID>(!std::strcmp(name,"getDeclaringClass")?&declaring_method:&flags_method);}
jobject LookupCall(JNIEnv*,jobject object,jmethodID id,va_list){Jni();assert(id==reinterpret_cast<jmethodID>(&declaring_method));return reinterpret_cast<jobject>(PublicOwner(static_cast<Mirror*>(static_cast<void*>(object))->field));}
jint LookupInt(JNIEnv*,jobject object,jmethodID id,va_list){Jni();assert(id==reinterpret_cast<jmethodID>(&flags_method));return jint(static_cast<abi::arm64::Field*>(static_cast<Mirror*>(static_cast<void*>(object))->field)->access_flags);}
jobjectArray LookupArray(JNIEnv*,jsize count,jclass,jobject){Jni();auto* array=heap.New(12+size_t(count)*4);Put32(array,8,uint32_t(count));lookup_arrays.push_back(array);return reinterpret_cast<jobjectArray>(array);}
jsize LookupLength(JNIEnv*,jarray array){Jni();return jsize(Read32(array,8));}
jobject LookupGet(JNIEnv*,jobjectArray array,jsize index){Jni();return reinterpret_cast<jobject>(uintptr_t(Read32(array,12+size_t(index)*4)));}
void LookupSet(JNIEnv*,jobjectArray array,jsize index,jobject value){Jni();Put32(array,12+size_t(index)*4,uint32_t(uintptr_t(value)));}
size_t PublicOrdinal(jfieldID id){for(size_t i=0;i<opaque_ids.size();++i)if(id==reinterpret_cast<jfieldID>(&opaque_ids[i]))return i;std::abort();}
jvmtiError LookupPublicOwner(jvmtiEnv*,jclass c,jfieldID,jclass* out){Jni();*out=lookup_wrong_owner?reinterpret_cast<jclass>(parent_class):c;return JVMTI_ERROR_NONE;}
jvmtiError LookupPublicText(jvmtiEnv*,jclass,jfieldID id,char** name,char** descriptor,char** generic){Jni();const auto i=PublicOrdinal(id);*name=strdup(names[i]);*descriptor=strdup(types[i]);if(generic)*generic=nullptr;return JVMTI_ERROR_NONE;}
jvmtiError LookupPublicFlags(jvmtiEnv*,jclass,jfieldID id,jint* out){Jni();*out=PublicOrdinal(id)==0?8:1;return JVMTI_ERROR_NONE;}
jvmtiError LookupFree(jvmtiEnv*,unsigned char* value){Jni();std::free(value);return JVMTI_ERROR_NONE;}
}
int main(){
 InitJni();InitializeHeap();InterfaceFixtures();LookupHooks hooks;std::string error;
 const uint32_t indexes[]{1,3,0,2};for(size_t i=0;i<4;++i){old_fields[i]->dex_field_index=indexes[i];new_fields[i]->dex_field_index=indexes[i];}
 std::array<modern_field_metadata::FieldOracle,4> oracles;for(size_t i=0;i<4;++i)oracles[i]={reinterpret_cast<jobject>(&mirrors[i]),names[i],types[i],i==0?8u:1u};
 modern_field_metadata::FieldMetadata metadata;assert(metadata.Bind(hooks,&error));
 struct Calibration{modern_field_metadata::FieldMetadata* metadata;const modern_field_metadata::FieldOracle* oracles;std::string* error;} calibration{&metadata,oracles.data(),&error};
 auto calibrate=[](const runnable_jni::Context& c,void* raw){auto& r=*static_cast<Calibration*>(raw);return r.metadata->Calibrate(c,reinterpret_cast<jobject>(old_class),"LFixture;",r.oracles,4,r.error);};
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),calibrate,&calibration)==runnable_jni::Result::kSuccess);lookup_owner.metadata=&metadata;
 table.NewGlobalRef=LookupGlobal;table.DeleteGlobalRef=LookupDeleteGlobal;table.FromReflectedField=LookupFrom;table.GetObjectClass=LookupObjectClass;table.GetMethodID=LookupMethod;table.CallObjectMethodV=LookupCall;table.CallIntMethodV=LookupInt;
 table.NewObjectArray=LookupArray;table.GetArrayLength=LookupLength;table.GetObjectArrayElement=LookupGet;table.SetObjectArrayElement=LookupSet;
 jvmtiInterface_1 ti_functions{};ti_functions.GetFieldDeclaringClass=LookupPublicOwner;ti_functions.GetFieldName=LookupPublicText;ti_functions.GetFieldModifiers=LookupPublicFlags;ti_functions.Deallocate=LookupFree;jvmtiEnv ti{&ti_functions};lookup_owner.ti=&ti;
 native_lazy_field_mirror::Bindings b{&lookup_owner,true,LookupScope,LookupOracle,LookupDecode,LookupCalibratedId,LookupMirrorField,LookupDeclaring};b.declared_field=LookupDeclared;
 assert(lookup_owner.lazy.Bind(hooks,b,&error));
 const std::vector<native_lazy_field_mirror::Oracle> samples{{reinterpret_cast<jobject>(&mirrors[0]),reinterpret_cast<jclass>(old_class),Opaque(old_fields[0]),old_fields[0],true},{reinterpret_cast<jobject>(&mirrors[1]),reinterpret_cast<jclass>(old_class),Opaque(old_fields[1]),old_fields[1],false}};
 assert(lookup_owner.lazy.Calibrate(&env,samples,&error));assert(lookup_globals==1);
 rooted_field_identities::Store store{{&lookup_owner,LookupScope,LookupOracle,LookupDecode,LookupMirrorField,LookupDeclaring,LookupElement,LookupProduce}};
 const auto baseline_os=method_identity::MappingOsQueries();const auto baseline_jni=jni_calls;const auto baseline_factory=lookup_factory_calls;
 lookup_move=true;
 for(auto* klass:{old_class,new_class})for(size_t i=0;i<4;++i)assert(store.Stage(&env,reinterpret_cast<jclass>(klass),Opaque(old_fields[i]),i==0,&error));
 assert(lookup_moved&&lookup_calls==16&&lookup_inspected==40&&!lookup_jni_during_reads);
 struct ReadRequest{rooted_field_identities::Store* store;};ReadRequest read{&store};
 auto read_native=[](const runnable_jni::Context&,void* raw){auto& r=*static_cast<ReadRequest*>(raw);for(size_t i=0;i<4;++i){assert(r.store->ReadNative(Fresh(old_class),Opaque(old_fields[i]))==old_fields[i]);assert(r.store->ReadNative(new_class,Opaque(new_fields[i]))==new_fields[i]);}return true;};
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),read_native,&read)==runnable_jni::Result::kSuccess);
 std::cout<<"Actual lazy+Store Stage fields=8 declaration-scans="<<lookup_calls<<" inspected-selected-fields="<<lookup_inspected<<" mapping-snapshot-queries="<<lookup_os<<" total-mapping-queries="<<method_identity::MappingOsQueries()-baseline_os<<" public-JNI-calls="<<jni_calls-baseline_jni<<" factory-calls="<<lookup_factory_calls-baseline_factory<<" native-read-JNI="<<lookup_jni_during_reads<<" moved-owner=1 colliding-opaque-IDs=1\n";
 const auto factories=lookup_factory_calls;lookup_wrong_owner=true;jobject produced=nullptr;assert(!lookup_owner.lazy.CreateDeclared(&env,&ti,reinterpret_cast<jclass>(old_class),Opaque(old_fields[0]),true,&produced,&error)&&!produced&&lookup_factory_calls==factories);lookup_wrong_owner=false;
 lookup_wrong_factory=true;assert(!lookup_owner.lazy.CreateDeclared(&env,&ti,reinterpret_cast<jclass>(old_class),Opaque(old_fields[0]),true,&produced,&error)&&!produced);lookup_wrong_factory=false;
 // Exact duplicate text is independently exposed by the named source leaf.
 lookup_duplicate=true;
 // A duplicate exact declaration must refuse after the complete selected
 // table was inspected; the public owner and opaque-ID namespace stay valid.
 original_field_declaration::Declaration wrong{names[3],types[3],1};native_declared_field_lookup::Observation denied;
 struct Negative{modern_field_metadata::FieldMetadata* metadata;original_field_declaration::Declaration* declaration;std::string* error;native_declared_field_lookup::Observation* observed;} negative{&metadata,&wrong,&error,&denied};
 auto reject=[](const runnable_jni::Context& c,void* raw){auto&r=*static_cast<Negative*>(raw);return !native_declared_field_lookup::Read(*r.metadata,c.self,Current,new_class,*r.declaration,r.error,r.observed)&&r.observed->inspected_fields==3&&!native_declared_field_lookup::Read(*r.metadata,&lookup_owner,Current,new_class,*r.declaration,r.error);};
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),reject,&negative)==runnable_jni::Result::kSuccess);lookup_duplicate=false;
 for(const auto& declaration:std::array<original_field_declaration::Declaration,3>{{{"absent","I",1},{names[3],"[Ljava/lang/Object;",1},{names[3],types[3],17}}}){
  wrong=declaration;assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),reject,&negative)==runnable_jni::Result::kSuccess);
 }
 const auto declared_owner=new_fields[3]->declaring_class;new_fields[3]->declaring_class=uint32_t(uintptr_t(parent_class));wrong={names[3],types[3],1};
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),reject,&negative)==runnable_jni::Result::kSuccess);new_fields[3]->declaring_class=declared_owner;
 store.Clear(&env);lookup_owner.lazy.Clear(&env);assert(!lookup_globals);
 std::cout<<"Actual fresh declaration traversal + calibrated lazy factories + shared contextual Store: moved Class reread, complete scans, opaque collision namespaces, foreign owner/wrong factory/duplicate/missing descriptor/flags/current Thread refusal and root cleanup PASS (named JNI/ART leaves doubled)\n";
}
