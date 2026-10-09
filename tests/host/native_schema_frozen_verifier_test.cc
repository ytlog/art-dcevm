#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "native_commit_fixture.inc"
#pragma clang diagnostic pop
#include "platform/common/native_schema_policy.h"
#include "platform/common/native_schema_binding.h"
#include "platform/common/native_abstract_instances.h"
#define private public
#include "platform/common/native_schema_guard_verifier.h"
#undef private
#include "platform/common/prepared_root_registry.h"
// This test accesses the actual private frozen-state implementation without
// adding a production readiness/testing capability. JNI stage and Direct gate
// are boundary doubles; field metadata still uses its real named calibration.
#define private public
#include "platform/common/native_schema_guard_verifier.cc"
#undef private
#include <new>
static bool deny_alloc=false;
void* operator new(std::size_t n){if(deny_alloc)throw std::bad_alloc();if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
namespace artdcevm::redefinition_gate {bool Owned(JavaVM*){return stopped==1;}}
namespace {
struct Handle{void* live;};std::array<Handle,32> handles{};size_t used=0;
bool DecodeHandle(void*,jobject root,void** out){if(!root||!out)return false;*out=reinterpret_cast<Handle*>(root)->live;return *out;}
jobject OwnHandle(void*,void* value){assert(used<handles.size());handles[used].live=value;return reinterpret_cast<jobject>(&handles[used++]);}
void ReleaseHandle(void*,jobject){}
void* Method(void*,jmethodID id){return id;}
void* Field(void*,jclass,jfieldID id){return id;}
void* Owner(void*,void* method){return Root(method);}
}
int main(){
 InitJni();InitializeHeap();Hooks hooks;std::string error;
 std::array<modern_field_metadata::FieldOracle,4> oracles{};for(size_t i=0;i<4;++i)oracles[i]={reinterpret_cast<jobject>(&mirrors[i]),names[i],types[i],i==0?8u:1u};
 modern_field_metadata::FieldMetadata metadata;assert(metadata.Bind(hooks,&error));
 struct Request{modern_field_metadata::FieldMetadata* m;decltype(oracles)* o;std::string* e;} request{&metadata,&oracles,&error};
 const auto calibrate=[](const runnable_jni::Context& scope,void* raw){auto& r=*static_cast<Request*>(raw);return r.m->Calibrate(scope,reinterpret_cast<jobject>(old_class),"LFixture;",r.o->data(),4,r.e);};
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),calibrate,&request)==runnable_jni::Result::kSuccess);
 method_identity::Calibration model{};model.model=method_identity::Model::kMethod12Onward;
 using namespace native_schema_guard_verifier;
 // Private State is named through the production class definition below.
 Verifier verifier;auto& s=*verifier.state_;s.bindings.metadata=&metadata;s.bindings.methods=&model;
 s.bindings.decode_global=DecodeHandle;s.bindings.decode_field=Field;s.bindings.decode_method=Method;s.bindings.method_class=Owner;
 s.nodes.resize(2);s.nodes[0].root=reinterpret_cast<jclass>(OwnHandle(nullptr,old_class));s.nodes[1].root=reinterpret_cast<jclass>(OwnHandle(nullptr,parent_class));
 s.nodes[0].method_ids={reinterpret_cast<jmethodID>(old_method)};for(auto* f:old_fields)s.nodes[0].field_ids.push_back(reinterpret_cast<jfieldID>(f));
 s.frozen_snapshot.classes={old_class,parent_class};s.frozen_snapshot.loaders={nullptr,nullptr};s.frozen_snapshot.parents={1,SIZE_MAX};
 s.frozen_snapshot.declared={{old_method},{}};s.frozen_snapshot.all_methods={{old_method},{}};s.class_proofs.resize(2);
 auto& proof=s.class_proofs[0];proof.methods=reinterpret_cast<void*>(old_class->methods);proof.instances=proof.statics=reinterpret_cast<void*>(old_class->fields);proof.instance_count=proof.static_count=4;
 for(size_t i=0;i<4;++i)s.member_proofs.push_back({old_fields[i],0,old_fields[i]->access_flags,uint32_t(i),old_fields[i]->offset,true,reinterpret_cast<jfieldID>(old_fields[i])});
 s.member_proofs.push_back({old_method,0,old_method->access_flags,old_method->dex_method_index,0,false});
 s.expected.mirror=old_class;s.expected.new_classes_root=class_array;s.expected.old_classes={old_class};s.expected.new_classes={new_class};s.expected.class_root_origins={{class_array,0}};
 prepared_roots::Registry registry{{nullptr,OwnHandle,DecodeHandle,ReleaseHandle}};const auto registrar=registry.Collect();
 source_commit::Plan plan=s.expected;prepared_roots::Bindings plan_roots;assert(rooted_schema_plan::CollectBindings(&plan,registrar,&plan_roots));
 assert(rooted_schema_plan::CollectBindings(&s.expected,registrar,&s.expected_roots));
 for(auto* values:{&s.frozen_snapshot.classes,&s.frozen_snapshot.loaders})for(auto& value:*values)assert(s.frozen_roots.Add(&value,registrar));
 s.frozen_roots.Seal();assert(registry.Freeze());s.frozen=true;s.sealed=true;Stop(nullptr,nullptr,false);assert(s.FrozenProof());
 deny_alloc=true;bool allocation_refused=false;try{(void)::operator new(1);}catch(const std::bad_alloc&){allocation_refused=true;}assert(allocation_refused);assert(registry.Resolve());assert(verifier.RefreshManagedRoots(plan,registry.Final()));assert(s.refreshed&&s.FrozenProof());
 // Runtime-only method flags may change; Java declaration/index/owner cannot.
 old_method->access_flags|=0x80000;assert(s.FrozenProof());old_method->access_flags^=1;assert(!s.FrozenProof());old_method->access_flags^=1;
 ++old_fields[1]->dex_field_index;assert(!s.FrozenProof());--old_fields[1]->dex_field_index;
 old_fields[1]->declaring_class=uint32_t(uintptr_t(new_class));assert(!s.FrozenProof());old_fields[1]->declaring_class=uint32_t(uintptr_t(old_class));
 old_class->super_class=0;assert(!s.FrozenProof());old_class->super_class=uint32_t(uintptr_t(parent_class));
 old_class->methods=0;assert(!s.FrozenProof());old_class->methods=uintptr_t(proof.methods);
 ++plan.class_root_origins[0].index;assert(!verifier.RefreshManagedRoots(plan,registry.Final()));--plan.class_root_origins[0].index;
 assert(verifier.RefreshManagedRoots(plan,registry.Final()));
 // GC updates globals and native declaring roots; final state is rebound in
 // existing storage while allocation remains deliberately unavailable.
 auto* moved=static_cast<class_metadata::Class17*>(moved_class);std::memcpy(moved,old_class,sizeof(*old_class));
 for(auto& handle:handles)if(handle.live==old_class)handle.live=moved;
 for(auto* field:old_fields)field->declaring_class=uint32_t(uintptr_t(moved));old_method->declaring_class=uint32_t(uintptr_t(moved));
 assert(registry.Resolve());assert(plan_roots.CanRefresh(registry.Final()));plan_roots.Refresh(registry.Final());
 assert(verifier.RefreshManagedRoots(plan,registry.Final())&&s.FrozenProof());
 deny_alloc=false;Resume(nullptr);registry.Clear();verifier.Clear(&env);
 std::cout<<"Actual frozen verifier native TU: calibrated metadata, allocator-forbidden refresh and flags/IDs/owners/parent/table/origin refusal PASS\n";
}
