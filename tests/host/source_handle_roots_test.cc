#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "native_commit_fixture.inc"
#pragma clang diagnostic pop
#include "platform/common/source_handle_roots.h"
#include "platform/common/source_allocation_transaction.h"
namespace {
using SourceProbe=bool(*)(const runnable_jni::Context&,modern_field_metadata::FieldMetadata&,source_handle_roots::Service&,modern_field_storage::Service&);
SourceProbe source_probe=nullptr;
// Independent named-source walker: this fixture's actual TLS top is 280. The
// service derives that location from the independently calibrated card oracle.
constexpr size_t original_top=280;
#pragma pack(push,4)
struct OriginalHeader{void* previous;int32_t count;};
#pragma pack(pop)
bool sized_scope=std::getenv("SOURCE_SCOPE_SIZED")!=nullptr;
size_t OriginalReferences(){return sized_scope?16u:12u;}
void* Top(){void* out=nullptr;std::memcpy(&out,thread.data()+original_top,8);return out;}
void Publish(void* value){std::memcpy(thread.data()+original_top,&value,8);}
bool corrupt_info=false,hide_owned=false;const void* public_slot=nullptr;size_t walks=0;
void OriginalRoots(void* self,abi::arm64::android_common::HandleRootVisitor* visitor,uint32_t flags){
 assert(self==thread.data()&&flags==0&&Read32(self)==0);++walks;
 for(void* scope=Top();scope;){OriginalHeader header{};std::memcpy(&header,scope,sizeof(header));
  assert(header.count>=0&&header.count<=32);
  const uint32_t used=sized_scope?Read32(static_cast<unsigned char*>(scope)+12):uint32_t(header.count);
  assert(used<=uint32_t(header.count));std::array<uint32_t*,32> roots{};
  for(uint32_t i=0;i<used;++i)roots[i]=reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(scope)+OriginalReferences()+size_t(i)*4);
  abi::arm64::android_common::NativeRootInfo info{nullptr,corrupt_info?5u:4u,0};
  if(!hide_owned||roots[0]==public_slot)visitor->VisitRoots(roots.data(),size_t(used),info);
  scope=header.previous;
 }
}
abi::arm64::android11::ReleaseObjectPointer AllocationMirror(void* raw){return {uintptr_t(static_cast<abi::arm64::android15::ClassRedefinition*>(raw)->klass)};}
class RootHooks final:public Hooks{public:
 void* FindSymbol(const char* lib,const char* name)override{
  if(std::strstr(name,"GetMirrorClass"))return reinterpret_cast<void*>(AllocationMirror);
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
 alignas(8) unsigned char original[32]{};OriginalHeader header{nullptr,3};std::memcpy(original,&header,sizeof(header));if(sized_scope)Put32(original,12,2);Put32(original,OriginalReferences(),uint32_t(uintptr_t(old_class)));public_slot=original+OriginalReferences();
 // Capacity includes an unused nonzero tail; the modern walker must use size.
 if(sized_scope)Put32(original,24,uint32_t(uintptr_t(new_class)));Publish(original);
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
 struct Use{source_handle_roots::Service* service;modern_field_metadata::FieldMetadata* metadata;modern_field_storage::Service* fields;} use_data{&service,&metadata,&fields};
 auto use=[](const runnable_jni::Context& context,void* p){auto& data=*static_cast<Use*>(p);auto& s=*data.service;auto& metadata=*data.metadata;std::string error;
  void* borrowed=nullptr;assert(s.BorrowHandle(context,public_slot,&borrowed)&&borrowed==old_class);
  if(sized_scope){borrowed=new_class;
   assert(!s.BorrowHandle(context,static_cast<const unsigned char*>(public_slot)-4,&borrowed)&&borrowed==new_class);
   assert(!s.BorrowHandle(context,static_cast<const unsigned char*>(public_slot)+8,&borrowed)&&borrowed==new_class);
  }
  const auto calls=jni_calls;
  assert(s.BorrowOwnedStrongGlobal(context,reinterpret_cast<jobject>(old_class),&borrowed)&&borrowed==old_class&&jni_calls==calls);
  assert(s.BorrowStrongGlobal(context,reinterpret_cast<jobject>(old_class),&borrowed)&&borrowed==old_class&&jni_calls>calls);
  auto invalid=context;invalid.self=foreign_thread.data();borrowed=new_class;
  assert(!s.BorrowOwnedStrongGlobal(invalid,reinterpret_cast<jobject>(old_class),&borrowed)&&borrowed==new_class);
  uint32_t foreign_slot=uint32_t(uintptr_t(old_class));borrowed=new_class;
  assert(!s.BorrowHandle(context,&foreign_slot,&borrowed)&&borrowed==new_class);
  auto foreign_context=context;foreign_context.self=foreign_thread.data();
  assert(!s.BorrowHandle(foreign_context,public_slot,&borrowed)&&borrowed==new_class);

  if(source_probe)assert(source_probe(context,metadata,s,*data.fields));
  auto outer=s.CreateScope(3,&error);assert(outer&&!outer->Get(0)&&!outer->Get(2));outer->Set(0,old_class);
  {auto inner=s.CreateScope(2,&error);assert(inner&&!inner->Get(0));inner->Set(1,old_class);
   MoveRoots mover;mover.old_value=uint32_t(uintptr_t(old_class));mover.next_value=uint32_t(uintptr_t(new_class));OriginalRoots(thread.data(),&mover,0);
   assert(outer->Get(0)==new_class&&inner->Get(1)==new_class&&!inner->Get(0));}
  assert(outer->Get(0)==new_class);
  {source_handle_roots::Pool pool(s);std::array<void*,70> slots{};
   for(auto& slot:slots){slot=pool.Add(old_class,&error);assert(slot);}
   MoveRoots mover;mover.old_value=uint32_t(uintptr_t(old_class));mover.next_value=uint32_t(uintptr_t(new_class));OriginalRoots(thread.data(),&mover,0);
   for(auto slot:slots){void* value=nullptr;assert(pool.Read(slot,&value)&&value==new_class);}
   void* untouched=old_class;assert(!pool.Read(public_slot,&untouched)&&untouched==old_class);
   assert(!pool.Write(const_cast<void*>(public_slot),new_class));
   assert(!pool.Write(static_cast<char*>(slots.back())+4,new_class));
   assert(pool.Write(slots.back(),old_class));void* value=nullptr;
   assert(pool.Read(slots.back(),&value)&&value==old_class);
   auto saved_current=current;void* foreign_self=foreign_thread.data();
   std::memcpy(foreign_thread.data()+216,&foreign_self,8);
   std::memcpy(foreign_thread.data()+200,&context.env,8);Put32(foreign_self,0,0);
   current=foreign_self;value=new_class;
   assert(!pool.Read(slots.back(),&value)&&value==new_class);
   assert(!pool.Write(slots.back(),new_class));current=saved_current;
   auto* card=cards+((uintptr_t(old_class)-uintptr_t(heap.base))>>10);*card=0;
   const auto jni_before=jni_calls;
   assert(data.fields->SourceBarrier(context,pool,slots.back())&&*card==0x70&&jni_calls==jni_before);
   *card=0;assert(!data.fields->SourceBarrier(invalid,pool,slots.back())&&*card==0);
   assert(!data.fields->SourceBarrier(context,pool,public_slot)&&*card==0);
  }
  assert(s.BorrowHandle(context,outer->Slot(0),&borrowed)&&borrowed==new_class);
  assert(outer->Get(0)==new_class);
  // Source allocation has valid mirrors/caches while every initialized and
  // old/new Classes/instances slot is still empty. The final reader cannot be
  // substituted for this actual preallocation Holder phase.
  RootHooks hooks;source_handle_roots::Pool targets(s);
  auto* array_type=static_cast<class_metadata::Class17*>(heap.New(160));InitializeClass(array_type);
  array_type->component_type=uint32_t(uintptr_t(parent_class));
  void* array=heap.New(12+26*4);Put32(array,0,uint32_t(uintptr_t(array_type)));Put32(array,8,26);
  void* cache=heap.New(16);Put32(cache,0,uint32_t(uintptr_t(parent_class)));
  Put32(array,12+3*4,uint32_t(uintptr_t(cache)));Put32(array,12+4*4,uint32_t(uintptr_t(old_class)));
  Put32(array,12+16*4,uint32_t(uintptr_t(cache)));Put32(array,12+17*4,uint32_t(uintptr_t(new_class)));
  uint32_t array_slot=uint32_t(uintptr_t(array));
  std::array<abi::arm64::android15::ClassRedefinition,2> owners{};
  owners[0].klass=reinterpret_cast<jclass>(old_class);owners[1].klass=reinterpret_cast<jclass>(new_class);
  transaction_root::Owners owner_span{reinterpret_cast<const unsigned char*>(owners.data()),reinterpret_cast<const unsigned char*>(owners.data()+2),reinterpret_cast<const unsigned char*>(owners.data()+2)};
  uint64_t initialized=0,structural=3,initial=3;
  transaction_root::Holder holder{&array_slot,&owner_span,{&initialized,2,1},{&structural,2,1},{&initial,2,1}};
  source_allocation_transaction::Snapshot snapshot;
  assert(source_allocation_transaction::Read(hooks,metadata,&holder,targets,&snapshot,&error));
  assert(snapshot.targets.size()==2&&snapshot.metadata.count==2);
  assert(snapshot.targets[0].owner==&owners[0]&&snapshot.targets[1].owner==&owners[1]);
  assert(s.BorrowHandle(context,snapshot.targets[0].klass,&borrowed)&&borrowed==old_class);
  assert(s.BorrowHandle(context,snapshot.targets[1].klass,&borrowed)&&borrowed==new_class);
  auto untouched=snapshot.targets[0].klass;
  owners[1].klass=reinterpret_cast<jclass>(old_class);
  assert(!source_allocation_transaction::Read(hooks,metadata,&holder,targets,&snapshot,&error)&&snapshot.targets[0].klass==untouched);
  owners[1].klass=reinterpret_cast<jclass>(new_class);Put32(array,8,25);
  assert(!source_allocation_transaction::Read(hooks,metadata,&holder,targets,&snapshot,&error));Put32(array,8,26);
  holder.actually_structural.size=1;assert(!source_allocation_transaction::Read(hooks,metadata,&holder,targets,&snapshot,&error));
  return true;
 };
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),use,&use_data)==runnable_jni::Result::kSuccess);assert(Top()==original&&!service.CreateScope(1,&error)&&!service.ReadyForSourceAccess());
 const auto native_queries=method_identity::MappingOsQueries();void* unchanged=new_class;
 runnable_jni::Context native{self,source_env,&runtime};
 assert(!service.ReadyForSourceAccess()&&!service.BorrowHandle(native,public_slot,&unchanged)&&unchanged==new_class);
 assert(!service.BorrowOwnedStrongGlobal(native,reinterpret_cast<jobject>(old_class),&unchanged)&&unchanged==new_class);
 auto foreign_native=native;foreign_native.self=reinterpret_cast<void*>(uintptr_t(1));
 assert(!service.BorrowHandle(foreign_native,public_slot,&unchanged)&&unchanged==new_class);
 assert(method_identity::MappingOsQueries()==native_queries);
 Publish(nullptr);
 std::cout<<"Source handle roots: real field/card oracle prerequisite, named current/TLS self, actual source handle/strong Class/visitor identity, foreign/cyclic/bad root info/hidden owned scopes refuse, nested zeroed roots update and restore exact chain; Native publication refuses\n";
 return 0;
}
