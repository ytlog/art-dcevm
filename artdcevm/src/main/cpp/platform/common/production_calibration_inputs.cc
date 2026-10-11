#include "platform/common/production_calibration_inputs.h"
#include "platform/common/production_final_dex.h"
#include "platform/common/ordinary_source_fixture.h"
#include "platform/common/schema_snapshot.h"
#include "platform/common/body_only_policy.h"
#include "platform/common/native_receiver_public_sample.h"
#include "platform/common/native_call_graph_incoming.h"
#include <map>
#include <cstdlib>
#include <cstring>
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
 evolution::MappedDexMemberCounts*counts,std::string*error,std::vector<unsigned char>*selected=nullptr){
 Text signature{ti};
 if(ti->GetClassSignature(klass,&signature.value,nullptr)!=JVMTI_ERROR_NONE||!signature.value)
  return Fail(error,"calibration-input-Class-signature-unavailable");
 std::vector<unsigned char>effective;
 const bool recorded=records.Find(env,klass,&effective);
 if(env->ExceptionCheck()||!records.Trusted())return Fail(error,"calibration-input-current-DEX-source-unavailable");
 const std::vector<std::vector<unsigned char>>current=recorded?
     std::vector<std::vector<unsigned char>>{std::move(effective)}:std::vector<std::vector<unsigned char>>{};
 const auto&files=recorded?current:apk;
 InstalledClassLocation location;
 if(!SelectInstalledClass(files,signature.value,out,counts,error,nullptr,selected?&location:nullptr))return false;
 if(selected){if(location.file_index>=files.size())return false;*selected=files[location.file_index];}
 return true;
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
 const char*handle_names[]{"instanceHandle","staticHandle"};
 for(size_t n=0;n<field_var_handles_.size();++n){
  auto id=env->GetFieldID(static_cast<jclass>(type.value),handle_names[n],"Ljava/lang/invoke/VarHandle;");
  Local handle{env,id?env->GetObjectField(input_,id):nullptr};
  field_var_handles_[n]=Own(env,handle.value);
  if(!field_var_handles_[n])return Fail(error,"calibration-input-field-VarHandle-unavailable");
 }
 const char*method_handle_names[]{"instanceGetter","instanceSetter","staticGetter","staticSetter"};
 for(size_t n=0;n<field_method_handles_.size();++n){
  auto id=env->GetFieldID(static_cast<jclass>(type.value),method_handle_names[n],"Ljava/lang/invoke/MethodHandle;");
  Local handle{env,id?env->GetObjectField(input_,id):nullptr};
  field_method_handles_[n]=Own(env,handle.value);
  if(!field_method_handles_[n])return Fail(error,"calibration-input-field-MethodHandle-unavailable");
 }
 auto handles_id=env->GetFieldID(static_cast<jclass>(type.value),"methodHandles","[Ljava/lang/invoke/MethodHandle;");
 Local handles{env,handles_id?env->GetObjectField(input_,handles_id):nullptr};
 if(!handles.value||env->ExceptionCheck()||env->GetArrayLength(static_cast<jobjectArray>(handles.value))!=jsize(method_handles_.size()))
  return Fail(error,"calibration-input-method-MethodHandle-count-differs");
 for(size_t n=0;n<method_handles_.size();++n){
  Local handle{env,env->GetObjectArrayElement(static_cast<jobjectArray>(handles.value),jsize(n))};
  method_handles_[n]=Own(env,handle.value);
  if(!method_handles_[n])return Fail(error,"calibration-input-method-MethodHandle-unavailable");
 }
 if(!LoadOrdinary(env,ti,static_cast<jclass>(type.value),error)||
    !LoadReceiver(env,ti,static_cast<jclass>(type.value),error)||
    !LoadFinalWriter(env,ti,static_cast<jclass>(type.value),error))return false;
 bool native_marker=false;
 for(const auto&method:methods_schema_.methods)
  if(method.name=="bootstrapNativeMarker"&&method.descriptor=="()V"&&
     (method.access&0x108u)==0x100u)native_marker=true;
 if(!native_marker)return Fail(error,"calibration-input-owned-native-marker-declaration-unavailable");
 JNINativeMethod marker{const_cast<char*>("bootstrapNativeMarker"),const_cast<char*>("()V"),NativeMarker()};
 if(env->RegisterNatives(anchor_,&marker,1)!=JNI_OK||env->ExceptionCheck())return Fail(error,"calibration-input-native-marker-registration-failed");
 return true;
}
bool CalibrationInputs::InvokeReceiver(JNIEnv*env,size_t index,bool*matched,std::string*error)const{
 if(matched)*matched=false;JavaVM*vm=nullptr;
 if(!matched||!env||index>=receiver_samples_.size()||!bootstrap_||!input_||env->ExceptionCheck()||
    env->GetJavaVM(&vm)!=JNI_OK||vm!=vm_||!redefinition_gate::Owned(vm))
  return Fail(error,"calibration-input-receiver-invocation-invalid-caller-or-index");
 auto run=env->GetStaticMethodID(bootstrap_,"runReceiverSource","(Lcom/github/ytlog/artdcevm/internal/ProductionBootstrap$Inputs;I)I");
 if(!run||env->ExceptionCheck())return false;
 const auto result=env->CallStaticIntMethod(bootstrap_,run,input_,jint(index));
 if(env->ExceptionCheck())return false;
 constexpr jint expected[]{11,31,21,41,41,61,51,71,31,51};
 *matched=result==expected[index];return true;
}
bool CalibrationInputs::ExerciseFieldHandles(JNIEnv*env,std::string*error)const{
 JavaVM*vm=nullptr;
 if(!env||env->ExceptionCheck()||!bootstrap_||!input_||env->GetJavaVM(&vm)!=JNI_OK||
    vm!=vm_||!redefinition_gate::Owned(vm))
  return Fail(error,"calibration-input-field-handle-exercise-invalid-caller");
 auto run=env->GetStaticMethodID(bootstrap_,"exerciseFieldHandles","(Lcom/github/ytlog/artdcevm/internal/ProductionBootstrap$Inputs;)Z");
 if(!run||env->ExceptionCheck())return false;
 const bool matched=env->CallStaticBooleanMethod(bootstrap_,run,input_)==JNI_TRUE;
 if(env->ExceptionCheck())return false;
 return matched||Fail(error,"calibration-input-field-handle-public-readback-differs");
}
bool CalibrationInputs::ExerciseFieldMethodHandle(JNIEnv*env,size_t index,std::string*error)const{
 JavaVM*vm=nullptr;
 if(index>=field_method_handles_.size()||!env||env->ExceptionCheck()||!bootstrap_||!input_||
    env->GetJavaVM(&vm)!=JNI_OK||vm!=vm_||!redefinition_gate::Owned(vm))
  return Fail(error,"calibration-input-field-MethodHandle-invalid-caller-or-index");
 auto run=env->GetStaticMethodID(bootstrap_,"exerciseFieldMethodHandle","(Lcom/github/ytlog/artdcevm/internal/ProductionBootstrap$Inputs;I)Z");
 if(!run||env->ExceptionCheck())return false;
 const bool matched=env->CallStaticBooleanMethod(bootstrap_,run,input_,jint(index))==JNI_TRUE;
 if(env->ExceptionCheck())return false;
 return matched||Fail(error,"calibration-input-field-MethodHandle-public-readback-differs");
}
bool CalibrationInputs::ExerciseMethodHandle(JNIEnv*env,size_t index,std::string*error)const{
 JavaVM*vm=nullptr;
 if(index>=method_handles_.size()||!env||env->ExceptionCheck()||!bootstrap_||!input_||
    env->GetJavaVM(&vm)!=JNI_OK||vm!=vm_||!redefinition_gate::Owned(vm))
  return Fail(error,"calibration-input-method-MethodHandle-invalid-caller-or-index");
 auto run=env->GetStaticMethodID(bootstrap_,"exerciseMethodHandle","(Lcom/github/ytlog/artdcevm/internal/ProductionBootstrap$Inputs;I)Z");
 if(!run||env->ExceptionCheck())return false;
 const bool matched=env->CallStaticBooleanMethod(bootstrap_,run,input_,jint(index))==JNI_TRUE;
 if(env->ExceptionCheck())return false;
 return matched||Fail(error,"calibration-input-method-MethodHandle-public-result-differs");
}
bool CalibrationInputs::PrepareFinalWrite(JNIEnv*env,size_t index,std::string*error)const{
 JavaVM*vm=nullptr;
 if(index>=final_fields_.size()||!env||env->ExceptionCheck()||!bootstrap_||!input_||
    env->GetJavaVM(&vm)!=JNI_OK||vm!=vm_||!redefinition_gate::Owned(vm))
  return Fail(error,"calibration-input-final-writer-prepare-invalid-owner-or-index");
 auto run=env->GetStaticMethodID(bootstrap_,"prepareFinalWrite","(Lcom/github/ytlog/artdcevm/internal/ProductionBootstrap$Inputs;I)Z");
 if(!run||env->ExceptionCheck())return false;
 const bool matched=env->CallStaticBooleanMethod(bootstrap_,run,input_,jint(index))==JNI_TRUE;
 if(env->ExceptionCheck())return false;
 return matched||Fail(error,"calibration-input-final-writer-legal-write-or-sentinel-differs");
}
bool CalibrationInputs::ExerciseFinalWrite(JNIEnv*env,size_t index,bool*illegal,bool*unchanged,std::string*error)const{
 if(illegal)*illegal=false;if(unchanged)*unchanged=false;JavaVM*vm=nullptr;
 if(!illegal||!unchanged||index>=final_fields_.size()||!env||env->ExceptionCheck()||!bootstrap_||!input_||
    env->GetJavaVM(&vm)!=JNI_OK||vm!=vm_||!redefinition_gate::Owned(vm))
  return Fail(error,"calibration-input-final-writer-exercise-invalid-owner-or-index");
 auto run=env->GetStaticMethodID(bootstrap_,"exerciseFinalWrite","(Lcom/github/ytlog/artdcevm/internal/ProductionBootstrap$Inputs;I)I");
 if(!run||env->ExceptionCheck())return false;
 const jint facts=env->CallStaticIntMethod(bootstrap_,run,input_,jint(index));
 if(env->ExceptionCheck())return false;
 if(facts<0||(facts&~3))return Fail(error,"calibration-input-final-writer-public-facts-invalid");
 *illegal=(facts&1)!=0;*unchanged=(facts&2)!=0;return true;
}
bool CalibrationInputs::LoadFinalWriter(JNIEnv*env,jvmtiEnv*ti,jclass type,std::string*error){
 std::string ignored;if(!error)error=&ignored;
 auto*records=BorrowFinalDexRecords(vm_);
 if(!records||receiver_guard::PreparePublicBytecodes(ti)!=JVMTI_ERROR_NONE)
  return Fail(error,"calibration-input-final-writer-DEX-history-or-bytecodes-unavailable");
 Local expected_target{env,env->FindClass("com/github/ytlog/artdcevm/internal/FinalPermissionOracle$Target")},
       expected_writer{env,env->FindClass("com/github/ytlog/artdcevm/internal/FinalPermissionOracle$Writer")};
 auto target_id=env->GetFieldID(type,"finalTarget","Lcom/github/ytlog/artdcevm/internal/FinalPermissionOracle$Target;");
 auto writer_id=env->GetFieldID(type,"finalWriter","Ljava/lang/reflect/Method;");
 if(!expected_target.value||!expected_writer.value||!target_id||!writer_id||env->ExceptionCheck())return false;
 Local target{env,env->GetObjectField(input_,target_id)},mirror{env,env->GetObjectField(input_,writer_id)};
 if(!target.value||!mirror.value||env->ExceptionCheck())return false;
 Local actual_target{env,env->GetObjectClass(target.value)};
 if(!actual_target.value||!env->IsSameObject(actual_target.value,expected_target.value)||env->ExceptionCheck())
  return Fail(error,"calibration-input-final-target-is-not-the-SDK-fixture");
 final_target_class_=static_cast<jclass>(Own(env,expected_target.value));
 final_writer_class_=static_cast<jclass>(Own(env,expected_writer.value));
 if(!final_target_class_||!final_writer_class_)return false;
 evolution::ClassSchema writer_schema,live;std::vector<unsigned char>writer_dex;
 evolution::MappedDexMemberCounts writer_counts{},field_counts{};
 if(!Declaration(env,ti,final_writer_class_,*records,apk_,&writer_schema,&writer_counts,error,&writer_dex)||
    !Declaration(env,ti,final_target_class_,*records,apk_,&final_fields_schema_,&field_counts,error)||
    live_schema::ReadLiveSchema(ti,env,final_writer_class_,&live,error)!=JVMTI_ERROR_NONE||
    env->ExceptionCheck()||!body_only::SameDeclarations(live,writer_schema))
  return Fail(error,"calibration-input-final-writer-complete-declarations-differ-from-DEX");
 if(live_schema::ReadLiveSchema(ti,env,final_target_class_,&live,error)!=JVMTI_ERROR_NONE||
    env->ExceptionCheck()||!body_only::SameDeclarations(live,final_fields_schema_)||
    final_fields_schema_.fields.size()!=final_fields_.size())
  return Fail(error,"calibration-input-final-target-complete-declarations-differ-from-DEX");
 const auto id=env->FromReflectedMethod(mirror.value);if(!id||env->ExceptionCheck())return false;
 jclass declaring=nullptr;
 if(ti->GetMethodDeclaringClass(id,&declaring)!=JVMTI_ERROR_NONE||!declaring){
  if(declaring)env->DeleteLocalRef(declaring);return false;
 }
 Local owned_declaring{env,declaring};Text name{ti},descriptor{ti};jint flags=0;
 if(!env->IsSameObject(declaring,final_writer_class_)||env->ExceptionCheck()||
    ti->GetMethodName(id,&name.value,&descriptor.value,nullptr)!=JVMTI_ERROR_NONE||
    ti->GetMethodModifiers(id,&flags)!=JVMTI_ERROR_NONE||!name.value||!descriptor.value||
    std::strcmp(name.value,"write")||std::strcmp(descriptor.value,
      "(Lcom/github/ytlog/artdcevm/internal/FinalPermissionOracle$Target;ILjava/lang/Object;ZZ)V")||
    !(flags&8)||(flags&(0x100|0x400)))
  return Fail(error,"calibration-input-final-writer-public-method-identity-refused");
 native_call_graph_incoming::Inventory inventory;
 if(!inventory.Capture(writer_dex,writer_schema.descriptor,error))return false;
 const evolution::Member*member=nullptr;
 for(const auto&method:inventory.Methods())if(method.declaration.name==name.value&&method.declaration.descriptor==descriptor.value){
  if(member)return Fail(error,"calibration-input-final-writer-DEX-identity-ambiguous");member=&method.declaration;
 }
 if(!member||(member->access&0xffffu)!=(uint32_t(flags)&0xffffu))return false;
 const auto*instructions=inventory.InstructionBytes(member->dex_index);
 jint byte_count=0;unsigned char*public_bytes=nullptr;
 const auto byte_result=ti->GetBytecodes(id,&byte_count,&public_bytes);Ids owned_bytes{ti,public_bytes};
 if(!instructions||instructions->empty()||byte_result!=JVMTI_ERROR_NONE||!public_bytes||byte_count<=0||
    size_t(byte_count)!=instructions->size()||std::memcmp(public_bytes,instructions->data(),instructions->size()))
  return Fail(error,"calibration-input-final-writer-public-bytecodes-differ-from-final-DEX");
 final_writer_instructions_=*instructions;
 final_writer_={Own(env,mirror.value),member->dex_index,uint32_t(flags)};
 if(!final_writer_.strong_global_method)return false;
 jint field_count=0;jfieldID*ids=nullptr;
 const auto field_result=ti->GetClassFields(final_target_class_,&field_count,&ids);
 Ids owned_fields{ti,reinterpret_cast<unsigned char*>(ids)};
 if(field_result!=JVMTI_ERROR_NONE||field_count!=jint(final_fields_.size())||!ids)return false;
 constexpr const char*names[]{"instanceValue","staticValue","instanceReference","staticReference"};
 uint32_t seen=0;
 for(jint n=0;n<field_count;++n){
  Text field_name{ti},field_type{ti};jint field_flags=0;
  if(ti->GetFieldName(final_target_class_,ids[n],&field_name.value,&field_type.value,nullptr)!=JVMTI_ERROR_NONE||
     ti->GetFieldModifiers(final_target_class_,ids[n],&field_flags)!=JVMTI_ERROR_NONE||
     !field_name.value||!field_type.value)return false;
  size_t index=0;while(index<final_fields_.size()&&std::strcmp(names[index],field_name.value))++index;
  if(index==final_fields_.size()||(seen&(1u<<index))||
     std::strcmp(field_type.value,index<2?"I":"Ljava/lang/Object;")||
     (uint32_t(field_flags)&0xffffu)!=(index&1?9u:1u))
   return Fail(error,"calibration-input-final-field-storage-path-identity-refused");
  const evolution::Member*field=nullptr;
  for(const auto&candidate:final_fields_schema_.fields)
   if(candidate.name==field_name.value&&candidate.descriptor==field_type.value){if(field)return false;field=&candidate;}
  if(!field||(field->access&0xffffu)!=(uint32_t(field_flags)&0xffffu))return false;
  Local field_mirror{env,env->ToReflectedField(final_target_class_,ids[n],index&1?JNI_TRUE:JNI_FALSE)};
  auto root=Own(env,field_mirror.value);if(!root)return false;
  final_fields_[index]={root,field->name.c_str(),field->descriptor.c_str(),uint32_t(field_flags),field->dex_index};
  seen|=1u<<index;
 }
 return seen==0xfu&&!env->ExceptionCheck();
}
bool CalibrationInputs::LoadReceiver(JNIEnv*env,jvmtiEnv*ti,jclass type,std::string*error){
 std::string ignored;if(!error)error=&ignored;
 if(receiver_guard::PreparePublicBytecodes(ti)!=JVMTI_ERROR_NONE)
  return Fail(error,"calibration-input-receiver-public-bytecodes-capability-unavailable");
 auto dex_id=env->GetFieldID(type,"receiverDex","[B");
 if(!dex_id||env->ExceptionCheck())return false;
 Local bytes{env,env->GetObjectField(input_,dex_id)};
 if(!bytes.value||env->ExceptionCheck())return false;
 const auto length=env->GetArrayLength(static_cast<jbyteArray>(bytes.value));
 if(length<=0||length>64*1024*1024||env->ExceptionCheck())
  return Fail(error,"calibration-input-receiver-DEX-size-refused");
 receiver_dex_.resize(size_t(length));
 env->GetByteArrayRegion(static_cast<jbyteArray>(bytes.value),0,length,reinterpret_cast<jbyte*>(receiver_dex_.data()));
 if(env->ExceptionCheck())return false;
 receiver_guard::PublicInputs input(env);
 if(!input.Read(input_))return Fail(error,"calibration-input-receiver-public-samples-unavailable");
 receiver_object_=Own(env,input.object);if(!receiver_object_)return false;
 Local receiver_class{env,env->GetObjectClass(receiver_object_)};
 jobject loader=nullptr;
 if(!receiver_class.value||env->ExceptionCheck()||
    ti->GetClassLoader(static_cast<jclass>(receiver_class.value),&loader)!=JVMTI_ERROR_NONE){
  if(loader)env->DeleteLocalRef(loader);return false;
 }
 Local receiver_loader{env,loader};
 if(!loader||env->ExceptionCheck())return Fail(error,"calibration-input-receiver-isolated-loader-unavailable");
 // Public names/modifiers and bytecodes must match this separately copied
 // fixture DEX, not an ArtMethod, DexCache or guessed source declaration.
 const auto capture=[&](jobject mirror,jmethodID id,uint32_t flags,method_identity::Oracle*out,
                        jclass*klass,std::string*class_descriptor,std::vector<unsigned char>*instructions)->bool{
  jclass declaring=nullptr;
  if(ti->GetMethodDeclaringClass(id,&declaring)!=JVMTI_ERROR_NONE||!declaring){
   if(declaring)env->DeleteLocalRef(declaring);return false;
  }
  Local owner{env,declaring};jobject member_loader=nullptr;
  if(ti->GetClassLoader(declaring,&member_loader)!=JVMTI_ERROR_NONE){
   if(member_loader)env->DeleteLocalRef(member_loader);return false;
  }
  Local owned_loader{env,member_loader};
  if(env->ExceptionCheck()||!member_loader||!env->IsSameObject(loader,member_loader)||env->ExceptionCheck())
   return Fail(error,"calibration-input-receiver-member-loader-differs");
  Text signature{ti},name{ti},descriptor{ti};
  if(ti->GetClassSignature(declaring,&signature.value,nullptr)!=JVMTI_ERROR_NONE||!signature.value||
     ti->GetMethodName(id,&name.value,&descriptor.value,nullptr)!=JVMTI_ERROR_NONE||!name.value||!descriptor.value)
   return false;
  native_call_graph_incoming::Inventory inventory;
  if(!inventory.Capture(receiver_dex_,signature.value,error))return false;
  evolution::ClassSchema live;
  if(!inventory.Schema()||live_schema::ReadLiveSchema(ti,env,declaring,&live,error)!=JVMTI_ERROR_NONE||
     env->ExceptionCheck()||!body_only::SameDeclarations(live,*inventory.Schema()))
   return Fail(error,"calibration-input-receiver-complete-live-declarations-differ-from-DEX");
  const evolution::Member*found=nullptr;
  for(const auto&method:inventory.Methods())if(method.declaration.name==name.value&&method.declaration.descriptor==descriptor.value){
   if(found)return Fail(error,"calibration-input-receiver-method-DEX-identity-ambiguous");
   found=&method.declaration;
  }
  if(!found||(found->access&0xffffu)!=(flags&0xffffu))
   return Fail(error,"calibration-input-receiver-method-declaration-differs-from-DEX");
  if(instructions){
   const auto*code=inventory.InstructionBytes(found->dex_index);
   if(!code||code->empty())return Fail(error,"calibration-input-receiver-caller-DEX-body-unavailable");
   *instructions=*code;
  }
  auto root=Own(env,mirror);if(!root)return false;
  *klass=static_cast<jclass>(Own(env,declaring));if(!*klass)return false;
  *class_descriptor=signature.value;
  *out={root,found->dex_index,flags};return true;
 };
 for(size_t n=0;n<receiver_samples_.size();++n){
  receiver_guard::PublicSample sample(env,ti);
  if(!sample.Read(input.callers,input.targets,jint(n)))
   return Fail(error,"calibration-input-receiver-public-method-sample-unavailable");
  auto&owned=receiver_samples_[n];
  if(!capture(sample.caller,sample.caller_id,uint32_t(sample.caller_flags),&owned.caller,&owned.caller_class,&owned.caller_descriptor,&owned.instructions)||
     !capture(sample.target,sample.target_id,uint32_t(sample.target_flags),&owned.target,&owned.target_class,&owned.target_descriptor,nullptr))return false;
  if(size_t(sample.bytes)!=owned.instructions.size()||
     std::memcmp(sample.public_bytes,owned.instructions.data(),owned.instructions.size()))
   return Fail(error,"calibration-input-receiver-public-bytecodes-differ-from-DEX");
 }
 return !env->ExceptionCheck();
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
