#include "platform/common/native_interface_graph.h"
#include "platform/common/modern_field_metadata.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/common/runnable_executor.h"
#include "platform/abi/arm64/common/interface_table.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
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
bool TableBoundsFailure(std::string*error,const char*reason,void*table,int32_t length,
                        bool extent_known,size_t extent){
 // Final no-error readers must never construct diagnostic strings or allocate.
 if(!error)return false;
 const uint64_t required=length<0?0:uint64_t(kData)+uint64_t(length)*4;
 char message[320];
 std::snprintf(message,sizeof(message),
  "IfTable length/pair storage exceeds live mapped bounds [reason=%s table=%p length=%d pairWidth=%d requiredBytes=%llu extentKnown=%d mappedExtent=%zu]",
  reason,table,length,kPairWidth,static_cast<unsigned long long>(required),int(extent_known),extent);
 *error=message;return false;
}
const class_metadata::Layout& Layout(const modern_field_metadata::FieldMetadata& m){return m.StorageModel()==modern_field_metadata::Model::kSplit?class_metadata::kLegacy:class_metadata::k17;}
}
bool ReadView(const modern_field_metadata::FieldMetadata& metadata,void* klass,View* output,std::string* error,const modern_field_metadata::ReadContext* context){
 if(!output||!metadata.Calibrated()||!Mapped(klass,Layout(metadata).size,context))return Fail(error,"IfTable Class has no calibrated live source prefix");
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
 const char* invalid=length<0?"negative-length":length%kPairWidth?"odd-pair-length":length>1000000?"length-over-limit":nullptr;
 size_t table_extent=0;
 if(invalid){const bool known=error&&Extent(context,table,&table_extent);return TableBoundsFailure(error,invalid,table,length,known,table_extent);}
 const bool extent_known=Extent(context,table,&table_extent);
 if(!extent_known||table_extent<kData+size_t(length)*4)
  return TableBoundsFailure(error,extent_known?"truncated-pair-storage":"unmapped-pair-storage",table,length,extent_known,table_extent);
 for(int32_t i=0;i<length;i+=kPairWidth){void* interface=metadata.Reference(table,kData+size_t(i)*4);
  class_metadata::State state;size_t extent=0;
  if(!interface||!Extent(context,interface,&extent)||!class_metadata::ReadState(Layout(metadata),interface,extent,&state)||state.IsPrimitive()||!(state.access_flags&0x200u)||metadata.Reference(interface,0)!=meta_class)
   return Fail(error,"IfTable element lacks actual native interface Class identity");
  for(int32_t j=0;j<i;j+=kPairWidth)if(metadata.Reference(table,kData+size_t(j)*4)==interface)return Fail(error,"IfTable repeats a native interface identity");
 }
 View result;result.klass_=klass;result.table_=table;result.meta_class_=meta_class;result.array_class_=array_class;result.count_=size_t(length)/kPairWidth;*output=result;
 if(error)error->clear();return true;
}
bool InterfaceAt(const modern_field_metadata::FieldMetadata&metadata,const View&view,size_t index,void**output,const modern_field_metadata::ReadContext*context){
 if(!output||index>=view.count_||!metadata.Calibrated()||!Mapped(view.klass_,Layout(metadata).size,context)||metadata.Reference(view.klass_,0)!=view.meta_class_||metadata.Reference(view.klass_,kIfTable)!=view.table_||!Mapped(view.table_,kData+view.count_*kPairWidth*4,context))return false;
 int32_t length=0;std::memcpy(&length,static_cast<const char*>(view.table_)+kLength,4);
 if(length<0||size_t(length)!=view.count_*kPairWidth||metadata.Reference(view.table_,0)!=view.array_class_||!Mapped(view.array_class_,Layout(metadata).size,context)||metadata.Reference(view.array_class_,0)!=view.meta_class_)return false;
 void*interface=metadata.Reference(view.table_,kData+index*kPairWidth*4);class_metadata::State state;size_t extent=0;
 if(!interface||!Extent(context,interface,&extent)||!class_metadata::ReadState(Layout(metadata),interface,extent,&state)||state.IsPrimitive()||!(state.access_flags&0x200u)||metadata.Reference(interface,0)!=view.meta_class_)return false;
 *output=interface;return true;
}
namespace {
bool ReadTable(const modern_field_metadata::FieldMetadata&metadata,void*klass,std::vector<void*>*output,const std::vector<void*>*expected,std::string*error,const modern_field_metadata::ReadContext*context){
 View view;if((!output&&!expected)||!ReadView(metadata,klass,&view,error,context))return false;
 if(expected&&expected->size()!=view.Count())return Fail(error,"Live IfTable count differs from prepared rooted interface graph");
 if(output){output->clear();output->reserve(view.Count());}
 for(size_t i=0;i<view.Count();++i){void*interface=nullptr;if(!InterfaceAt(metadata,view,i,&interface,context))return Fail(error,"IfTable view changed inside its native read interval");
  if(expected&&(*expected)[i]!=interface)return Fail(error,"Live IfTable identity differs from prepared rooted interface graph");
  if(output)output->push_back(interface);
 }if(error)error->clear();return true;
}
}
bool Read(const modern_field_metadata::FieldMetadata& metadata,void* klass,std::vector<void*>* output,std::string* error){
 std::vector<void*> result;if(!output||!ReadTable(metadata,klass,&result,nullptr,error,nullptr))return false;*output=std::move(result);return true;
}
bool Matches(const modern_field_metadata::FieldMetadata& metadata,void* klass,const std::vector<void*>& expected,std::string* error){return ReadTable(metadata,klass,nullptr,&expected,error,nullptr);}

bool ReadInContext(const modern_field_metadata::FieldMetadata& metadata,void* klass,std::vector<void*>* output,std::string* error,const modern_field_metadata::ReadContext* context){std::vector<void*> result;if(!output||!ReadTable(metadata,klass,&result,nullptr,error,context))return false;*output=std::move(result);return true;}
bool MatchesInContext(const modern_field_metadata::FieldMetadata& metadata,void* klass,const std::vector<void*>& expected,std::string* error,const modern_field_metadata::ReadContext* context){return ReadTable(metadata,klass,nullptr,&expected,error,context);}

bool Calibrate(const runnable_jni::Context& context,jobject fixture,const modern_field_metadata::FieldMetadata& metadata,std::string* error,const StrongRoots*decoder){
 JNIEnv* env=context.env;if(!env||!fixture||env->ExceptionCheck())return Fail(error,"Missing public interface graph calibration scope");
 struct Roots {
  JNIEnv*env;const StrongRoots*decoder;std::vector<jobject>values,registered;
  ~Roots(){if(decoder)for(auto root:registered)decoder->forget(decoder->owner,root);for(auto value:values)env->DeleteGlobalRef(value);}
  jobject Add(jobject local){
   if(!local||env->ExceptionCheck())return nullptr;auto root=env->NewGlobalRef(local);
   if(!root||env->ExceptionCheck()){if(root)env->DeleteGlobalRef(root);return nullptr;}
   try{values.push_back(root);}catch(...){env->DeleteGlobalRef(root);throw;}return root;
  }
  bool Register(jobject root,std::string*error){
   if(!decoder)return true;registered.push_back(root);
   if(decoder->register_root(decoder->owner,env,root,error))return true;registered.pop_back();return false;
  }
 } roots{env,decoder,{},{}};
 if(decoder&&(!decoder->owner||!decoder->register_root||!decoder->forget||!decoder->read))return Fail(error,"Interface calibration native root decoder is not ready");
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
  if(!roots.Register(root,error))return false;
  for(auto owned:expected)if(!roots.Register(owned,error))return false;
  const auto decode=[&](jobject owned,void**out){return decoder?
      decoder->read(decoder->owner,context.self,owned,out):runnable_executor::DecodeStrongGlobal(context,owned,out);};
  void* native_root=nullptr;if(!decode(root,&native_root))return Fail(error,"Interface holder strong root failed to decode");
  std::vector<void*> actual;if(!Read(metadata,native_root,&actual,error))return false;
  if(actual.size()!=expected.size()||(root==positive&&expected.empty()))return Fail(error,"Native IfTable differs from independent complete public interface closure");
  for(auto owned:expected){void* native=nullptr;if(!decode(owned,&native)||std::count(actual.begin(),actual.end(),native)!=1)return Fail(error,"IfTable native interface identity differs from public Class");}
 }
 if(error)error->clear();return true;
}
}
