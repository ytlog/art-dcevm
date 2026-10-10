#include "platform/common/native_interface_graph.h"
#include "platform/common/modern_field_metadata.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/common/runnable_executor.h"
#include "platform/abi/arm64/common/interface_table.h"
#include <algorithm>
#include <cstring>
namespace artdcevm::native_interface_graph {
namespace {
constexpr size_t kIfTable=offsetof(class_metadata::Class11,iftable);
constexpr size_t kLength=abi::arm64::android_common::kIfTableLength;
constexpr size_t kData=abi::arm64::android_common::kIfTableData;
constexpr int32_t kPairWidth=abi::arm64::android_common::kIfTablePairWidth;
static_assert(kIfTable==offsetof(class_metadata::Class16,iftable)&&kIfTable==offsetof(class_metadata::Class17,iftable));
bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
bool Extent(const modern_field_metadata::ReadContext* c,const void*p,size_t*out){return c?c->extent&&c->extent(c->owner,p,out):method_identity::ReadableMappingExtent(p,out);}
bool Mapped(void* p,size_t bytes,const modern_field_metadata::ReadContext*c){size_t extent=0;return p&&Extent(c,p,&extent)&&extent>=bytes;}
const class_metadata::Layout& Layout(const modern_field_metadata::FieldMetadata& m){return m.StorageModel()==modern_field_metadata::Model::kSplit?class_metadata::kLegacy:class_metadata::k17;}
}
namespace {
bool ReadTable(const modern_field_metadata::FieldMetadata& metadata,void* klass,std::vector<void*>* output,const std::vector<void*>* expected,std::string* error,const modern_field_metadata::ReadContext* context){
 if((!output&&!expected)||!metadata.Calibrated()||!Mapped(klass,Layout(metadata).size,context))return Fail(error,"IfTable Class has no calibrated live source prefix");
 void* meta_class=metadata.Reference(klass,0);if(!Mapped(meta_class,Layout(metadata).size,context))return Fail(error,"IfTable holder is not a live Class object");
 void* table=metadata.Reference(klass,kIfTable);
 if(!Mapped(table,kData,context))return Fail(error,"IfTable is null or has no readable ObjectArray prefix");
 void* array_class=metadata.Reference(table,0);
 if(!Mapped(array_class,Layout(metadata).size,context)||metadata.Reference(array_class,0)!=meta_class)return Fail(error,"IfTable ObjectArray Class identity is unreadable");
 void* component=metadata.Reference(array_class,offsetof(class_metadata::Class11,component_type));
 field_copy::CopyClassView component_identity;
 if(!Mapped(component,Layout(metadata).size,context)||metadata.Reference(component,0)!=meta_class||!metadata.ClassIdentity(component,&component_identity,error,context)||!component_identity.is_object||component_identity.loader)
  return Fail(error,"IfTable is not the proven bootstrap Object[] declaration");
 int32_t length=0;std::memcpy(&length,static_cast<char*>(table)+kLength,4);
 if(length<0||(length%kPairWidth)||length>1000000||!Mapped(table,kData+size_t(length)*4,context))return Fail(error,"IfTable length/pair storage exceeds live mapped bounds");
 if(expected&&expected->size()!=size_t(length)/kPairWidth)return Fail(error,"Live IfTable count differs from prepared rooted interface graph");
 if(output){output->clear();output->reserve(size_t(length)/kPairWidth);}
 for(int32_t i=0;i<length;i+=kPairWidth){void* interface=metadata.Reference(table,kData+size_t(i)*4);
  class_metadata::State state;size_t extent=0;
  if(!interface||!Extent(context,interface,&extent)||!class_metadata::ReadState(Layout(metadata),interface,extent,&state)||state.IsPrimitive()||!(state.access_flags&0x200u)||metadata.Reference(interface,0)!=meta_class)
   return Fail(error,"IfTable element lacks actual native interface Class identity");
  for(int32_t j=0;j<i;j+=kPairWidth)if(metadata.Reference(table,kData+size_t(j)*4)==interface)return Fail(error,"IfTable repeats a native interface identity");
  if(expected&&(*expected)[size_t(i)/kPairWidth]!=interface)return Fail(error,"Live IfTable identity differs from prepared rooted interface graph");
  if(output)output->push_back(interface);
 }
 if(error)error->clear();return true;
}
}
bool Read(const modern_field_metadata::FieldMetadata& metadata,void* klass,std::vector<void*>* output,std::string* error){
 std::vector<void*> result;if(!output||!ReadTable(metadata,klass,&result,nullptr,error,nullptr))return false;*output=std::move(result);return true;
}
bool Matches(const modern_field_metadata::FieldMetadata& metadata,void* klass,const std::vector<void*>& expected,std::string* error){return ReadTable(metadata,klass,nullptr,&expected,error,nullptr);}

bool ReadInContext(const modern_field_metadata::FieldMetadata& metadata,void* klass,std::vector<void*>* output,std::string* error,const modern_field_metadata::ReadContext* context){std::vector<void*> result;if(!output||!ReadTable(metadata,klass,&result,nullptr,error,context))return false;*output=std::move(result);return true;}
bool MatchesInContext(const modern_field_metadata::FieldMetadata& metadata,void* klass,const std::vector<void*>& expected,std::string* error,const modern_field_metadata::ReadContext* context){return ReadTable(metadata,klass,nullptr,&expected,error,context);}

bool Calibrate(const runnable_jni::Context& context,jobject fixture,const modern_field_metadata::FieldMetadata& metadata,std::string* error){
 JNIEnv* env=context.env;if(!env||!fixture||env->ExceptionCheck())return Fail(error,"Missing public interface graph calibration scope");
 struct Roots{JNIEnv* env;std::vector<jobject> values;~Roots(){for(auto value:values)env->DeleteGlobalRef(value);}jobject Add(jobject local){if(!local||env->ExceptionCheck())return nullptr;auto root=env->NewGlobalRef(local);if(root)values.push_back(root);return env->ExceptionCheck()?nullptr:root;}} roots{env,{}};
 jclass class_type=env->FindClass("java/lang/Class");
 if(!class_type||env->ExceptionCheck()){if(class_type)env->DeleteLocalRef(class_type);return Fail(error,"Public Class lookup failed");}
 jmethodID interfaces=env->GetMethodID(class_type,"getInterfaces","()[Ljava/lang/Class;");
 if(!interfaces||env->ExceptionCheck()){env->DeleteLocalRef(class_type);return Fail(error,"Public interface method lookup failed");}
 jmethodID parent=env->GetMethodID(class_type,"getSuperclass","()Ljava/lang/Class;");
 env->DeleteLocalRef(class_type);
 if(!parent||env->ExceptionCheck())return Fail(error,"Public superclass method lookup failed");
 jclass positive_local=env->FindClass("java/util/ArrayList");
 if(!positive_local||env->ExceptionCheck()){if(positive_local)env->DeleteLocalRef(positive_local);return Fail(error,"Public positive Class lookup failed");}
 jobject positive=roots.Add(positive_local);env->DeleteLocalRef(positive_local);
 if(!positive)return Fail(error,"Public positive Class root unavailable");
 for(auto root:{positive,fixture}){
  std::vector<jobject> pending{root},visited,expected;
  for(size_t cursor=0;cursor<pending.size();++cursor){jobject current=pending[cursor];
   if(pending.size()>4096)return Fail(error,"Public interface closure exceeds verified bound");
   if(std::any_of(visited.begin(),visited.end(),[&](auto c){return env->IsSameObject(c,current);}))continue;visited.push_back(current);
   auto array=static_cast<jobjectArray>(env->CallObjectMethod(current,interfaces));
   if(!array||env->ExceptionCheck()){if(array)env->DeleteLocalRef(array);return Fail(error,"Public declared interface array unavailable");}
   const jsize count=env->GetArrayLength(array);if(env->ExceptionCheck()||count<0||count>4096){env->DeleteLocalRef(array);return Fail(error,"Public interface array has invalid extent");}
   for(jsize i=0;i<count;++i){auto local=env->GetObjectArrayElement(array,i);
    if(!local||env->ExceptionCheck()){if(local)env->DeleteLocalRef(local);env->DeleteLocalRef(array);return Fail(error,"Public interface element unavailable");}
    auto owned=roots.Add(local);env->DeleteLocalRef(local);
    if(!owned||env->ExceptionCheck()){env->DeleteLocalRef(array);return Fail(error,"Public interface Class root unavailable");}
    if(std::none_of(expected.begin(),expected.end(),[&](auto c){return env->IsSameObject(c,owned);}))expected.push_back(owned);pending.push_back(owned);
   }
   env->DeleteLocalRef(array);auto local_parent=env->CallObjectMethod(current,parent);
   if(env->ExceptionCheck()){if(local_parent)env->DeleteLocalRef(local_parent);return Fail(error,"Public superclass closure unavailable");}
   if(local_parent){auto owned=roots.Add(local_parent);env->DeleteLocalRef(local_parent);if(!owned)return Fail(error,"Public superclass root unavailable");pending.push_back(owned);}
  }
  void* native_root=nullptr;if(!runnable_executor::DecodeStrongGlobal(context,root,&native_root))return Fail(error,"Interface holder strong root failed to decode");
  std::vector<void*> actual;if(!Read(metadata,native_root,&actual,error))return false;
  if(actual.size()!=expected.size()||(root==positive&&expected.empty()))return Fail(error,"Native IfTable differs from independent complete public interface closure");
  for(auto owned:expected){void* native=nullptr;if(!runnable_executor::DecodeStrongGlobal(context,owned,&native)||std::count(actual.begin(),actual.end(),native)!=1)return Fail(error,"IfTable native interface identity differs from public Class");}
 }
 if(error)error->clear();return true;
}
}
