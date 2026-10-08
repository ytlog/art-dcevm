#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "native_commit_fixture.inc"
#pragma clang diagnostic pop
#include "platform/common/native_member_provider.h"
#include "platform/common/dex_mapping_abi.h"
#include "platform/abi/arm64/android11/dex_cache.h"
#include "platform/common/native_reflective_value_abi.h"
#include <fstream>
#include <iterator>
namespace {
int retired_methods=0,retired_fields=0;bool mh_guard=false,vh_guard=false,visit_mh=false,visit_vh=false,field_mh_guard=false,visit_field_mh=false;
void RetireMethod(void*,void*){++retired_methods;}void RetireField(void*,void*){++retired_fields;}
bool NotRetired(void*,void*){return false;}bool Mh(void*){return mh_guard;}bool Vh(void*){return vh_guard;}bool FieldMh(void*){return field_mh_guard;}
bool SameIndex(void* a,void* b){return static_cast<abi::arm64::android12::Method12*>(a)->dex_method_index==static_cast<abi::arm64::android12::Method12*>(b)->dex_method_index;}
abi::arm64::Field* ancestor_cached_field=nullptr;art::ArtField* resolved_field_slot=nullptr;art::ArtMethod* resolved_method_slot=nullptr;bool visit_resolution=false;
const char* ResolutionName(void* field){return field==ancestor_cached_field?names[0]:FieldName(field);}
const char* ResolutionType(void* field){return field==ancestor_cached_field?types[0]:FieldType(field);}
void* forbidden_descriptor_class=nullptr;int forbidden_descriptor_calls=0;
const char* ResolutionDescriptor(void* klass,void* storage){if(klass==forbidden_descriptor_class){++forbidden_descriptor_calls;assert(false&&"Unrelated cache Class descriptor is not an eligibility prerequisite");}return Descriptor(klass,storage);}
std::array<executable_mirror::Declaration,3> executable{};
void* Leaf(const abi::arm64::android_common::RunnableJniScope&,jobject object){return reinterpret_cast<void*>(uintptr_t(reinterpret_cast<executable_mirror::Declaration*>(object)->art_method));}
bool DecodeMirror(const runnable_jni::Context&,jobject object,void** out){*out=object;return true;}
void Visit(void* receiver,art::ReflectiveValueVisitor* visitor){
 assert(receiver==&runtime&&stopped);
 alignas(8) unsigned char info[24]{};
 if(visit_resolution){Put32(info,8,8);resolved_method_slot=visitor->VisitMethod(resolved_method_slot,*reinterpret_cast<const art::ReflectionSourceInfo*>(info));Put32(info,8,9);resolved_field_slot=visitor->VisitField(resolved_field_slot,*reinterpret_cast<const art::ReflectionSourceInfo*>(info));}
 if(visit_mh){Put32(info,8,3);assert(visitor->VisitMethod(reinterpret_cast<art::ArtMethod*>(old_method),*reinterpret_cast<const art::ReflectionSourceInfo*>(info))==reinterpret_cast<art::ArtMethod*>(old_method));}
 if(visit_field_mh){Put32(info,8,3);assert(visitor->VisitField(reinterpret_cast<art::ArtField*>(old_fields[3]),*reinterpret_cast<const art::ReflectionSourceInfo*>(info))==reinterpret_cast<art::ArtField*>(old_fields[3]));}
 if(visit_vh){Put32(info,8,4);assert(visitor->VisitField(reinterpret_cast<art::ArtField*>(old_fields[3]),*reinterpret_cast<const art::ReflectionSourceInfo*>(info))==reinterpret_cast<art::ArtField*>(old_fields[3]));}
}
class ProviderHooks:public Hooks {public:void* FindSymbol(const char* lib,const char* name)override{
 if(std::strstr(name,"Class13GetDescriptor"))return reinterpret_cast<void*>(ResolutionDescriptor);
 if(std::strstr(name,"ArtField7GetNameEv"))return reinterpret_cast<void*>(ResolutionName);
 if(std::strstr(name,"ArtField17GetTypeDescriptorEv"))return reinterpret_cast<void*>(ResolutionType);
 if(std::strstr(name,"VisitReflectiveTargets"))return reinterpret_cast<void*>(Visit);
 if(std::strstr(name,"HasSameNameAndSignature"))return reinterpret_cast<void*>(SameIndex);
 return Hooks::FindSymbol(lib,name);
}};
std::vector<unsigned char> File(const char* path){std::ifstream in(path,std::ios::binary);assert(in.good());return {std::istreambuf_iterator<char>(in),{}};}
void LinkDex(class_metadata::Class17* klass,std::vector<unsigned char>& bytes){
 auto* prefix=new(heap.New(sizeof(dex_mapping_abi::Prefix)))dex_mapping_abi::Prefix{nullptr,bytes.data(),bytes.size(),bytes.data(),bytes.size()};
 auto* cache=heap.New(sizeof(abi::arm64::android11::LegacyCache));const uint64_t link=uintptr_t(prefix);
 std::memcpy(static_cast<char*>(cache)+offsetof(abi::arm64::android11::LegacyCache,dex_file),&link,8);klass->dex_cache=uint32_t(uintptr_t(cache));
}
void Methods(class_metadata::Class17* klass,bool deletion){
 auto* array=static_cast<unsigned char*>(heap.New(8+3*32));Put32(array,0,deletion?2:3);klass->methods=uintptr_t(array);
 for(size_t n=0;n<(deletion?2u:3u);++n){const uint32_t index=deletion?uint32_t(n+1):uint32_t(n);auto* m=reinterpret_cast<abi::arm64::android12::Method12*>(array+8+32*n);
  m->declaring_class=uint32_t(uintptr_t(klass));m->dex_method_index=index;m->access_flags=index==0?0x101:index==1?1:9;m->data=index==0?uintptr_t(&binding_value):0;
 }
 if(klass==old_class)old_method=reinterpret_cast<abi::arm64::android12::Method12*>(array+8);
}
}
int main(int argc,char** argv){
 assert(argc==5);InitJni();InitializeHeap();
 const uint32_t field_indexes[]{1,3,0,2};for(size_t i=0;i<4;++i){old_fields[i]->dex_field_index=field_indexes[i];new_fields[i]->dex_field_index=field_indexes[i];}
 ProviderHooks hooks;std::string error;
 std::array<modern_field_metadata::FieldOracle,4> oracle{};for(size_t i=0;i<4;++i)oracle[i]={reinterpret_cast<jobject>(&mirrors[i]),names[i],types[i],i==0?8u:1u};
 modern_field_metadata::FieldMetadata metadata;assert(metadata.Bind(hooks,&error));
 struct Cal{modern_field_metadata::FieldMetadata* metadata;const modern_field_metadata::FieldOracle* oracle;std::string* error;} cal{&metadata,oracle.data(),&error};
 auto calibrate=[](const runnable_jni::Context& ctx,void* raw){auto& c=*static_cast<Cal*>(raw);return c.metadata->Calibrate(ctx,reinterpret_cast<jobject>(old_class),"LFixture;",c.oracle,4,c.error);};
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),calibrate,&cal)==runnable_jni::Result::kSuccess);
 size_t thread_extent=0,card_extent=0;assert(method_identity::ReadableMappingExtent(thread.data(),&thread_extent));assert(method_identity::ReadableMappingExtent(cards,&card_extent));
 modern_field_storage::Service fields;if(!fields.Calibrate(hooks,&env,reinterpret_cast<jobject>(old_class),reinterpret_cast<jobject>(old_instance),metadata,oracle.data(),4,&error)){std::cerr<<"Fixture field storage calibration: threadExtent="<<thread_extent<<" cardExtent="<<card_extent<<" bias="<<bias<<" current="<<current<<" "<<error<<std::endl;return 1;}
 native_cache::Service caches;assert(caches.Bind(hooks,&thread_list,&error)&&caches.Prepare(&error));
 Methods(old_class,false);Methods(new_class,false);
 std::array<method_identity::Oracle,3> method_oracles{};
 for(size_t i=0;i<3;++i){auto* m=reinterpret_cast<abi::arm64::android12::Method12*>(reinterpret_cast<char*>(old_method)+i*32);executable[i].art_method=uintptr_t(m);executable[i].dex_method_index=uint32_t(i);executable[i].access_flags=i==0?0x101:i==1?1:9;method_oracles[i]={reinterpret_cast<jobject>(&executable[i]),uint32_t(i),executable[i].access_flags};}
 method_identity::Calibration model{};std::unique_ptr<executable_mirror::Layout> layout;std::unique_ptr<modern_methods::Binding> binding;
 struct Proof{ProviderHooks* hooks;method_identity::Calibration* model;std::unique_ptr<executable_mirror::Layout>* layout;std::unique_ptr<modern_methods::Binding>* binding;const method_identity::Oracle* oracle;std::string* error;} proof{&hooks,&model,&layout,&binding,method_oracles.data(),&error};
 auto prove=[](const runnable_jni::Context& ctx,void* raw){auto& r=*static_cast<Proof*>(raw);if(!method_identity::Calibrate(ctx,Leaf,r.oracle,3,r.model,r.error))return false;*r.layout=executable_mirror::Layout::Probe(ctx,Leaf,DecodeMirror,r.oracle,3,r.error);*r.binding=modern_methods::Service::BindOwned(*r.hooks,ctx,Leaf,r.oracle[0],&binding_value,&linker,r.model->model,r.error);return bool(*r.layout)&&bool(*r.binding);};
 if(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),prove,&proof)!=runnable_jni::Result::kSuccess){std::cerr<<"Fixture native method calibration: "<<error<<std::endl;return 1;}
 native_structural_reflection::Service reflection;assert(reflection.Bind(hooks,&runtime,*layout,&error));
 std::array<std::vector<unsigned char>,4> dex{File(argv[1]),File(argv[2]),File(argv[3]),File(argv[4])};
 LinkDex(old_class,dex[0]);
 source_commit::Plan plan{old_class,class_array,{old_class},{new_class},{old_instance},{new_instance},true};
 native_source_commit::Diagnostics diagnostics;diagnostics.cleared_threads=87;diagnostics.declaring_roots_verified=true;
 native_member_provider::Dependencies dependencies;dependencies.hooks=&hooks;dependencies.methods=binding.get();dependencies.method_model=&model;dependencies.metadata=&metadata;dependencies.fields=&fields;dependencies.cache=&caches;dependencies.reflection=&reflection;dependencies.is_retired=Retired;dependencies.allow_field_removal=true;dependencies.diagnostics=&diagnostics;
 dependencies.retirement={&retired_methods,NotRetired,NotRetired,RetireMethod,RetireField};dependencies.handle_guards={&retired_methods,Mh,Vh};
 dependencies.runtime.runtime=&runtime;dependencies.runtime.instrumentation=&instrumentation;dependencies.runtime.receivers.thread_list=&thread_list;dependencies.runtime.receivers.jit=&jit;dependencies.runtime.receivers.jit_code_cache=&jit_cache;
 Stop(nullptr,nullptr,false);
 auto shape=[&](bool methods,bool removed_fields){Methods(new_class,methods);Put32(reinterpret_cast<void*>(new_class->fields),0,removed_fields?3:4);LinkDex(new_class,dex[(methods?1:0)+(removed_fields?2:0)]);};
 auto rejected=[&](native_member_provider::Dependencies d,const source_commit::Plan& p,const char* expected=nullptr){const std::vector<unsigned char> bytes(heap.base,heap.base+heap.used);const int before=mutations;native_member_provider::Service provider;assert(!provider.Prepare(p,d,&error));assert(!error.empty());if(expected&&error.find(expected)==std::string::npos){std::cerr<<"Expected "<<expected<<" got "<<error<<"\n";std::abort();}assert(mutations==before&&events.empty()&&retired_methods==0&&retired_fields==0);assert(!std::memcmp(bytes.data(),heap.base,bytes.size()));assert(diagnostics.cleared_threads==87&&diagnostics.declaring_roots_verified);};
 shape(true,false);dependencies.expected_removal=native_member_provider::Removal::kFieldsOnly;rejected(dependencies,plan,"transaction shape");
 shape(false,true);dependencies.expected_removal=native_member_provider::Removal::kMethodsOnly;rejected(dependencies,plan,"transaction shape");
 shape(true,true);rejected(dependencies,plan,"transaction shape");dependencies.expected_removal=native_member_provider::Removal::kFieldsOnly;rejected(dependencies,plan,"transaction shape");
 shape(false,false);rejected(dependencies,plan,"transaction shape");dependencies.expected_removal=native_member_provider::Removal::kMethodsOnly;rejected(dependencies,plan,"transaction shape");
 shape(true,false);visit_mh=true;rejected(dependencies,plan,"unsupported MethodHandle");mh_guard=true;visit_mh=false;
 shape(false,true);dependencies.expected_removal=native_member_provider::Removal::kFieldsOnly;visit_vh=true;rejected(dependencies,plan,"unsupported MethodHandle");vh_guard=true;visit_vh=false;
 // A Field visited through MethodHandle kind 3 needs its own installed owner
 // proof: neither ordinary-method readiness nor VarHandle readiness proves it.
 visit_field_mh=true;mh_guard=true;vh_guard=true;
 rejected(dependencies,plan,"unsupported MethodHandle"); // Missing fourth callback.
 dependencies.handle_guards.field_method_handles=FieldMh;
 rejected(dependencies,plan,"unsupported MethodHandle"); // Independent proof false.
 field_mh_guard=true;mh_guard=false;vh_guard=false;
 {native_member_provider::Service field_mh_provider;const std::vector<unsigned char> bytes(heap.base,heap.base+heap.used);auto valid=field_mh_provider.Prepare(plan,dependencies,&error);assert(valid&&valid->Validate(plan));
  assert(!std::memcmp(bytes.data(),heap.base,bytes.size())&&!retired_methods&&!retired_fields&&!mutations&&events.empty());
  field_mh_guard=false;assert(!source_commit::Run(plan,*valid));field_mh_guard=true;
  assert(!std::memcmp(bytes.data(),heap.base,bytes.size())&&!retired_methods&&!retired_fields&&!mutations&&events.empty());
  assert(diagnostics.cleared_threads==87&&diagnostics.declaring_roots_verified);}
 visit_field_mh=false;mh_guard=true;vh_guard=true;
 auto missing=dependencies;
 for(int n=0;n<8;++n){missing=dependencies;switch(n){case 0:missing.hooks=nullptr;break;case 1:missing.methods=nullptr;break;case 2:missing.method_model=nullptr;break;case 3:missing.metadata=nullptr;break;case 4:missing.fields=nullptr;break;case 5:missing.cache=nullptr;break;case 6:missing.reflection=nullptr;break;case 7:missing.is_retired=nullptr;break;}rejected(missing,plan,"Incomplete independently");}
 auto invalid_model=model;invalid_model.model=static_cast<method_identity::Model>(99);missing=dependencies;missing.method_model=&invalid_model;rejected(missing,plan,"complete independent source calibration");
 source_commit::Plan missing_pair=plan;missing_pair.new_classes.clear();rejected(dependencies,missing_pair,"Incomplete independently");
 missing=dependencies;missing.allow_field_removal=false;rejected(missing,plan);missing=dependencies;missing.retirement.retire_field=nullptr;rejected(missing,plan);
 current=foreign_thread.data();rejected(dependencies,plan,"Incomplete independently");current=thread.data();
 missing=dependencies;missing.diagnostics=nullptr; // Diagnostics are optional to the adapter; missing guards are not.
 missing.handle_guards={};visit_vh=true;rejected(missing,plan);visit_vh=false;
 shape(true,false);auto invalid_policy=dependencies;invalid_policy.expected_removal=static_cast<native_member_provider::Removal>(99);rejected(invalid_policy,plan,"Unknown native");
 shape(false,true);vh_guard=true;visit_vh=true;{native_member_provider::Service fields_provider;auto valid=fields_provider.Prepare(plan,dependencies,&error);assert(valid&&valid->Validate(plan)&&!retired_fields&&!retired_methods&&!mutations);}visit_vh=false;
 shape(true,true);auto any=dependencies;any.expected_removal=native_member_provider::Removal::kAny;{native_member_provider::Service mixed_provider;auto valid=mixed_provider.Prepare(plan,any,&error);assert(valid&&valid->Validate(plan)&&!retired_fields&&!retired_methods&&!mutations);}
 shape(true,false);dependencies.expected_removal=native_member_provider::Removal::kMethodsOnly;mh_guard=true;visit_mh=true;
 hooks.missing="ReplaceReferences";rejected(dependencies,plan);hooks.missing=nullptr;
 native_member_provider::Service provider;auto service=provider.Prepare(plan,dependencies,&error);if(!service){std::cerr<<error<<'\n';return 1;}assert(service->Validate(plan));assert(retired_methods==0&&retired_fields==0&&mutations==0&&events.empty());
 const std::vector<unsigned char> prepared_heap(heap.base,heap.base+heap.used);
 mh_guard=false;assert(!source_commit::Run(plan,*service));mh_guard=true;
 current=foreign_thread.data();assert(!source_commit::Run(plan,*service));current=thread.data();
 assert(!std::memcmp(prepared_heap.data(),heap.base,prepared_heap.size())&&!retired_methods&&!retired_fields&&!mutations&&events.empty());
 assert(diagnostics.cleared_threads==87&&diagnostics.declaring_roots_verified);
 // Native provenance integration: a prewarmed ancestor field is not in the
 // transaction's old->new map. The real source-kind callback must still clear
 // it before mapping when the replacement child declares the same field.
 shape(false,false);visit_mh=false;
 native_method_table::View before_table,after_table;
 assert(native_method_table::Read(metadata,model,old_class,&before_table,&error));
 assert(native_method_table::Read(metadata,model,new_class,&after_table,&error));
 auto table=modern_methods::Service::PrepareTables(*binding,old_class,before_table.methods,new_class,after_table.methods,[](void*){return false;},&error);
 assert(table);
 const uint64_t saved_parent_fields=static_cast<class_metadata::Class17*>(parent_class)->fields;
 auto* ancestor_array=static_cast<unsigned char*>(heap.New(4+sizeof(abi::arm64::Field)));Put32(ancestor_array,0,1);
 ancestor_cached_field=new(ancestor_array+4)abi::arm64::Field{};
 ancestor_cached_field->declaring_class=uint32_t(uintptr_t(parent_class));ancestor_cached_field->access_flags=8;
 static_cast<class_metadata::Class17*>(parent_class)->fields=uintptr_t(ancestor_array);
 auto mapping=native_structural_reflection::Plan::Prepare(plan,metadata,{table.get()},dependencies.retirement,&error,dependencies.handle_guards,{&hooks,&model,&fields});
 assert(mapping);
 auto* ancestor_method=new(heap.New(sizeof(abi::arm64::android12::Method12)))abi::arm64::android12::Method12{};
 ancestor_method->declaring_class=uint32_t(uintptr_t(parent_class));ancestor_method->access_flags=9;ancestor_method->dex_method_index=2;
 visit_resolution=true;resolved_method_slot=reinterpret_cast<art::ArtMethod*>(ancestor_method);resolved_field_slot=reinterpret_cast<art::ArtField*>(ancestor_cached_field);
 assert(reflection.Validate(*mapping,&error)&&resolved_field_slot==reinterpret_cast<art::ArtField*>(ancestor_cached_field)&&resolved_method_slot==reinterpret_cast<art::ArtMethod*>(ancestor_method));
 reflection.Update(*mapping);assert(!resolved_field_slot&&!resolved_method_slot);
 // A native cache callback can legally point outside the transaction's ancestry.
 // A proxy or array needs a platform string for GetDescriptor, but resolution
 // eligibility must not request its descriptor or inspect its field storage.
 auto* unrelated=new(heap.New(sizeof(class_metadata::Class17)))class_metadata::Class17{};
 unrelated->fields=UINT64_MAX;
 auto* unrelated_method=new(heap.New(sizeof(abi::arm64::android12::Method12)))abi::arm64::android12::Method12{};
 unrelated_method->declaring_class=uint32_t(uintptr_t(unrelated));unrelated_method->access_flags=9;unrelated_method->dex_method_index=2;
 auto* unrelated_field=new(heap.New(sizeof(abi::arm64::Field)))abi::arm64::Field{};
 unrelated_field->declaring_class=uint32_t(uintptr_t(unrelated));unrelated_field->access_flags=8;
 forbidden_descriptor_class=unrelated;
 for(unsigned mode=0;mode<3;++mode){unrelated->access_flags=mode==0?0x40000u:1u;unrelated->component_type=mode==1?uint32_t(uintptr_t(parent_class)):0;unrelated->primitive_type=mode==2?5:0;
  resolved_method_slot=reinterpret_cast<art::ArtMethod*>(unrelated_method);resolved_field_slot=reinterpret_cast<art::ArtField*>(unrelated_field);
  assert(reflection.Validate(*mapping,&error));reflection.Update(*mapping);
  assert(resolved_method_slot==reinterpret_cast<art::ArtMethod*>(unrelated_method)&&resolved_field_slot==reinterpret_cast<art::ArtField*>(unrelated_field));}
 assert(forbidden_descriptor_calls==0);forbidden_descriptor_class=nullptr;
 // Excluded Runtime/virtual/constructor and private-field entries do not need
 // a declaring Class root; still exercise the production TU native observer.
 unrelated_method->declaring_class=0;unrelated_field->declaring_class=0;unrelated_field->access_flags=2;
 for(uint32_t flags:{1u,0x10001u}){unrelated_method->access_flags=flags;
  resolved_method_slot=reinterpret_cast<art::ArtMethod*>(unrelated_method);resolved_field_slot=reinterpret_cast<art::ArtField*>(unrelated_field);
  assert(reflection.Validate(*mapping,&error));reflection.Update(*mapping);assert(resolved_method_slot&&resolved_field_slot);}
 // An eligible missing root remains a readonly refusal, with precise first
 // callback diagnostics. It must never become an unrelated-cache Keep.
 unrelated_method->access_flags=9;
 resolved_method_slot=reinterpret_cast<art::ArtMethod*>(unrelated_method);resolved_field_slot=nullptr;
 assert(!reflection.Validate(*mapping,&error));assert(error.find("source=8 kind=method")!=std::string::npos&&error.find("stage=declaring-class")!=std::string::npos);
 assert(resolved_method_slot==reinterpret_cast<art::ArtMethod*>(unrelated_method));
 unrelated_method->declaring_class=uint32_t(uintptr_t(unrelated));unrelated->access_flags=0x200;unrelated->primitive_type=0;unrelated->component_type=0;
 resolved_method_slot=reinterpret_cast<art::ArtMethod*>(unrelated_method);
 assert(!reflection.Validate(*mapping,&error)&&error.find("named-interface-implements-unavailable")!=std::string::npos);
 visit_resolution=false;static_cast<class_metadata::Class17*>(parent_class)->fields=saved_parent_fields;
 auto unchanged_without_provenance=native_structural_reflection::Plan::Prepare(plan,metadata,{table.get()},dependencies.retirement,&error,dependencies.handle_guards);
 assert(unchanged_without_provenance);
 // A new direct declaration must never silently fall back to the old mapping
 // adapter when complete resolution provenance is omitted.
 LinkDex(old_class,dex[1]);Methods(old_class,true);LinkDex(new_class,dex[0]);Methods(new_class,false);
 assert(native_method_table::Read(metadata,model,old_class,&before_table,&error));
 assert(native_method_table::Read(metadata,model,new_class,&after_table,&error));
 table=modern_methods::Service::PrepareTables(*binding,old_class,before_table.methods,new_class,after_table.methods,[](void*){return false;},&error);assert(table);
 assert(!native_structural_reflection::Plan::Prepare(plan,metadata,{table.get()},dependencies.retirement,&error,dependencies.handle_guards));
 assert(error.find("Added declarations require")!=std::string::npos);
 auto additive=native_structural_reflection::Plan::Prepare(plan,metadata,{table.get()},dependencies.retirement,&error,dependencies.handle_guards,{&hooks,&model,&fields});
 assert(additive&&reflection.Validate(*additive,&error));
 Resume(nullptr);
 std::cout<<"Native member provider: public field/method/Executable calibration, complete DEX tables, MethodsOnly/FieldsOnly/mixed rejection, independent live method/field MH and VH guards, missing field policy and wrong thread all refuse before heap mutation/retirement/diagnostics; guarded method/field/mixed preparation stays readonly and final guard/thread refusal never retires\n";
}
