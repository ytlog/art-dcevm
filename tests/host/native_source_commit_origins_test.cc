// Share independent low-heap/JNI/named-ART fixtures with the composed provider.
// Its standalone main is not called; the assertions here use actual services.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#define main existing_provider_fixture
#include "native_source_commit_test.cc"
#undef main
#pragma clang diagnostic pop
int main(){
 InitJni();InitializeHeap();Hooks hooks;std::string error;
 std::array<modern_field_metadata::FieldOracle,4> oracle{};for(size_t i=0;i<4;++i)oracle[i]={reinterpret_cast<jobject>(&mirrors[i]),names[i],types[i],i==0?8u:1u};
 modern_field_metadata::FieldMetadata metadata;assert(metadata.Bind(hooks,&error));
 struct MetadataRequest{modern_field_metadata::FieldMetadata* service;const modern_field_metadata::FieldOracle* oracle;std::string* error;} calibration{&metadata,oracle.data(),&error};
 const auto calibrate=[](const runnable_jni::Context& context,void* data){auto& r=*static_cast<MetadataRequest*>(data);return r.service->Calibrate(context,reinterpret_cast<jobject>(old_class),"LFixture;",r.oracle,4,r.error);};
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),calibrate,&calibration)==runnable_jni::Result::kSuccess);assert(metadata.StorageModel()==modern_field_metadata::Model::kMerged);
 modern_field_storage::Service fields;
 if(!fields.Calibrate(hooks,&env,reinterpret_cast<jobject>(old_class),reinterpret_cast<jobject>(old_instance),metadata,oracle.data(),oracle.size(),&error)){std::cerr<<error<<'\n';return 1;}
 assert(fields.Calibrated()&&fields.CardTableOffset()==144&&Read32(old_instance,8)==31);
 native_cache::Service caches;assert(caches.Bind(hooks,&thread_list,&error)&&caches.Prepare(&error));
 method_identity::Oracle native{reinterpret_cast<jobject>(old_method),4,0x101};
 struct BindingRequest{Hooks* hooks;method_identity::Oracle native;std::string* error;std::unique_ptr<modern_methods::Binding> result;} binding_request{&hooks,native,&error,nullptr};
 const auto bind=[](const runnable_jni::Context& context,void* data){auto& r=*static_cast<BindingRequest*>(data);r.result=modern_methods::Service::BindOwned(*r.hooks,context,MethodLeaf,r.native,&binding_value,&linker,r.error);return bool(r.result);};
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),bind,&binding_request)==runnable_jni::Result::kSuccess);auto binding=std::move(binding_request.result);
 Put32(old_instance,4,0x01004567u);Put32(new_instance,4,0x20000001u);
 const int setup_jni=jni_calls;Stop(nullptr,nullptr,false);
 method_array::View before,after;const method_array::Boundaries boundaries{0,1,method_array::Order::ByFlags};
 assert(method_array::Read(method_metadata::kAndroid12Onward,reinterpret_cast<void*>(old_class->methods),40,boundaries,&before));assert(method_array::Read(method_metadata::kAndroid12Onward,reinterpret_cast<void*>(new_class->methods),40,boundaries,&after));
 auto methods=modern_methods::Service::PrepareTables(*binding,old_class,before,new_class,after,Retired,&error);assert(methods);
 source_commit::Plan plan{old_class,class_array,{old_class},{new_class},{old_instance},{new_instance},true};
 native_source_commit::Diagnostics diagnostics;
 native_source_commit::Dependencies deps;deps.diagnostics=&diagnostics;deps.metadata=&metadata;deps.fields=&fields;deps.caches=&caches;deps.reflective={ReflectiveReady,ReflectiveUpdate,nullptr,PublishRetirement};deps.observed.runtime=&runtime;deps.observed.instrumentation=&instrumentation;deps.observed.receivers.thread_list=&thread_list;deps.observed.receivers.jit=&jit;deps.observed.receivers.jit_code_cache=&jit_cache;
 auto prepare=[&](){return modern_source_commit::Prepare(hooks,deps,plan,{methods.get()},&error);};
 auto* alternate_array=heap.New(24);Put32(alternate_array,4,1u<<28);Put32(alternate_array,8,3);Put32(alternate_array,20,uint32_t(uintptr_t(new_class)));
 plan.class_root_origins={{alternate_array,2}};assert(prepare());
 plan.class_root_origins={{alternate_array,3}};assert(!prepare()&&retirement_publications==0&&mutations==0);
 plan.class_root_origins={{alternate_array,1}};assert(!prepare()&&retirement_publications==0&&mutations==0);
 plan.class_root_origins={{alternate_array,2}};Put32(alternate_array,8,2);assert(!prepare()&&retirement_publications==0&&mutations==0);Put32(alternate_array,8,3);
 auto service=prepare();assert(service&&service->Validate(plan));assert(source_commit::Run(plan,*service));Resume(nullptr);
 assert(retirement_publications==1&&jni_calls==setup_jni&&diagnostics.declaring_roots_verified&&diagnostics.cleared_threads==2);
 assert(Read32(new_instance,8)==31&&Read32(old_instance,8)==0&&Read32(new_class,128)==73);
 puts("Actual native provider: nonzero Holder origin, wrong index/root/length refusal, barrier rewrite and full calibrated commit PASS");
 return 0;
}
