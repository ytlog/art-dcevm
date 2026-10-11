#include "platform/common/production_calibration_inputs.h"
#include "platform/common/production_final_dex.h"
#include "platform/common/ordinary_source_fixture.h"
#include "platform/common/schema_snapshot.h"
#include "platform/common/body_only_policy.h"
#include <map>
#include <cstdlib>
namespace artdcevm::production_bootstrap {
namespace {
constexpr char kAnchor[]="com/github/ytlog/artdcevm/internal/EvolutionCalibration";
constexpr char kStorage[]="com/github/ytlog/artdcevm/internal/CalibrationFixture";
bool Fail(std::string*error,const char*why){if(error)*error=why;return false;}
void JNICALL Marker(JNIEnv*,jobject){}
struct Local {JNIEnv*env;jobject value=nullptr;~Local(){if(value)env->DeleteLocalRef(value);}};
struct Text {jvmtiEnv*ti;char*value=nullptr;~Text(){if(value)ti->Deallocate(reinterpret_cast<unsigned char*>(value));}};
struct Ids {jvmtiEnv*ti;unsigned char*value=nullptr;~Ids(){if(value)ti->Deallocate(value);}};
bool Declaration(JNIEnv*env,jvmtiEnv*ti,jclass klass,Provenance&records,
 const std::vector<std::vector<unsigned char>>&apk,evolution::ClassSchema*out,
 evolution::MappedDexMemberCounts*counts,std::string*error){
 Text signature{ti};
 if(ti->GetClassSignature(klass,&signature.value,nullptr)!=JVMTI_ERROR_NONE||!signature.value)
  return Fail(error,"calibration-input-Class-signature-unavailable");
 std::vector<unsigned char>effective;
 if(records.Find(env,klass,&effective))
  return SelectInstalledClass({std::move(effective)},signature.value,out,counts,error);
 if(env->ExceptionCheck()||!records.Trusted())return Fail(error,"calibration-input-current-DEX-source-unavailable");
 return SelectInstalledClass(apk,signature.value,out,counts,error);
}
}
void*CalibrationInputs::NativeMarker()noexcept{return reinterpret_cast<void*>(Marker);}
CalibrationInputs::~CalibrationInputs(){
 if(roots_.empty())return;
 JNIEnv*env=nullptr;
 if(!vm_||vm_->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6)!=JNI_OK||!env)std::abort();
 for(auto root:roots_)env->DeleteGlobalRef(root);
}
jobject CalibrationInputs::Own(JNIEnv*env,jobject value){
 if(!value||env->ExceptionCheck())return nullptr;
 auto root=env->NewGlobalRef(value);
 if(!root||env->ExceptionCheck()){if(root)env->DeleteGlobalRef(root);return nullptr;}
 try{roots_.push_back(root);}catch(...){env->DeleteGlobalRef(root);throw;}
 return root;
}
std::unique_ptr<CalibrationInputs>CalibrationInputs::Create(JNIEnv*env,jvmtiEnv*ti,jobject fixture,std::string*error){
 auto result=std::unique_ptr<CalibrationInputs>(new CalibrationInputs);
 if(!result->Load(env,ti,fixture,error))return nullptr;
 return result;
}
bool CalibrationInputs::Load(JNIEnv*env,jvmtiEnv*ti,jobject fixture,std::string*error){
 if(!env||!ti||!fixture||env->ExceptionCheck()||env->GetJavaVM(&vm_)!=JNI_OK||!vm_)
  return Fail(error,"calibration-input-invalid-caller");
 JNIEnv*attached=nullptr;
 if(vm_->GetEnv(reinterpret_cast<void**>(&attached),JNI_VERSION_1_6)!=JNI_OK||attached!=env)
  return Fail(error,"calibration-input-current-JNI-attachment-differs");
 redefinition_gate::Lease lease(vm_);
 auto*records=lease.Acquired()?BorrowFinalDexRecords(vm_):nullptr;
 if(!records)return Fail(error,"calibration-input-complete-final-DEX-history-unavailable");
 Local expected{env,env->FindClass(kAnchor)},owner{env,env->GetObjectClass(fixture)};
 if(!expected.value||!owner.value||!env->IsSameObject(expected.value,owner.value)||env->ExceptionCheck())
  return Fail(error,"calibration-input-fixture-is-not-the-SDK-anchor");
 anchor_=static_cast<jclass>(Own(env,owner.value));if(!anchor_)return false;
 Local bootstrap{env,env->FindClass("com/github/ytlog/artdcevm/internal/ProductionBootstrap")};
 auto create=bootstrap.value?env->GetStaticMethodID(static_cast<jclass>(bootstrap.value),"create","()Lcom/github/ytlog/artdcevm/internal/ProductionBootstrap$Inputs;"):nullptr;
 Local value{env,create?env->CallStaticObjectMethod(static_cast<jclass>(bootstrap.value),create):nullptr};
 input_=Own(env,value.value);if(!input_)return Fail(error,"calibration-input-public-bootstrap-unavailable");
 bootstrap_=static_cast<jclass>(Own(env,bootstrap.value));if(!bootstrap_)return false;
 Local type{env,env->GetObjectClass(input_)};
 auto dex_id=type.value?env->GetFieldID(static_cast<jclass>(type.value),"dex","[[B"):nullptr;
 Local dex{env,dex_id?env->GetObjectField(input_,dex_id):nullptr};
 if(!dex.value||env->ExceptionCheck())return Fail(error,"calibration-input-APK-DEX-list-unavailable");
 const auto count=env->GetArrayLength(static_cast<jobjectArray>(dex.value));
 if(count<=0||env->ExceptionCheck())return Fail(error,"calibration-input-APK-DEX-list-empty");
 apk_.reserve(size_t(count));
 for(jsize n=0;n<count;++n){
  Local bytes{env,env->GetObjectArrayElement(static_cast<jobjectArray>(dex.value),n)};
  if(!bytes.value||env->ExceptionCheck())return false;
  const auto length=env->GetArrayLength(static_cast<jbyteArray>(bytes.value));if(length<=0||env->ExceptionCheck())return false;
  std::vector<unsigned char>copy(size_t(length),0);
  env->GetByteArrayRegion(static_cast<jbyteArray>(bytes.value),0,length,reinterpret_cast<jbyte*>(copy.data()));
  if(env->ExceptionCheck())return false;apk_.push_back(std::move(copy));
 }
 auto storage_id=env->GetFieldID(static_cast<jclass>(type.value),"storage","Ljava/lang/Object;");
 Local storage{env,storage_id?env->GetObjectField(input_,storage_id):nullptr};
 storage_=Own(env,storage.value);if(!storage_)return Fail(error,"calibration-input-storage-unavailable");
 Local storage_type{env,env->GetObjectClass(storage_)},expected_storage{env,env->FindClass(kStorage)};
 if(!storage_type.value||!expected_storage.value||!env->IsSameObject(storage_type.value,expected_storage.value)||env->ExceptionCheck())
  return Fail(error,"calibration-input-storage-is-not-the-SDK-fixture");
 storage_class_=static_cast<jclass>(Own(env,storage_type.value));if(!storage_class_)return false;
 evolution::MappedDexMemberCounts field_counts{};
 if(!Declaration(env,ti,anchor_,*records,apk_,&methods_schema_,&method_counts_,error)||
    !Declaration(env,ti,storage_class_,*records,apk_,&fields_schema_,&field_counts,error))return false;
 jint method_count=0;jmethodID*method_ids=nullptr;
 const auto method_result=ti->GetClassMethods(anchor_,&method_count,&method_ids);
 Ids owned_methods{ti,reinterpret_cast<unsigned char*>(method_ids)};
 if(method_result!=JVMTI_ERROR_NONE||method_count<3||!method_ids||size_t(method_count)!=methods_schema_.methods.size())
  return Fail(error,"calibration-input-complete-method-census-differs-from-DEX");
 std::map<std::pair<std::string,std::string>,const evolution::Member*>expected_methods;
 for(const auto&member:methods_schema_.methods)if(!expected_methods.emplace(std::make_pair(member.name,member.descriptor),&member).second)return false;
 methods_.reserve(size_t(method_count));
 for(jint n=0;n<method_count;++n){
  Text name{ti},signature{ti};jint flags=0;
  if(ti->GetMethodName(method_ids[n],&name.value,&signature.value,nullptr)!=JVMTI_ERROR_NONE||
     ti->GetMethodModifiers(method_ids[n],&flags)!=JVMTI_ERROR_NONE||!name.value||!signature.value)return false;
  auto found=expected_methods.find({name.value,signature.value});
  if(found==expected_methods.end()||(found->second->access&0xffffu)!=(uint32_t(flags)&0xffffu))
   return Fail(error,"calibration-input-method-identity-differs-from-DEX");
  Local mirror{env,env->ToReflectedMethod(anchor_,method_ids[n],flags&8?JNI_TRUE:JNI_FALSE)};
  auto root=Own(env,mirror.value);if(!root)return false;
  methods_.push_back({root,found->second->dex_index,uint32_t(flags)});expected_methods.erase(found);
 }
 if(!expected_methods.empty())return false;
 jint field_count=0;jfieldID*field_ids=nullptr;
 const auto field_result=ti->GetClassFields(storage_class_,&field_count,&field_ids);
 Ids owned_fields{ti,reinterpret_cast<unsigned char*>(field_ids)};
 if(field_result!=JVMTI_ERROR_NONE||field_count<=0||!field_ids||size_t(field_count)!=fields_schema_.fields.size())
  return Fail(error,"calibration-input-complete-field-census-differs-from-DEX");
 std::map<std::pair<std::string,std::string>,const evolution::Member*>expected_fields;
 for(const auto&member:fields_schema_.fields)if(!expected_fields.emplace(std::make_pair(member.name,member.descriptor),&member).second)return false;
 field_names_.reserve(size_t(field_count));field_types_.reserve(size_t(field_count));fields_.reserve(size_t(field_count));
 for(jint n=0;n<field_count;++n){
  Text name{ti},signature{ti};jint flags=0;
  if(ti->GetFieldName(storage_class_,field_ids[n],&name.value,&signature.value,nullptr)!=JVMTI_ERROR_NONE||
     ti->GetFieldModifiers(storage_class_,field_ids[n],&flags)!=JVMTI_ERROR_NONE||!name.value||!signature.value)return false;
  auto found=expected_fields.find({name.value,signature.value});
  if(found==expected_fields.end()||(found->second->access&0xffffu)!=(uint32_t(flags)&0xffffu))
   return Fail(error,"calibration-input-field-identity-differs-from-DEX");
  Local mirror{env,env->ToReflectedField(storage_class_,field_ids[n],flags&8?JNI_TRUE:JNI_FALSE)};
  auto root=Own(env,mirror.value);if(!root)return false;
  field_names_.emplace_back(name.value);field_types_.emplace_back(signature.value);
  fields_.push_back({root,field_names_.back().c_str(),field_types_.back().c_str(),uint32_t(flags),found->second->dex_index});expected_fields.erase(found);
 }
 if(!expected_fields.empty()||env->ExceptionCheck())return false;
 if(!LoadOrdinary(env,ti,static_cast<jclass>(type.value),error))return false;
 bool native_marker=false;
 for(const auto&method:methods_schema_.methods)
  if(method.name=="bootstrapNativeMarker"&&method.descriptor=="()V"&&
     (method.access&0x108u)==0x100u)native_marker=true;
 if(!native_marker)return Fail(error,"calibration-input-owned-native-marker-declaration-unavailable");
 JNINativeMethod marker{const_cast<char*>("bootstrapNativeMarker"),const_cast<char*>("()V"),NativeMarker()};
 if(env->RegisterNatives(anchor_,&marker,1)!=JNI_OK||env->ExceptionCheck())return Fail(error,"calibration-input-native-marker-registration-failed");
 return true;
}
bool CalibrationInputs::LoadOrdinary(JNIEnv*env,jvmtiEnv*ti,jclass type,std::string*error){
 auto class_id=env->GetFieldID(type,"ordinarySourceClass","Ljava/lang/Class;");
 auto object_id=env->ExceptionCheck()?nullptr:env->GetFieldID(type,"ordinarySourceObject","Ljava/lang/Object;");
 auto dex_id=env->ExceptionCheck()?nullptr:env->GetFieldID(type,"ordinarySourceDex","[[B");
 if(!class_id||!object_id||!dex_id||env->ExceptionCheck())return Fail(error,"calibration-input-ordinary-source-fields-unavailable");
 Local klass{env,env->GetObjectField(input_,class_id)};
 ordinary_class_=static_cast<jclass>(Own(env,klass.value));if(!ordinary_class_)return false;
 Local object{env,env->GetObjectField(input_,object_id)};
 ordinary_object_=Own(env,object.value);if(!ordinary_object_)return false;
 Local actual{env,env->GetObjectClass(ordinary_object_)};
 if(!actual.value||!env->IsSameObject(actual.value,ordinary_class_)||env->ExceptionCheck())
  return Fail(error,"calibration-input-ordinary-object-Class-differs");
 Local arrays{env,env->GetObjectField(input_,dex_id)};
 if(!arrays.value||env->ExceptionCheck()||env->GetArrayLength(static_cast<jobjectArray>(arrays.value))!=3||env->ExceptionCheck())
  return Fail(error,"calibration-input-ordinary-source-needs-three-DEX-inputs");
 for(size_t n=0;n<ordinary_dex_.size();++n){
  Local bytes{env,env->GetObjectArrayElement(static_cast<jobjectArray>(arrays.value),static_cast<jsize>(n))};
  if(!bytes.value||env->ExceptionCheck())return Fail(error,"calibration-input-ordinary-source-DEX-unavailable");
  const auto length=env->GetArrayLength(static_cast<jbyteArray>(bytes.value));
  if(length<=0||length>64*1024*1024||env->ExceptionCheck())return Fail(error,"calibration-input-ordinary-source-DEX-size-refused");
  auto&copy=ordinary_dex_[n];copy.resize(static_cast<size_t>(length));
  env->GetByteArrayRegion(static_cast<jbyteArray>(bytes.value),0,length,reinterpret_cast<jbyte*>(copy.data()));
  if(env->ExceptionCheck())return false;
 }
 evolution::ClassSchema live;
 if(live_schema::ReadLiveSchema(ti,env,ordinary_class_,&live,error)!=JVMTI_ERROR_NONE||
    env->ExceptionCheck()||!ordinary_source_fixture::Validate(live,ordinary_dex_,error))return false;
 return VerifyOrdinary(env,ti,0,error);
}
bool CalibrationInputs::VerifyOrdinary(JNIEnv*env,jvmtiEnv*ti,size_t round,std::string*error)const{
 if(!env||!ti||round>=ordinary_dex_.size()||!bootstrap_||!ordinary_class_||!ordinary_object_||env->ExceptionCheck())
  return Fail(error,"calibration-input-ordinary-verification-invalid-caller-or-round");
 JavaVM*vm=nullptr;JNIEnv*attached=nullptr;
 if(env->GetJavaVM(&vm)!=JNI_OK||vm!=vm_||env->ExceptionCheck()||
    vm_->GetEnv(reinterpret_cast<void**>(&attached),JNI_VERSION_1_6)!=JNI_OK||attached!=env)
  return Fail(error,"calibration-input-ordinary-verification-JNI-attachment-differs");
 auto*records=BorrowFinalDexRecords(vm_);
 if(!records)return Fail(error,"calibration-input-ordinary-verification-final-DEX-history-or-lease-unavailable");
 if(round){
  std::vector<unsigned char>effective;
  if(!records->Find(env,ordinary_class_,&effective)||env->ExceptionCheck()||effective!=ordinary_dex_[round])
   return Fail(error,"calibration-input-ordinary-verification-final-transformed-DEX-differs");
 }
 evolution::ClassSchema live,expected;
 if(live_schema::ReadLiveSchema(ti,env,ordinary_class_,&live,error)!=JVMTI_ERROR_NONE||env->ExceptionCheck()||
    !evolution::ReadDexDeclarations(ordinary_dex_[round],&expected,error)||!body_only::SameDeclarations(live,expected))
  return Fail(error,"calibration-input-ordinary-verification-live-declarations-differ");
 Local type{env,env->GetObjectClass(input_)};
 if(!type.value||env->ExceptionCheck())return Fail(error,"calibration-input-ordinary-input-owner-unavailable");
 auto class_id=env->GetFieldID(static_cast<jclass>(type.value),"ordinarySourceClass","Ljava/lang/Class;");
 auto object_id=env->ExceptionCheck()?nullptr:env->GetFieldID(static_cast<jclass>(type.value),"ordinarySourceObject","Ljava/lang/Object;");
 if(!class_id||!object_id||env->ExceptionCheck())return false;
 Local java_class{env,env->GetObjectField(input_,class_id)};
 if(!java_class.value||env->ExceptionCheck())return false;
 Local java_object{env,env->GetObjectField(input_,object_id)};
 if(!java_object.value||env->ExceptionCheck()||!env->IsSameObject(java_class.value,ordinary_class_)||
    !env->IsSameObject(java_object.value,ordinary_object_)||env->ExceptionCheck())
  return Fail(error,"calibration-input-ordinary-Java-oracle-owners-differ");
 // Resolve the public opaque JNI ID afresh. No native method/Class address or
 // saved Runnable Context is retained across this Java oracle invocation.
 auto verify=env->GetStaticMethodID(bootstrap_,"verifyOrdinarySource","(Lcom/github/ytlog/artdcevm/internal/ProductionBootstrap$Inputs;I)Z");
 if(!verify||env->ExceptionCheck())return Fail(error,"calibration-input-ordinary-public-behavior-oracle-unavailable");
 const auto accepted=env->CallStaticBooleanMethod(bootstrap_,verify,input_,static_cast<jint>(round));
 if(env->ExceptionCheck()||accepted!=JNI_TRUE)
  return Fail(error,"calibration-input-ordinary-retained-or-fresh-object-oracle-refused");
 return true;
}

}
