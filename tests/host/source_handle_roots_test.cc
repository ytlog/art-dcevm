#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "native_commit_fixture.inc"
#pragma clang diagnostic pop
#include "platform/common/source_handle_roots.h"
namespace {
// Independent named-source walker: this fixture's actual TLS top is 280. The
// service derives that location from the independently calibrated card oracle.
constexpr size_t original_top=280;
#pragma pack(push,4)
struct OriginalHeader{void* previous;int32_t count;};
#pragma pack(pop)
void* Top(){void* out=nullptr;std::memcpy(&out,thread.data()+original_top,8);return out;}
void Publish(void* value){std::memcpy(thread.data()+original_top,&value,8);}
bool corrupt_info=false,hide_owned=false;const void* public_slot=nullptr;size_t walks=0;
void OriginalRoots(void* self,abi::arm64::android_common::HandleRootVisitor* visitor,uint32_t flags){
 assert(self==thread.data()&&flags==0&&Read32(self)==0);++walks;
 for(void* scope=Top();scope;){OriginalHeader header{};std::memcpy(&header,scope,sizeof(header));
  assert(header.count>=0&&header.count<=32);std::array<uint32_t*,32> roots{};
  for(int32_t i=0;i<header.count;++i)roots[size_t(i)]=reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(scope)+12+size_t(i)*4);
  abi::arm64::android_common::NativeRootInfo info{nullptr,corrupt_info?5u:4u,0};
  if(!hide_owned||roots[0]==public_slot)visitor->VisitRoots(roots.data(),size_t(header.count),info);
  scope=header.previous;
 }
}
class RootHooks final:public Hooks{public:
 void* FindSymbol(const char* lib,const char* name)override{
  if(std::strstr(name,"HandleScopeVisitRoots"))return reinterpret_cast<void*>(OriginalRoots);
  if(std::strstr(name,"AllocateNewClassObject"))return reinterpret_cast<void*>(OriginalRoots); // Presence only; actual allocator is not invoked by this test.
  return Hooks::FindSymbol(lib,name);
 }
};
struct MoveRoots final:abi::arm64::android_common::HandleRootVisitor{
 uint32_t old_value,next_value;
 void VisitRoots(void***,size_t,const abi::arm64::android_common::NativeRootInfo&)override{assert(false);}
 void VisitRoots(uint32_t** values,size_t n,const abi::arm64::android_common::NativeRootInfo&)override{for(size_t i=0;i<n;++i)if(*values[i]==old_value)*values[i]=next_value;}
};
}
int main(){
 InitJni();InitializeHeap();RootHooks hooks;std::string error;
 std::array<modern_field_metadata::FieldOracle,4> oracle{};for(size_t i=0;i<4;++i)oracle[i]={reinterpret_cast<jobject>(&mirrors[i]),names[i],types[i],i==0?8u:1u};
 modern_field_metadata::FieldMetadata metadata;assert(metadata.Bind(hooks,&error));
 struct Calibration{modern_field_metadata::FieldMetadata* metadata;modern_field_metadata::FieldOracle* oracle;std::string* error;} calibration{&metadata,oracle.data(),&error};
 auto calibrate=[](const runnable_jni::Context& c,void* p){auto& r=*static_cast<Calibration*>(p);return r.metadata->Calibrate(c,reinterpret_cast<jobject>(old_class),"LFixture;",r.oracle,4,r.error);};
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),calibrate,&calibration)==runnable_jni::Result::kSuccess);
 modern_field_storage::Service fields;assert(fields.Calibrate(hooks,&env,reinterpret_cast<jobject>(old_class),reinterpret_cast<jobject>(old_instance),metadata,oracle.data(),oracle.size(),&error));
 assert(fields.CardTableOffset()==144);
 void* self=thread.data();JNIEnv* source_env=&env;std::memcpy(thread.data()+216,&self,8);std::memcpy(thread.data()+200,&source_env,8);
 source_handle_roots::Service no_oracle;modern_field_storage::Service unavailable;assert(!source_handle_roots::Service::Bind(hooks,unavailable,&no_oracle,&error));
 source_handle_roots::Service service;assert(source_handle_roots::Service::Bind(hooks,fields,&service,&error));assert(!service.Calibrated()&&!service.CreateScope(1,&error));
 alignas(8) unsigned char original[24]{};OriginalHeader header{nullptr,3};std::memcpy(original,&header,sizeof(header));Put32(original,12,uint32_t(uintptr_t(old_class)));public_slot=original+12;Publish(original);
 struct Request{source_handle_roots::Service* service;std::string* error;const void* handle;bool expected;} request{&service,&error,public_slot,true};
 auto observe=[](const runnable_jni::Context& c,void* p){auto& r=*static_cast<Request*>(p);const bool result=r.service->ObserveAllocatorHandle(c,reinterpret_cast<jobject>(old_class),r.handle,r.error);assert(result==r.expected);return true;};
 // Candidate corruption/foreign slots/root provenance refuse before publication.
 uint32_t foreign=uint32_t(uintptr_t(old_class));request.handle=&foreign;request.expected=false;
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),observe,&request)==runnable_jni::Result::kSuccess);assert(Top()==original&&!service.Calibrated());
 request.handle=public_slot;
 auto original_ref_kind=table.GetObjectRefType;table.GetObjectRefType=[](JNIEnv*,jobject){Jni();return JNILocalRefType;};
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),observe,&request)==runnable_jni::Result::kSuccess);assert(Top()==original&&!service.Calibrated());table.GetObjectRefType=original_ref_kind;
 corrupt_info=true;
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),observe,&request)==runnable_jni::Result::kSuccess);assert(Top()==original&&!service.Calibrated());corrupt_info=false;
 header.previous=original;std::memcpy(original,&header,sizeof(header));
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),observe,&request)==runnable_jni::Result::kSuccess);assert(!service.Calibrated());header.previous=nullptr;std::memcpy(original,&header,sizeof(header));
 hide_owned=true;
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),observe,&request)==runnable_jni::Result::kSuccess);assert(Top()==original&&!service.Calibrated());hide_owned=false;
 request.expected=true;
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),observe,&request)==runnable_jni::Result::kSuccess);assert(service.Calibrated()&&Top()==original&&walks>=5);
 auto use=[](const runnable_jni::Context&,void* p){auto& s=*static_cast<source_handle_roots::Service*>(p);std::string error;
  auto outer=s.CreateScope(3,&error);assert(outer&&!outer->Get(0)&&!outer->Get(2));outer->Set(0,old_class);
  {auto inner=s.CreateScope(2,&error);assert(inner&&!inner->Get(0));inner->Set(1,old_class);
   MoveRoots mover;mover.old_value=uint32_t(uintptr_t(old_class));mover.next_value=uint32_t(uintptr_t(new_class));OriginalRoots(thread.data(),&mover,0);
   assert(outer->Get(0)==new_class&&inner->Get(1)==new_class&&!inner->Get(0));}
  assert(outer->Get(0)==new_class);
  {source_handle_roots::Pool pool(s);std::array<void*,70> slots{};
   for(auto& slot:slots){slot=pool.Add(old_class,&error);assert(slot);}
   MoveRoots mover;mover.old_value=uint32_t(uintptr_t(old_class));mover.next_value=uint32_t(uintptr_t(new_class));OriginalRoots(thread.data(),&mover,0);
   for(auto slot:slots)assert(Read32(slot)==uint32_t(uintptr_t(new_class)));
  }
  assert(outer->Get(0)==new_class);return true;
 };
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),use,&service)==runnable_jni::Result::kSuccess);assert(Top()==original&&!service.CreateScope(1,&error));
 Publish(nullptr);
 std::cout<<"Source handle roots: real field/card oracle prerequisite, named current/TLS self, actual source handle/strong Class/visitor identity, foreign/cyclic/bad root info/hidden owned scopes refuse, nested zeroed roots update and restore exact chain; Native publication refuses\n";
}
