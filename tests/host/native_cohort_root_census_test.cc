// Real Pool/SourceStop/RootProvenance/TypedHeap services. Only original ART/JNI
// leaves are unit doubles; this is not production no-escape calibration.
#include "cohort_fixture.inc"
#include "platform/common/native_cohort_root_census.h"
#include "platform/common/readable_mapping_scope.h"
namespace {
using namespace artdcevm;
using namespace abi::arm64::runtime_root_visit;
std::array<const void*,2> census_slots{};void* census_a=nullptr;void* census_b=nullptr;void* census_outside=nullptr;
uint32_t foreign_slot=0;void* foreign_pointer=nullptr;int census_mode=-1;
MutexPrefix census_trace;RwPrefix census_classes;void* census_trace_ptr=&census_trace;void* census_class_ptr=&census_classes;
int32_t CensusTid(){return 123;}int32_t CensusOwner(const void*){return 0;}
bool CensusShared(const void*p,const void*self){assert(self==thread.data());if(p==census_class_ptr)return false;auto*m=static_cast<const RwPrefix*>(p);return m->state==-1&&m->owner==123;}
void CensusMutexConstruct(void*p,const char*n,uint8_t l,bool recursive){assert(!stopped&&l==8&&!recursive);auto*m=new(p)MutexPrefix;m->name=n;m->level=l;m->respond=false;m->state=0;m->owner=0;m->recursion=0;m->monitor_id=0;}
void CensusMutexDestroy(void*p){static_cast<MutexPrefix*>(p)->~MutexPrefix();}
void CensusMutexLock(void*p,void*self){assert(!stopped&&self==current);auto*m=static_cast<MutexPrefix*>(p);m->state=1;m->owner=123;}
void CensusMutexUnlock(void*p,void*self){assert(!stopped&&self==current);auto*m=static_cast<MutexPrefix*>(p);m->state=0;m->owner=0;}
void CensusRwConstruct(void*p,const char*n,uint8_t l){assert(!stopped&&l==8);auto*m=new(p)RwPrefix;m->name=n;m->level=l;m->respond=false;m->state=0;m->owner=0;m->contenders=0;}
void CensusRwDestroy(void*p){static_cast<RwPrefix*>(p)->~RwPrefix();}
void CensusRwLock(void*p,void*self){assert(!stopped&&self==current);auto*m=static_cast<RwPrefix*>(p);m->state=-1;m->owner=123;}
void CensusRwUnlock(void*p,void*self){assert(!stopped&&self==current);auto*m=static_cast<RwPrefix*>(p);m->state=0;m->owner=0;}

void CensusOriginalRoots(void*r,Visitor*v,uint8_t flags){assert(r==&::runtime&&flags==129&&stopped&&gc);
 // Simulate the original source's complete HandleScope chain, including
 // foreign-owned slots. The census never decides which roots to enumerate.
 for(void* scope=Top();scope;){OriginalHeader header{};std::memcpy(&header,scope,sizeof(header));
  if(header.count==-1){if(variable_local&&Read32(variable_local)){auto*slot=static_cast<uint32_t*>(variable_local);Info info{nullptr,4,19};v->VisitRoots(&slot,1,info);}scope=header.previous;continue;}
  assert(header.count>=0&&header.count<=32);
  const auto used=sized_scope?Read32(static_cast<char*>(scope)+12):uint32_t(header.count);
  for(uint32_t i=0;i<used;++i){auto*slot=reinterpret_cast<uint32_t*>(static_cast<char*>(scope)+OriginalReferences()+4*i);if(!*slot)continue;
   if(census_mode==1&&slot==census_slots[1])continue;
   Info info{nullptr,4,census_mode==4&&slot==census_slots[1]?20u:19u};v->VisitRoots(&slot,1,info);
  }scope=header.previous;
 }
 if(census_mode==2||census_mode==3||census_mode==9){
  if(census_mode==9){foreign_pointer=Root(const_cast<void*>(census_slots[0]));void** p=&foreign_pointer;Info info{nullptr,1,0};v->VisitRoots(&p,1,info);}
  else{foreign_slot=uint32_t(uintptr_t(Root(const_cast<void*>(census_slots[0]))));auto*p=&foreign_slot;Info info{nullptr,census_mode==2?4u:5u,census_mode==2?20u:0u};v->VisitRoots(&p,1,info);}
 }
}
size_t CensusOriginalHeap(void*h,bool refs){if(census_mode<0)return SchemaTypedOriginalHeap(h,refs);
 assert(stopped&&gc&&h==&owner_heap&&refs);size_t failures=0;uintptr_t visitor=0;schema_typed_ctor(&visitor,current,h,&failures,true);
 auto*a=Root(const_cast<void*>(census_slots[0]));auto*b=Root(const_cast<void*>(census_slots[1]));
 schema_typed_ref(&visitor,a,b,8);schema_typed_ref(&visitor,b,a,8);
 if(census_mode==10)schema_typed_ref(&visitor,census_a,b,8); // Stage-era pointer is now outside the refreshed cohort.
 if(census_mode==5)schema_typed_ref(&visitor,census_outside,b,8);
 if(census_mode==6)++failures;
 return failures;
}
class CensusHooks:public OwnerHooks {public:
 void* FindSymbol(const char*lib,const char*n)override{
  if(!std::strcmp(n,kMutexConstruct))return reinterpret_cast<void*>(CensusMutexConstruct);
  if(!std::strcmp(n,kMutexDestroy))return reinterpret_cast<void*>(CensusMutexDestroy);
  if(!std::strcmp(n,kMutexLock))return reinterpret_cast<void*>(CensusMutexLock);
  if(!std::strcmp(n,kMutexUnlock))return reinterpret_cast<void*>(CensusMutexUnlock);
  if(!std::strcmp(n,kRwConstruct))return reinterpret_cast<void*>(CensusRwConstruct);
  if(!std::strcmp(n,kRwDestroy))return reinterpret_cast<void*>(CensusRwDestroy);
  if(!std::strcmp(n,kRwLock))return reinterpret_cast<void*>(CensusRwLock);
  if(!std::strcmp(n,kRwUnlock))return reinterpret_cast<void*>(CensusRwUnlock);
  if(!std::strcmp(n,kVisit))return reinterpret_cast<void*>(CensusOriginalRoots);
  if(!std::strcmp(n,kTid))return reinterpret_cast<void*>(CensusTid);
  if(!std::strcmp(n,kMutexOwner)||!std::strcmp(n,kRwOwner))return reinterpret_cast<void*>(CensusOwner);
  if(!std::strcmp(n,kShared))return reinterpret_cast<void*>(CensusShared);
  if(!std::strcmp(n,kTraceLock))return &census_trace_ptr;
  if(!std::strcmp(n,kClassLock))return &census_class_ptr;
  if(!std::strcmp(n,native_typed_heap_walk::kVerifyHeap))return reinterpret_cast<void*>(CensusOriginalHeap);
  return OwnerHooks::FindSymbol(lib,n);
 }
};
}
#include "cohort_setup.inc"
  // From this point we are inside the original runnable executor callback with
  // the actual calibrated source Pool provider, SourceStop and TypedHeap.
  (void)ti;
  source_handle_roots::Pool cohort(*d.roots);source_handle_roots::Pool foreign(*d.roots);
  census_a=heap.New(16);census_b=heap.New(16);census_outside=heap.New(16);
  census_slots={cohort.Add(census_a,&error),cohort.Add(census_b,&error)};
  auto* foreign_owned=foreign.Add(census_a,&error);assert(foreign_owned);
  runnable_jni::Context census_context=context;census_context.vm=reinterpret_cast<JavaVM*>(&::runtime);
  native_cohort_root_census::Census census;
  const void* foreign_stage[]{foreign_owned,census_slots[1]};
  assert(!census.Stage(census_hooks,cohort,*d.roots,census_context,Current,foreign_stage,2));
  const void* duplicate[]{census_slots[0],census_slots[0]};assert(!census.Stage(census_hooks,cohort,*d.roots,census_context,Current,duplicate,2));
  auto bad_context=census_context;bad_context.self=&linker;assert(!census.Stage(census_hooks,cohort,*d.roots,bad_context,Current,census_slots.data(),2));
  bad_context=census_context;bad_context.vm=reinterpret_cast<JavaVM*>(&linker);assert(!census.Stage(census_hooks,cohort,*d.roots,bad_context,Current,census_slots.data(),2));
  assert(!census.Stage(census_hooks,cohort,*d.roots,census_context,+[]()->void*{return thread.data();},census_slots.data(),2));
  assert(census.Stage(census_hooks,cohort,*d.roots,census_context,Current,census_slots.data(),2));
  assert(foreign.DropManagedReferences());
  native_typed_heap_walk::RootProvenance provenance;assert(provenance.Bind(census_hooks,&::runtime,Current,&error));
  assert(provenance.CalibrateLocks(census_context,&error)&&provenance.LocksCalibrated());
  method_identity::ScopedReadableMappings mapping;assert(mapping.Ready());
  modern_field_metadata::ReadContext stopped_context{&mapping,+[](void*p,const void*address,size_t*out){return stopped&&current==thread.data()&&static_cast<method_identity::ScopedReadableMappings*>(p)->Extent(address,out);}};
  // Delete the real JNI global witness before the positive. Its value alone
  // never authorizes a Runtime JNI-global slot.
  const auto globals_before=SchemaTypedGlobals();
  auto strong=table.NewGlobalRef(&env,reinterpret_cast<jobject>(census_a));assert(strong);
  table.DeleteGlobalRef(&env,strong);
  using Failure=native_cohort_root_census::Census::Failure;
  for(int mode=0;mode<11;++mode){census_mode=mode;
   if(mode==7){ // Real GC-updated owned slot; membership is not Stage's value.
    auto*moved=heap.New(16);assert(cohort.Write(const_cast<void*>(census_slots[0]),moved));
   }
   if(mode==8)assert(foreign.Write(foreign_owned,Root(const_cast<void*>(census_slots[0]))));
   jobject live_global=nullptr;if(mode==9)live_global=table.NewGlobalRef(&env,reinterpret_cast<jobject>(Root(const_cast<void*>(census_slots[0]))));
   native_source_stop::Session stop(source);assert(stop.Begin(context.self,thread.size()));
   const auto allocations=method_identity::MappingOsQueries();forbid_allocation=true;
   auto result=census.Observe(stop,stopped_context,provenance,typed_heap);
   forbid_allocation=false;assert(method_identity::MappingOsQueries()==allocations);
   assert(stop.RollbackBeforeSource());
   std::cerr<<"census mode="<<mode<<" failure="<<int(result.failure)<<" rootfailure="<<int(result.roots.failure)<<" owned="<<result.owned_slots<<" incoming="<<result.incoming_edges<<" internal="<<result.internal_edges<<"\n";
   if(mode==0||mode==7)assert(result.completed&&result.owned_slots==2&&result.internal_edges==2&&result.observed_thread_id==19);
   else{assert(!result.completed);
    if(mode==1)assert(result.failure==Failure::MissingSlot);
    if(mode==2||mode==3||mode==8||mode==9)assert(result.failure==Failure::ForeignRoot);
    if(mode==4)assert(result.failure==Failure::ThreadIdentity);
    if(mode==5||mode==10)assert(result.failure==Failure::ForeignIncoming);
    if(mode==6)assert(result.failure==Failure::HeapStream);
   }
   if(live_global)table.DeleteGlobalRef(&env,live_global);if(mode==8)assert(foreign.DropManagedReferences());assert(!census.Observe(stop,stopped_context,provenance,typed_heap).completed);
  }
  assert(SchemaTypedGlobals()==globals_before);
  assert(cohort.DropManagedReferences());native_source_stop::Session empty_stop(source);assert(empty_stop.Begin(context.self,thread.size()));
  assert(census.Observe(empty_stop,stopped_context,provenance,typed_heap).failure==Failure::RootRead);assert(empty_stop.RollbackBeforeSource());
  return true;
 };
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),run,&use)==runnable_jni::Result::kSuccess);
 std::cout<<"Actual Pool + Session + Runtime/wholeheap service wrappers: exact root slots, missing/foreign thread/Class/JNI roots, cycles/external incoming edges, original heap failure and relocated cohort PASS; unit original ART/JNI leaves, strong-root+incoming census ONLY (no weak/frame/native/noescape permission)\n";
}
