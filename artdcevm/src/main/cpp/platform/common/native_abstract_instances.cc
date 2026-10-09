#include "platform/common/native_abstract_instances.h"
#include "platform/common/runnable_executor.h"
#include <algorithm>
#include <cstring>
namespace artdcevm::native_abstract_instances {
namespace {
constexpr size_t kVtable=offsetof(class_metadata::Class11,vtable),kComponent=offsetof(class_metadata::Class11,component_type);
static_assert(kVtable==offsetof(class_metadata::Class16,vtable)&&kVtable==offsetof(class_metadata::Class17,vtable));
static_assert(kComponent==offsetof(class_metadata::Class16,component_type)&&kComponent==offsetof(class_metadata::Class17,component_type));
// Fixed mirror/array.h Array<long>, length8, naturally aligned long payload16.
struct LongArrayPrefix {abi::arm64::ObjectPrefix object;int32_t length,padding;};
static_assert(sizeof(LongArrayPrefix)==16&&offsetof(LongArrayPrefix,length)==8);
bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
bool Mapped(void* address,size_t needed){size_t extent=0;return address&&method_identity::ReadableMappingExtent(address,&extent)&&extent>=needed;}
const class_metadata::Layout& Layout(const modern_field_metadata::FieldMetadata& metadata){return metadata.StorageModel()==modern_field_metadata::Model::kSplit?class_metadata::kLegacy:class_metadata::k17;}
bool State(const modern_field_metadata::FieldMetadata& metadata,void* klass,class_metadata::State* state){
 return Mapped(klass,Layout(metadata).size)&&class_metadata::ReadState(Layout(metadata),klass,Layout(metadata).size,state);
}
bool Read(const modern_field_metadata::FieldMetadata& metadata,method_identity::Model model,
          void* klass,bool* abstract,size_t concrete_prefix,void* expected=nullptr,bool* found=nullptr,std::string* error=nullptr){
 class_metadata::State state;if(!abstract||!metadata.Calibrated()||!State(metadata,klass,&state))return Fail(error,"Abstract vtable lacks a calibrated live Class declaration");
 *abstract=false;if(found)*found=false;
 if(state.IsPrimitive()||(state.access_flags&0x200u)||metadata.Reference(klass,kComponent))return Fail(error,"Abstract replacement is not an ordinary Class with external vtable");
 void* meta=metadata.Reference(klass,0);if(!Mapped(meta,Layout(metadata).size))return Fail(error,"Abstract holder lacks actual Class metatype");
 void* table=nullptr;size_t payload=0;int32_t length=0;
 if(state.access_flags&0x400u){
  table=metadata.Reference(klass,kVtable);if(!table)return true;
  if(!Mapped(table,sizeof(LongArrayPrefix)))return Fail(error,"Abstract vtable prefix is unreadable");
  void* array_class=metadata.Reference(table,0);class_metadata::State array_state;
  if(!State(metadata,array_class,&array_state)||metadata.Reference(array_class,0)!=meta)return Fail(error,"Abstract vtable array Class identity differs");
  void* component=metadata.Reference(array_class,kComponent);class_metadata::State component_state;
  if(!State(metadata,component,&component_state)||metadata.Reference(component,0)!=meta||(component_state.primitive_type&0xffffu)!=6u)return Fail(error,"Abstract external vtable is not the fixed primitive long[] declaration");
  std::memcpy(&length,static_cast<char*>(table)+offsetof(LongArrayPrefix,length),4);payload=sizeof(LongArrayPrefix);
 }else{
  // Fixed mirror/class.h: length at sizeof(Class), then aligned IMT pointer,
  // then embedded virtual entries. Calibrated complete concrete reflection
  // oracle below proves this source path independently of the external array.
  uint32_t class_size=0;const size_t size_offset=metadata.StorageModel()==modern_field_metadata::Model::kSplit?offsetof(class_metadata::Class11,class_size):offsetof(class_metadata::Class17,class_size);
  std::memcpy(&class_size,static_cast<char*>(klass)+size_offset,4);
  const size_t prefix=concrete_prefix;payload=((prefix+4+7)&~size_t(7))+8;table=klass;
  if(class_size<payload||!Mapped(klass,payload))return Fail(error,"Concrete embedded vtable lacks source Class extent");
  std::memcpy(&length,static_cast<char*>(klass)+prefix,4);
  if(length<0||size_t(length)>(class_size-payload)/8)return Fail(error,"Concrete embedded vtable exceeds source Class size");
 }
 if(length<0||length>1000000||!Mapped(table,payload+size_t(length)*8))return Fail(error,"Abstract vtable length exceeds mapped storage");
 const auto& method=model==method_identity::Model::kMethod11?method_metadata::kAndroid11:method_metadata::kAndroid12Onward;
 for(int32_t i=0;i<length;++i){uint64_t address=0;std::memcpy(&address,static_cast<char*>(table)+payload+size_t(i)*8,8);
  void* entry=reinterpret_cast<void*>(address);method_metadata::Identity identity;
  if(!entry||address%4||!Mapped(entry,method.size)||!method_metadata::ReadIdentity(method,entry,method.size,&identity))return Fail(error,"Abstract vtable method entry lacks calibrated readable identity");
  if(expected==entry&&found)*found=true;
  *abstract|=(identity.access_flags&0x400u)!=0;
 }
 if(error)error->clear();return true;
}
struct Probe {const modern_field_metadata::FieldMetadata* metadata;method_identity::Model model;size_t embedded_prefix;
 abi::arm64::android_common::FromReflectedMethod leaf;jobject classes[3],methods[3];jint flags[3];};
bool Observe(const runnable_jni::Context& context,void* raw){auto& p=*static_cast<Probe*>(raw);
 const abi::arm64::android_common::RunnableJniScope scope{context.self,context.env,context.vm};
 for(size_t i=0;i<3;++i){void* klass=nullptr;if(!runnable_executor::DecodeStrongGlobal(context,p.classes[i],&klass))return false;
  class_metadata::State state;if(!State(*p.metadata,klass,&state)||(state.access_flags&0x7611u)!=(uint32_t(p.flags[i])&0x7611u))return false;
  void* method=p.leaf(scope,p.methods[i]);const auto& layout=p.model==method_identity::Model::kMethod11?method_metadata::kAndroid11:method_metadata::kAndroid12Onward;
  method_metadata::Identity identity;if(!Mapped(method,layout.size)||!method_metadata::ReadIdentity(layout,method,layout.size,&identity)||bool(identity.access_flags&0x400u)!=(i==0))return false;
  bool abstract=false,found=false;if(!Read(*p.metadata,p.model,klass,&abstract,p.embedded_prefix,method,&found)||!found||abstract!=(i==0))return false;
 }
 return true;
}
}
bool Service::Calibrate(HookEngine& hooks,JNIEnv* env,const modern_field_metadata::FieldMetadata& metadata,
                        const method_identity::Calibration& model,std::string* error){
 if(!env||env->ExceptionCheck()||!metadata.Calibrated()||model.identities.size()<3||
    (model.model!=method_identity::Model::kMethod11&&model.model!=method_identity::Model::kMethod12Onward))return Fail(error,"Abstract vtable bootstrap lacks independent native Class/method identities");
 auto leaf=reinterpret_cast<abi::arm64::android_common::FromReflectedMethod>(hooks.FindSymbol("libart.so",abi::arm64::android_common::kFromReflectedMethod));
 if(!leaf)return Fail(error,"Abstract vtable public reflection decoder is unavailable");
 struct Roots{JNIEnv* env;std::vector<jobject> globals,locals;~Roots(){for(auto root:globals)env->DeleteGlobalRef(root);for(auto local:locals)if(local)env->DeleteLocalRef(local);}
  jobject Add(jobject local){if(!local||env->ExceptionCheck())return nullptr;locals.push_back(local);auto root=env->NewGlobalRef(local);if(root)globals.push_back(root);return root;}} roots{env,{},{}};
 jclass fixture=env->FindClass("com/github/ytlog/artdcevm/internal/AbstractVTableOracle");if(fixture)roots.locals.push_back(fixture);
 if(!fixture||env->ExceptionCheck())return Fail(error,"Public abstract fixture Class unavailable");
 auto factory=fixture?env->GetStaticMethodID(fixture,"create","()[Ljava/lang/Object;"):nullptr;
 if(!factory||env->ExceptionCheck())return Fail(error,"Public abstract original factory unavailable");
 auto array=factory?static_cast<jobjectArray>(env->CallStaticObjectMethod(fixture,factory)):nullptr;if(array)roots.locals.push_back(array);
 if(!array||env->ExceptionCheck())return Fail(error,"Public abstract/complete original vtable fixture unavailable");
 const jsize count=env->GetArrayLength(array);
 if(env->ExceptionCheck()||count!=6)return Fail(error,"Public abstract/complete original vtable fixture length differs");
 Probe probe{&metadata,model.model,0,leaf,{nullptr,nullptr,nullptr},{nullptr,nullptr,nullptr},{0,0,0}};
 jclass class_type=env->FindClass("java/lang/Class");if(class_type)roots.locals.push_back(class_type);
 if(!class_type||env->ExceptionCheck())return Fail(error,"Public Class modifier type unavailable");
 auto modifiers=class_type?env->GetMethodID(class_type,"getModifiers","()I"):nullptr;
 if(!modifiers||env->ExceptionCheck())return Fail(error,"Public abstract Class modifier oracle unavailable");
 for(size_t i=0;i<3;++i){probe.classes[i]=roots.Add(env->GetObjectArrayElement(array,jint(i)));
  if(!probe.classes[i]||env->ExceptionCheck())return Fail(error,"Abstract vtable public Class root unavailable");
  probe.methods[i]=roots.Add(env->GetObjectArrayElement(array,jint(i+3)));
  if(!probe.classes[i]||!probe.methods[i]||env->ExceptionCheck())return Fail(error,"Abstract vtable independent strong roots unavailable");
  probe.flags[i]=env->CallIntMethod(probe.classes[i],modifiers);if(env->ExceptionCheck())return Fail(error,"Public abstract Class modifiers raised exception");
 }
 size_t matched=0,selected=0;
 const std::vector<size_t> candidates=metadata.StorageModel()==modern_field_metadata::Model::kSplit?std::vector<size_t>{sizeof(class_metadata::Class11)}:std::vector<size_t>{sizeof(class_metadata::Class16),sizeof(class_metadata::Class17)};
 for(auto prefix:candidates){probe.embedded_prefix=prefix;if(runnable_executor::With(env,probe.methods[0],Observe,&probe)==runnable_jni::Result::kSuccess){++matched;selected=prefix;}if(env->ExceptionCheck())return Fail(error,"Native vtable oracle raised exception");}
 if(matched!=1)return Fail(error,"No unique fixed external/embedded vtable declaration matches public Class/Method identities");
 metadata_=&metadata;method_model_=model.model;embedded_prefix_=selected;if(error)error->clear();return true;
}
bool Service::Validate(const source_commit::Plan& plan,size_t pair,const modern_field_metadata::FieldMetadata& metadata,std::string* error)const{
 if(metadata_!=&metadata||!metadata.Calibrated()||pair>=plan.old_classes.size()||plan.old_classes.size()!=plan.new_classes.size()||plan.old_instances.size()!=plan.new_instances.size())
  return Fail(error,"Existing-instance abstract validation lacks the same calibrated native owner and complete Holder roots");

 for(size_t i=0;i<plan.old_instances.size();++i){void* instance=plan.old_instances[i];void* replacement=plan.new_instances[i];
  if(!Mapped(instance,sizeof(abi::arm64::ObjectPrefix))||!Mapped(replacement,sizeof(abi::arm64::ObjectPrefix)))return Fail(error,"Existing-instance root is missing or unreadable");
  void* klass=metadata.Reference(instance,0);auto old=std::find(plan.old_classes.begin(),plan.old_classes.end(),klass);
  if(old==plan.old_classes.end())return Fail(error,"Existing-instance actual Class is outside paired Holder proof");
  size_t k=size_t(old-plan.old_classes.begin());if(metadata.Reference(replacement,0)!=plan.new_classes[k])return Fail(error,"Replacement-instance Class differs from exact paired Holder");
 }
 for(size_t k=0;k<plan.old_classes.size();++k){
  if(std::none_of(plan.old_instances.begin(),plan.old_instances.end(),[&](void* instance){return metadata.Reference(instance,0)==plan.old_classes[k];}))continue;
  bool unresolved=false;if(!Read(metadata,method_model_,plan.new_classes[k],&unresolved,embedded_prefix_,nullptr,nullptr,error))return false;
  if(unresolved)return Fail(error,"An actual migrated instance would inherit an abstract method");
 }
 if(error)error->clear();return true;
}
}
