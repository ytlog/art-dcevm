#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "native_commit_fixture.inc"
#pragma clang diagnostic pop
#include "platform/common/native_abstract_instances.h"
#include <new>
static bool forbid_vtable_allocation=false;
void* operator new(std::size_t n){if(forbid_vtable_allocation)throw std::bad_alloc();if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
namespace {
void* incomplete=nullptr;void* complete=nullptr;void* long_array_class=nullptr;void* primitive_long=nullptr;
void* embedded_complete=nullptr;void* embedded_method=nullptr;
void* public_array=nullptr;void* abstract_method=nullptr;void* concrete_method=nullptr;
int modifiers_id,factory_id,fixture_id;bool abstract_pending=false,broken_factory=false,wrong_public_flags=false,wrong_method=false;
jboolean AbstractException(JNIEnv*){Jni();return abstract_pending;}
jclass AbstractFind(JNIEnv* e,const char* name){if(!std::strcmp(name,"com/github/ytlog/artdcevm/internal/AbstractVTableOracle")){Jni();return reinterpret_cast<jclass>(&fixture_id);}return Find(e,name);}
jmethodID AbstractMethod(JNIEnv* e,jclass klass,const char* name,const char* signature){if(!std::strcmp(name,"getModifiers")){Jni();return reinterpret_cast<jmethodID>(&modifiers_id);}return MethodId(e,klass,name,signature);}
jmethodID AbstractFactoryId(JNIEnv*,jclass,const char* name,const char* signature){Jni();assert(std::string(name)=="create"&&std::string(signature)=="()[Ljava/lang/Object;");return reinterpret_cast<jmethodID>(&factory_id);}
jobject AbstractFactory(JNIEnv*,jclass,jmethodID,va_list){Jni();if(broken_factory){abstract_pending=true;return nullptr;}return reinterpret_cast<jobject>(public_array);}
jsize AbstractLength(JNIEnv*,jarray array){Jni();return jsize(Read32(array,8));}
jobject AbstractElement(JNIEnv*,jobjectArray array,jsize index){Jni();assert(array==public_array&&index>=0&&index<6);return reinterpret_cast<jobject>(uintptr_t(Read32(array,12+4*size_t(index))));}
jint AbstractModifiers(JNIEnv*,jobject owner,jmethodID,va_list){Jni();assert(owner==incomplete||owner==complete||owner==embedded_complete);return wrong_public_flags?1:(owner==embedded_complete?1:0x401);}
void* AbstractLeaf(const abi::arm64::android_common::RunnableJniScope& scope,jobject method){assert(scope.self==current&&Read32(current)==0&&!stopped);return wrong_method?static_cast<void*>(old_method):static_cast<void*>(method);}
class AbstractHooks:public Hooks {public:bool missing_leaf=false;void* FindSymbol(const char* lib,const char* name)override{
 if(std::string(name)==abi::arm64::android_common::kFromReflectedMethod)return missing_leaf?nullptr:reinterpret_cast<void*>(AbstractLeaf);
 return Hooks::FindSymbol(lib,name);
}};
void* Vtable(void* method){void* table=heap.New(24);Put32(table,0,uint32_t(uintptr_t(long_array_class)));Put32(table,8,1);uint64_t value=uintptr_t(method);std::memcpy(static_cast<char*>(table)+16,&value,8);return table;}
void Fixtures(){
 incomplete=heap.New(160);complete=heap.New(160);long_array_class=heap.New(160);primitive_long=heap.New(160);
 for(auto klass:{incomplete,complete,long_array_class,primitive_long})static_cast<class_metadata::Class17*>(klass)->object.klass=uint32_t(uintptr_t(meta_class));
 for(auto klass:{incomplete,complete})static_cast<class_metadata::Class17*>(klass)->access_flags=0x401;
 static_cast<class_metadata::Class17*>(long_array_class)->component_type=uint32_t(uintptr_t(primitive_long));
 static_cast<class_metadata::Class17*>(primitive_long)->primitive_type=6;
 abstract_method=heap.New(40);concrete_method=heap.New(40);Put32(abstract_method,4,0x401);Put32(concrete_method,4,1);
 static_cast<class_metadata::Class17*>(incomplete)->vtable=uint32_t(uintptr_t(Vtable(abstract_method)));
 static_cast<class_metadata::Class17*>(complete)->vtable=uint32_t(uintptr_t(Vtable(concrete_method)));
 embedded_complete=heap.New(160);embedded_method=heap.New(40);Put32(embedded_method,4,1);
 static_cast<class_metadata::Class17*>(embedded_complete)->object.klass=uint32_t(uintptr_t(meta_class));static_cast<class_metadata::Class17*>(embedded_complete)->access_flags=1;static_cast<class_metadata::Class17*>(embedded_complete)->class_size=160;
 Put32(embedded_complete,sizeof(class_metadata::Class17),1);const uint64_t entry=uintptr_t(embedded_method);std::memcpy(static_cast<char*>(embedded_complete)+120,&entry,8);
 public_array=heap.New(36);Put32(public_array,8,6);size_t i=0;for(auto root:{incomplete,complete,embedded_complete,abstract_method,concrete_method,embedded_method})Put32(public_array,12+4*i++,uint32_t(uintptr_t(root)));
 table.ExceptionCheck=AbstractException;table.FindClass=AbstractFind;table.GetMethodID=AbstractMethod;table.GetStaticMethodID=AbstractFactoryId;
 table.CallStaticObjectMethodV=AbstractFactory;table.GetArrayLength=AbstractLength;table.GetObjectArrayElement=AbstractElement;table.CallIntMethodV=AbstractModifiers;
}
}
int main(){
 InitJni();InitializeHeap();AbstractHooks hooks;std::string error;
 std::array<modern_field_metadata::FieldOracle,4> oracles{};for(size_t i=0;i<4;++i)oracles[i]={reinterpret_cast<jobject>(&mirrors[i]),names[i],types[i],i==0?8u:1u};
 modern_field_metadata::FieldMetadata metadata;assert(metadata.Bind(hooks,&error));
 struct Request{modern_field_metadata::FieldMetadata* m;const modern_field_metadata::FieldOracle* o;std::string* e;} request{&metadata,oracles.data(),&error};
 const auto calibrate=[](const runnable_jni::Context& scope,void* opaque){auto& r=*static_cast<Request*>(opaque);return r.m->Calibrate(scope,reinterpret_cast<jobject>(old_class),"LFixture;",r.o,4,r.e);};
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),calibrate,&request)==runnable_jni::Result::kSuccess);
 Fixtures();method_identity::Calibration model{};model.model=method_identity::Model::kMethod12Onward;model.identities={{0x101,1},{1,2},{9,3}};
 native_abstract_instances::Service service;hooks.missing_leaf=true;assert(!service.Calibrate(hooks,&env,metadata,model,&error)&&!service.Ready());hooks.missing_leaf=false;
 wrong_public_flags=true;assert(!service.Calibrate(hooks,&env,metadata,model,&error)&&!service.Ready());wrong_public_flags=false;
 wrong_method=true;assert(!service.Calibrate(hooks,&env,metadata,model,&error)&&!service.Ready());wrong_method=false;
 broken_factory=true;assert(!service.Calibrate(hooks,&env,metadata,model,&error)&&abstract_pending&&!service.Ready());broken_factory=false;abstract_pending=false;
 assert(service.Calibrate(hooks,&env,metadata,model,&error)&&service.Ready());
 source_commit::Plan plan{old_class,class_array,{old_class},{incomplete},{old_instance},{new_instance},true};Put32(new_instance,0,uint32_t(uintptr_t(incomplete)));
 const int before_jni=jni_calls;Stop(nullptr,nullptr,false);
 assert(!service.Validate(plan,0,metadata,&error)&&error=="An actual migrated instance would inherit an abstract method");
 plan.old_instances.clear();plan.new_instances.clear();assert(service.Validate(plan,0,metadata,&error));
 plan.old_instances={old_instance};plan.new_instances={new_instance};plan.new_classes[0]=complete;Put32(new_instance,0,uint32_t(uintptr_t(complete)));assert(service.Validate(plan,0,metadata,&error));
 forbid_vtable_allocation=true;assert(service.Validate(plan,0,metadata,nullptr));
 plan.new_classes[0]=incomplete;Put32(new_instance,0,uint32_t(uintptr_t(incomplete)));assert(!service.Validate(plan,0,metadata,nullptr));
 plan.new_classes[0]=complete;Put32(new_instance,0,uint32_t(uintptr_t(complete)));assert(service.Validate(plan,0,metadata,nullptr));forbid_vtable_allocation=false;
 // Concrete Class flags do not grant success: its real embedded dispatch
 // entries must also be complete for every actual corresponding instance.
 plan.new_classes[0]=embedded_complete;Put32(new_instance,0,uint32_t(uintptr_t(embedded_complete)));assert(service.Validate(plan,0,metadata,&error));
 const uint64_t missing=uintptr_t(abstract_method),valid=uintptr_t(embedded_method);std::memcpy(static_cast<char*>(embedded_complete)+120,&missing,8);
 assert(!service.Validate(plan,0,metadata,&error)&&error=="An actual migrated instance would inherit an abstract method");
 std::memcpy(static_cast<char*>(embedded_complete)+120,&valid,8);assert(service.Validate(plan,0,metadata,&error));
 // Missing counterpart ownership cannot masquerade as a harmless subclass.
 Put32(new_instance,0,uint32_t(uintptr_t(complete)));assert(!service.Validate(plan,0,metadata,&error));
 plan.new_classes[0]=incomplete;Put32(new_instance,0,uint32_t(uintptr_t(incomplete)));
 auto* klass=static_cast<class_metadata::Class17*>(incomplete);const auto saved=klass->vtable;klass->vtable=0;assert(service.Validate(plan,0,metadata,&error));klass->vtable=saved;
 auto* vtable=reinterpret_cast<void*>(uintptr_t(saved));Put32(vtable,8,uint32_t(-1));assert(!service.Validate(plan,0,metadata,&error));Put32(vtable,8,1);
 const auto component=static_cast<class_metadata::Class17*>(primitive_long)->primitive_type;static_cast<class_metadata::Class17*>(primitive_long)->primitive_type=5;assert(!service.Validate(plan,0,metadata,&error));static_cast<class_metadata::Class17*>(primitive_long)->primitive_type=component;
 const uint64_t bad_entry=1;std::memcpy(static_cast<char*>(vtable)+16,&bad_entry,8);assert(!service.Validate(plan,0,metadata,&error));const uint64_t restored=uintptr_t(abstract_method);std::memcpy(static_cast<char*>(vtable)+16,&restored,8);
 plan.old_instances[0]=nullptr;assert(!service.Validate(plan,0,metadata,&error));
 assert(jni_calls==before_jni);Resume(nullptr);
 std::cout<<"Actual native abstract vtable: public incomplete/complete identities, named leaf refusal, original JNI throw preservation, actual Holder direct/subclass instances and bounded long array/method negatives passed\n";
}
