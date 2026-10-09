#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "native_commit_fixture.inc"
#pragma clang diagnostic pop
#include "platform/common/source_handle_roots.h"
#include "platform/common/source_allocation_transaction.h"
#include <chrono>
namespace {
using SourceProbe=bool(*)(const runnable_jni::Context&,modern_field_metadata::FieldMetadata&,source_handle_roots::Service&,modern_field_storage::Service&);
SourceProbe source_probe=nullptr;
struct EnvironmentOracle {JNIEnv env;void*self;void*vm;};
EnvironmentOracle* pair_environment=nullptr;
jint PairVM(JNIEnv*,JavaVM**out){Jni();*out=reinterpret_cast<JavaVM*>(&runtime);return JNI_OK;}
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
bool corrupt_info=false,hide_owned=false,hide_variable=false,duplicate_variable=false;
void* variable_local=nullptr;const void* public_slot=nullptr;size_t walks=0;
void OriginalRoots(void* self,abi::arm64::android_common::HandleRootVisitor* visitor,uint32_t flags){
 assert(self==thread.data()&&flags==0&&Read32(self)==0);++walks;
 for(void* scope=Top();scope;){OriginalHeader header{};std::memcpy(&header,scope,sizeof(header));
  if(header.count==-1){
   if(!hide_variable){
    auto* slot=static_cast<uint32_t*>(variable_local);
    abi::arm64::android_common::NativeRootInfo info{nullptr,corrupt_info?5u:4u,0};
    if(*slot){visitor->VisitRoots(&slot,1,info);if(duplicate_variable)visitor->VisitRoots(&slot,1,info);}
   }
   scope=header.previous;continue;
  }
  assert(header.count>=0&&header.count<=32);
  const uint32_t used=sized_scope?Read32(static_cast<unsigned char*>(scope)+12):uint32_t(header.count);
  assert(used<=uint32_t(header.count));std::array<uint32_t*,32> roots{};
  for(uint32_t i=0;i<used;++i)roots[i]=reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(scope)+OriginalReferences()+size_t(i)*4);
  abi::arm64::android_common::NativeRootInfo info{nullptr,corrupt_info?5u:4u,0};
  if(!hide_owned||roots[0]==public_slot)visitor->VisitRoots(roots.data(),size_t(used),info);
  scope=header.previous;
 }
}
// Independent public Object-constructor/JNI root leaves for monitor proofs.
void* private_monitor=nullptr;void* private_top=nullptr;size_t private_created=0,private_globals=0,private_deleted=0;
int private_constructor;[[maybe_unused]] bool original_class_locked=true,private_monitor_locked=false;
jmethodID PrivateMethod(JNIEnv* e,jclass type,const char* name,const char* signature){
 if(!std::strcmp(name,"<init>")){Jni();assert(!std::strcmp(signature,"()V"));return reinterpret_cast<jmethodID>(&private_constructor);}
 return MethodId(e,type,name,signature);
}
jobject PrivateNew(JNIEnv*,jclass type,jmethodID method,va_list){
 Jni();assert(type==reinterpret_cast<jclass>(parent_class)&&method==reinterpret_cast<jmethodID>(&private_constructor));
 private_monitor=heap.New(32);Put32(private_monitor,0,uint32_t(uintptr_t(parent_class)));++private_created;private_top=Top();return reinterpret_cast<jobject>(private_monitor);
}
jobject PrivateGlobal(JNIEnv* e,jobject value){if(private_monitor&&value==private_monitor)++private_globals;return Global(e,value);}
void PrivateDeleteGlobal(JNIEnv* e,jobject value){if(private_monitor&&value==private_monitor){assert(Top()==private_top);++private_deleted;}Delete(e,value);}
void PrivateMonitorJni(){table.GetMethodID=PrivateMethod;table.NewObjectV=PrivateNew;table.NewGlobalRef=PrivateGlobal;table.DeleteGlobalRef=PrivateDeleteGlobal;}
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
 InitJni();PrivateMonitorJni();InitializeHeap();RootHooks hooks;std::string error;
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
 EnvironmentOracle independent_env{{&table},self,&runtime};pair_environment=&independent_env;table.GetJavaVM=PairVM;
 source_handle_roots::Service pair_service;assert(source_handle_roots::Service::Bind(hooks,fields,runnable_jni::Context{self,&independent_env.env,&runtime},&pair_service,&error));
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
 struct Use{source_handle_roots::Service* service;modern_field_metadata::FieldMetadata* metadata;modern_field_storage::Service* fields;source_handle_roots::Service* pair;} use_data{&service,&metadata,&fields,&pair_service};
 auto use=[](const runnable_jni::Context& context,void* p){auto& data=*static_cast<Use*>(p);auto& s=*data.service;auto& metadata=*data.metadata;std::string error;
  void* borrowed=nullptr;assert(s.BorrowHandle(context,public_slot,&borrowed)&&borrowed==old_class);
  if(sized_scope){borrowed=new_class;
   assert(!s.BorrowHandle(context,static_cast<const unsigned char*>(public_slot)-4,&borrowed)&&borrowed==new_class);
   assert(!s.BorrowHandle(context,static_cast<const unsigned char*>(public_slot)+8,&borrowed)&&borrowed==new_class);
  }
  // Independent named source walker visits variable locals which are absent
  // from the public Thread scope chain. Slot values/RootInfo must still match.
  uint32_t local=uint32_t(uintptr_t(old_class));OriginalHeader variable{Top(),-1};
  variable_local=&local;Publish(&variable);
  assert(s.BorrowHandle(context,&local,&borrowed)&&borrowed==old_class);
  assert(s.ObserveAllocatorHandle(context,reinterpret_cast<jobject>(old_class),&local,&error));
  uint32_t outside=local;borrowed=new_class;
  assert(!s.BorrowHandle(context,&outside,&borrowed)&&borrowed==new_class);
  hide_variable=true;assert(!s.BorrowHandle(context,&local,&borrowed));hide_variable=false;
  duplicate_variable=true;assert(!s.BorrowHandle(context,&local,&borrowed));duplicate_variable=false;
  corrupt_info=true;assert(!s.BorrowHandle(context,&local,&borrowed));corrupt_info=false;
  MoveRoots variable_move;variable_move.old_value=local;variable_move.next_value=uint32_t(uintptr_t(new_class));
  OriginalRoots(thread.data(),&variable_move,0);
  assert(s.BorrowHandle(context,&local,&borrowed)&&borrowed==new_class);
  local=0;borrowed=new_class;assert(!s.BorrowHandle(context,&local,&borrowed)&&borrowed==new_class);
  Put32(const_cast<void*>(public_slot),0,uint32_t(uintptr_t(old_class)));
  Publish(variable.previous);variable_local=nullptr;
  // Null entries in a fixed, bounded live scope remain legitimate Handles.
  assert(s.BorrowHandle(context,static_cast<const char*>(public_slot)+4,&borrowed)&&!borrowed);
  const auto calls=jni_calls;
  assert(s.BorrowOwnedStrongGlobal(context,reinterpret_cast<jobject>(old_class),&borrowed)&&borrowed==old_class&&jni_calls==calls);
  auto pair_context=context;pair_context.env=&pair_environment->env;
  void* left=nullptr,*right=nullptr;
  const auto no_jni=jni_calls;const auto baseline=method_identity::MappingOsQueries();
  for(int repeat=0;repeat<3;++repeat){assert(data.pair->BorrowOwnedStrongGlobal(pair_context,reinterpret_cast<jobject>(old_class),&left));assert(data.pair->BorrowOwnedStrongGlobal(pair_context,reinterpret_cast<jobject>(new_class),&right));}
  assert(method_identity::MappingOsQueries()-baseline==12&&jni_calls==no_jni);
  for(size_t count:{size_t(30000),size_t(60000)}){
   const auto before=method_identity::MappingOsQueries();const auto started=std::chrono::steady_clock::now();
   for(size_t index=0;index<count;++index)assert(data.pair->BorrowOwnedStrongPair(pair_context,reinterpret_cast<jobject>(old_class),reinterpret_cast<jobject>(new_class),&left,&right)&&left==old_class&&right==new_class);
   assert(method_identity::MappingOsQueries()-before==count*2&&jni_calls==no_jni);
   const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
   std::cout<<"Actual paired strong-root native mapping benchmark: roots="<<count<<" OSQueries="<<count*2<<" elapsedSeconds="<<elapsed<<"\n";
  }
  auto wrong_pair=pair_context;wrong_pair.self=foreign_thread.data();left=new_class;right=old_class;
  assert(!data.pair->BorrowOwnedStrongPair(wrong_pair,reinterpret_cast<jobject>(old_class),reinterpret_cast<jobject>(new_class),&left,&right)&&left==new_class&&right==old_class);
  wrong_pair=pair_context;wrong_pair.vm=&linker;assert(!data.pair->BorrowOwnedStrongPair(wrong_pair,reinterpret_cast<jobject>(old_class),reinterpret_cast<jobject>(new_class),&left,&right));
  pair_environment->self=foreign_thread.data();assert(!data.pair->BorrowOwnedStrongPair(pair_context,reinterpret_cast<jobject>(old_class),reinterpret_cast<jobject>(new_class),&left,&right));pair_environment->self=context.self;
  assert(!data.pair->BorrowOwnedStrongPair(pair_context,reinterpret_cast<jobject>(old_class),nullptr,&left,&right));
  assert(!data.pair->BorrowOwnedStrongPair(pair_context,reinterpret_cast<jobject>(old_class),reinterpret_cast<jobject>(new_class),&left,&left));
  assert(!data.pair->BorrowOwnedStrongPair(pair_context,reinterpret_cast<jobject>(old_class),reinterpret_cast<jobject>(uintptr_t(UINT32_MAX)+1),&left,&right)&&left==new_class&&right==old_class);
  assert(jni_calls==no_jni);

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
   for(size_t i=0;i<32;++i){slots[i]=pool.Add(old_class,&error);assert(slots[i]);}
   void* anchor_top=Top();
   {auto source_temporary=s.CreateScope(1,&error);assert(source_temporary);source_temporary->Set(0,new_class);void* original_top=Top();
    for(size_t i=32;i<slots.size();++i){slots[i]=pool.Add(old_class,&error);assert(slots[i]&&Top()==original_top);}
    MoveRoots move;move.old_value=uint32_t(uintptr_t(old_class));move.next_value=uint32_t(uintptr_t(new_class));OriginalRoots(thread.data(),&move,0);
    for(auto slot:slots){void* live=nullptr;assert(pool.Read(slot,&live)&&live==new_class);}
    assert(source_temporary->Get(0)==new_class&&Top()==original_top);
   }assert(Top()==anchor_top);
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
  // The actual named collector walker sees all owned compressed roots even
  // when original temporary source scopes sit above the persistent anchor.
  for(size_t count:{size_t(30000),size_t(60000)}){
   source_handle_roots::Pool large(s);std::vector<void*> slots;slots.reserve(count);
   slots.push_back(large.Add(old_class,&error));assert(slots.back());void* anchor=Top();
   {auto temporary=s.CreateScope(1,&error);assert(temporary);temporary->Set(0,old_class);void* source_top=Top();
    for(size_t i=1;i<count;++i){slots.push_back(large.Add(i%2?old_class:new_class,&error));assert(slots.back()&&Top()==source_top);}
    MoveRoots move;move.old_value=uint32_t(uintptr_t(old_class));move.next_value=uint32_t(uintptr_t(new_class));OriginalRoots(thread.data(),&move,0);
    for(auto slot:slots){void* actual=nullptr;assert(large.Read(slot,&actual)&&actual==new_class);}
    assert(temporary->Get(0)==new_class&&Top()==source_top);
   }assert(Top()==anchor);
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
 void* second_unchanged=old_class;auto pair_native=native;pair_native.env=&independent_env.env;assert(!pair_service.BorrowOwnedStrongPair(pair_native,reinterpret_cast<jobject>(old_class),reinterpret_cast<jobject>(new_class),&unchanged,&second_unchanged)&&unchanged==new_class&&second_unchanged==old_class);
 auto foreign_native=native;foreign_native.self=reinterpret_cast<void*>(uintptr_t(1));
 assert(!service.BorrowHandle(foreign_native,public_slot,&unchanged)&&unchanged==new_class);
 assert(method_identity::MappingOsQueries()==native_queries);
 Publish(nullptr);assert(private_created==private_globals&&private_globals==private_deleted);
 std::cout<<"Source handle roots: real field/card oracle prerequisite, named current/TLS self, actual source handle/strong Class/visitor identity, foreign/cyclic/bad root info/hidden owned scopes refuse, nested zeroed roots update and restore exact chain; Native publication refuses\n";
 return 0;
}
