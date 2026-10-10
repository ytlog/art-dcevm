#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "native_commit_fixture.inc"
#pragma clang diagnostic pop
#include "platform/common/native_typed_heap_walk.h"
namespace {
using namespace native_typed_heap_walk;
using Ctor=void(*)(void*,void*,void*,size_t*,bool);using Verify=bool(*)(const void*,void*,void*,uint32_t);
Ctor ctor_hook=nullptr;Verify ref_hook=nullptr;void* fixture_array=nullptr;
void* expected_heap=&heap;bool hide_second=false,hide_roots=false,hide_fields=false,missing_ctor=false,wrong_receiver=false;size_t original_refs=0,original_ctors=0,walks=0;
void OriginalCtor(void*,void* self,void* actual_heap,size_t* count,bool referents){assert(stopped&&self==thread.data()&&actual_heap==expected_heap&&count&&referents);++original_ctors;}
bool OriginalReference(const void*,void*,void*,uint32_t){assert(stopped);++original_refs;return true;}
size_t OriginalHeap(void* receiver,bool referents){assert(stopped&&receiver==expected_heap&&referents);++walks;size_t failures=0;uintptr_t visitor=0;
 if(!missing_ctor)ctor_hook(&visitor,current,receiver,&failures,true);
 const auto visitor_address=wrong_receiver?static_cast<const void*>(&failures):static_cast<const void*>(&visitor);
 ref_hook(visitor_address,fixture_array,old_instance,12);if(!hide_second)ref_hook(visitor_address,fixture_array,new_instance,16);
 if(!hide_fields){ref_hook(visitor_address,old_instance,old_instance,12);ref_hook(visitor_address,old_class,new_instance,128);}
 if(!hide_roots){ref_hook(visitor_address,nullptr,old_class,0);ref_hook(visitor_address,nullptr,fixture_array,0);ref_hook(visitor_address,nullptr,old_instance,0);ref_hook(visitor_address,nullptr,new_instance,0);}
 return failures;
}
class TypedHooks:public Hooks {public:
 void* FindSymbol(const char* lib,const char* name)override{
  if(!std::strcmp(name,kVerifyHeap))return reinterpret_cast<void*>(OriginalHeap);
  if(!std::strcmp(name,kVisitorCtor))return reinterpret_cast<void*>(OriginalCtor);
  if(!std::strcmp(name,kVerifyReference))return reinterpret_cast<void*>(OriginalReference);
  return Hooks::FindSymbol(lib,name);
 }
 void* Install(void* target,void* replacement,void** original,std::string*)override{
  *original=target;if(target==reinterpret_cast<void*>(OriginalCtor))ctor_hook=reinterpret_cast<Ctor>(replacement);
  else{assert(target==reinterpret_cast<void*>(OriginalReference));ref_hook=reinterpret_cast<Verify>(replacement);}return target;
 }
 bool Remove(void*,std::string*)override{return true;}
};
jsize ArrayLength(JNIEnv*,jarray a){Jni();assert(a==fixture_array);return 2;}
jobject ArrayGet(JNIEnv*,jobjectArray a,jsize i){Jni();assert(a==fixture_array&&i>=0&&i<2);return reinterpret_cast<jobject>(i?new_instance:old_instance);}
struct CountingSink final:Sink {size_t seen=0;bool deny=false;bool Reference(void*,void*,uint32_t)noexcept override{++seen;return !deny;}};
}
int main(){InitJni();InitializeHeap();fixture_array=heap.New(20);Put32(fixture_array,8,2);Put32(fixture_array,12,uint32_t(uintptr_t(old_instance)));Put32(fixture_array,16,uint32_t(uintptr_t(new_instance)));
 types[0]="Ljava/lang/Object;";Put32(old_instance,12,uint32_t(uintptr_t(old_instance)));Put32(old_class,128,uint32_t(uintptr_t(new_instance)));
 table.GetStaticObjectField=[](JNIEnv* e,jclass o,jfieldID f){return GetObject(e,o,f);};
 table.GetArrayLength=ArrayLength;table.GetObjectArrayElement=ArrayGet;TypedHooks hooks;std::string error;exclusive_owned_roots::Service exclusive;assert(exclusive_owned_roots::Service::Bind(hooks,&exclusive,&error));
 Service service;assert(service.Bind(hooks,expected_heap,Current,&error));CountingSink unproven;assert(!service.Walk(current,unproven)&&!unproven.seen);
 const auto array=reinterpret_cast<jobjectArray>(fixture_array);const auto left=reinterpret_cast<jobject>(old_instance),right=reinterpret_cast<jobject>(new_instance);
 hide_second=true;assert(!service.Calibrate(&env,array,left,right,exclusive,&error)&&!service.Calibrated());hide_second=false;
 hide_roots=true;assert(!service.Calibrate(&env,array,left,right,exclusive,&error)&&!service.Calibrated());hide_roots=false;
 assert(service.Calibrate(&env,array,left,right,exclusive,&error)&&!service.Calibrated());
 modern_field_metadata::FieldMetadata metadata;assert(metadata.Bind(hooks,&error));
 std::array<modern_field_metadata::FieldOracle,4> oracle{};for(size_t i=0;i<4;++i)oracle[i]={reinterpret_cast<jobject>(&mirrors[i]),names[i],types[i],i==0?8u:1u};
 struct FieldRequest{Service* service;modern_field_metadata::FieldMetadata* metadata;TypedHooks* hooks;exclusive_owned_roots::Service* exclusive;std::array<modern_field_metadata::FieldOracle,4>* oracle;std::string* error;bool ready;} field_request{&service,&metadata,&hooks,&exclusive,&oracle,&error,false};
 auto metadata_cal=[](const runnable_jni::Context& c,void* raw){auto&r=*static_cast<FieldRequest*>(raw);return r.metadata->Calibrate(c,reinterpret_cast<jobject>(old_class),"LFixture;",r.oracle->data(),4,r.error);};
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),metadata_cal,&field_request)==runnable_jni::Result::kSuccess);
 auto field_cal=[&](){return service.CalibrateFields(hooks,&env,reinterpret_cast<jobject>(old_instance),reinterpret_cast<jclass>(old_class),reinterpret_cast<jobject>(&mirrors[2]),reinterpret_cast<jobject>(&mirrors[0]),left,right,metadata,exclusive,&error);};
 hide_fields=true;assert(!field_cal()&&!service.Calibrated());hide_fields=false;
 if(!field_cal()){std::cerr<<error<<"\n";std::abort();}assert(service.Calibrated());
 struct Request{Service* service;CountingSink* sink;bool accepted;} request{&service,&unproven,true};
 const auto callback=[](const exclusive_owned_roots::Context& c,void* raw){auto&r=*static_cast<Request*>(raw);assert(r.service->Walk(c.self,*r.sink)==r.accepted);return true;};
 const jobject root[]{array};const auto jni_before=jni_calls;assert(exclusive.With(&env,root,1,callback,&request,&error));assert(unproven.seen==8&&jni_calls>jni_before);
 unproven.deny=true;request.accepted=false;assert(exclusive.With(&env,root,1,callback,&request,&error));unproven.deny=false;
 assert(!service.Walk(foreign_thread.data(),unproven));
 missing_ctor=true;assert(exclusive.With(&env,root,1,callback,&request,&error));missing_ctor=false;
 wrong_receiver=true;assert(exclusive.With(&env,root,1,callback,&request,&error));wrong_receiver=false;
 assert(original_refs&&original_ctors&&walks&&!stopped);
 std::cout<<"Actual named whole-heap episode: public independent instance/static/array/reference/root oracles, original forwarding, missing coverage/constructor, wrong self/receiver and sink refusal PASS\n";
}
