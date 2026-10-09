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
namespace artdcevm::redefinition_gate {bool Owned(JavaVM*){return stopped==1;}}
namespace {
struct Handle{void* live;};
bool DecodeHandle(void*,jobject root,void** out){if(!root||!out)return false;*out=reinterpret_cast<Handle*>(root)->live;return *out;}
void* Method(void*,jmethodID id){return id;}
void* Field(void*,jclass,jfieldID id){return id;}
size_t native_field_reads=0;
void* NativeField(void*,void*,jfieldID id){++native_field_reads;return id;}
void* Owner(void*,void* method){return Root(method);}
std::array<void*,bounded_managed_roots::Store::kChunkSize> managed{};
jobjectArray JNICALL NewRoots(JNIEnv*,jsize length,jclass,jobject){Jni();assert(length==managed.size());return reinterpret_cast<jobjectArray>(&managed);}
jsize JNICALL RootsLength(JNIEnv*,jarray){Jni();return managed.size();}
void JNICALL SetRoot(JNIEnv*,jobjectArray,jsize index,jobject value){Jni();managed.at(index)=value;}
jobject JNICALL GetRoot(JNIEnv*,jobjectArray,jsize index){Jni();return reinterpret_cast<jobject>(managed.at(index));}
bool RootDecode(void*,jobject root,void** out){*out=root;return root;}
bool RootOracle(void*,const runnable_jni::Context&,jobject root,void** out){return RootDecode(nullptr,root,out);}
void* RootElement(void*,void* array,size_t index){assert(array==&managed);return managed.at(index);}
bool RootScope(void*,JNIEnv* jni,jobject,runnable_jni::Operation operation,void* payload){return operation({nullptr,jni,nullptr},payload);}

}
int main(){
 InitJni();InitializeHeap();Hooks hooks;std::string error;
 std::array<modern_field_metadata::FieldOracle,4> oracles{};for(size_t i=0;i<4;++i)oracles[i]={reinterpret_cast<jobject>(&mirrors[i]),names[i],types[i],i==0?8u:1u};
 modern_field_metadata::FieldMetadata metadata;assert(metadata.Bind(hooks,&error));
 struct Request{modern_field_metadata::FieldMetadata* m;decltype(oracles)* o;std::string* e;} request{&metadata,&oracles,&error};
 const auto calibrate=[](const runnable_jni::Context& scope,void* raw){auto& r=*static_cast<Request*>(raw);return r.m->Calibrate(scope,reinterpret_cast<jobject>(old_class),"LFixture;",r.o->data(),4,r.e);};
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),calibrate,&request)==runnable_jni::Result::kSuccess);

 using namespace native_schema_guard_verifier;
 struct Maps {
  const void* denied=nullptr;size_t reads=0;
  static bool Extent(void* p,const void* at,size_t* bytes){auto& m=*static_cast<Maps*>(p);++m.reads;
   if(at==m.denied||uintptr_t(at)<uintptr_t(heap.base)||uintptr_t(at)-uintptr_t(heap.base)>=(1u<<20))return false;
   *bytes=(1u<<20)-(uintptr_t(at)-uintptr_t(heap.base));return true;}
  static bool Readable(void* p,const void* at,size_t bytes){size_t extent=0;return Extent(p,at,&extent)&&extent>=bytes;}
 } maps;
 method_identity::Calibration model{};model.model=method_identity::Model::kMethod12Onward;model.identities.resize(3);model.readable_extents.resize(3);
 Verifier verifier;auto& s=*verifier.state_;s.bindings.metadata=&metadata;s.bindings.methods=&model;
 s.bindings.decode_native_field=NativeField;s.bindings.decode_global=DecodeHandle;s.bindings.decode_field=Field;s.bindings.decode_method=Method;s.bindings.method_class=Owner;
 s.bindings.owner=&maps;s.bindings.readable=Maps::Readable;s.bindings.writable=Maps::Readable;s.bindings.extent=Maps::Extent;
 auto* array=static_cast<unsigned char*>(heap.New(40));Put32(array,0,1);auto* different=reinterpret_cast<abi::arm64::android12::Method12*>(array+8);*different=*old_method;different->declaring_class=uint32_t(uintptr_t(parent_class));different->dex_method_index=7;
 static_cast<class_metadata::Class17*>(parent_class)->methods=uintptr_t(array);
 s.bindings.same_method=+[](void*,void* a,void* b){return static_cast<abi::arm64::android12::Method12*>(a)->dex_method_index==static_cast<abi::arm64::android12::Method12*>(b)->dex_method_index;};
 table.NewObjectArray=NewRoots;table.GetArrayLength=RootsLength;table.SetObjectArrayElement=SetRoot;table.GetObjectArrayElement=GetRoot;
 s.roots.Bind({nullptr,RootScope,RootOracle,RootDecode,RootElement});s.nodes.resize(3);
 const std::array<void*,3> classes{old_class,new_class,parent_class};
 for(size_t i=0;i<3;++i){auto& n=s.nodes[i];assert(s.roots.Stage(&env,reinterpret_cast<jobject>(classes[i]),&n.root,&error));n.schema.descriptor=i==2?"Ljava/lang/Object;":"LFixture;";
  if(i<2){assert(s.roots.Stage(&env,reinterpret_cast<jobject>(parent_class),&n.parent,&error));const auto& fields=i==0?old_fields:new_fields;
   for(size_t j=0;j<4;++j){n.field_ids.push_back(reinterpret_cast<jfieldID>(fields[j]));n.schema.fields.push_back({names[j],types[j],j==0?8u:1u,UINT32_MAX});}}
  auto* method=i==0?old_method:i==1?next_method:different;n.method_ids={reinterpret_cast<jmethodID>(method)};n.schema.methods.push_back({i==2?"different":"shared","()V",0x101,UINT32_MAX});
 }
 Stop(nullptr,nullptr,false);const auto queries=method_identity::MappingOsQueries();Snapshot out;
 assert(s.Roots(&out,&error));assert(out.classes.size()==3&&maps.reads>30&&method_identity::MappingOsQueries()==queries);
 maps.denied=old_class;assert(!s.Roots(&out,&error));maps.denied=nullptr;
 old_fields[1]->access_flags^=8;assert(!s.Roots(&out,&error));old_fields[1]->access_flags^=8;
 s.nodes[0].schema.fields[1].name="wrong";assert(!s.Roots(&out,&error));s.nodes[0].schema.fields[1].name=names[1];
 s.nodes[0].schema.fields[1].descriptor="J";assert(!s.Roots(&out,&error));s.nodes[0].schema.fields[1].descriptor=types[1];
 assert(s.Roots(&out,&error)&&method_identity::MappingOsQueries()==queries);
 Resume(nullptr);verifier.Clear(&env);
 std::cout<<"Actual schema Roots TU: shared snapshot, zero OS mapping queries, field flags/name/type and snapshot refusal PASS\n";
}
