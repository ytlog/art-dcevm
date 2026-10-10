#define main existing_source_stop_test_main
#include "native_source_stop_test.cc"
#undef main
#include "platform/common/native_typed_heap_walk.h"
namespace {
using namespace native_typed_heap_walk;
using namespace abi::arm64::runtime_root_visit;
int runtime_object,trace_object,class_object;void* trace_pointer=&trace_object;void* class_pointer=&class_object;
uint32_t compressed=7;void* pointer=&heap;int moved_object;bool owned_trace=false,owned_rw=false,shared_rw=false,move_root=false,deny_memory=false,expire=false;
size_t original_visits=0,barriers=0;int32_t Tid(){return 123;}
int32_t MutexTid(const void* p){assert(p==&trace_object);return owned_trace?123:0;}
int32_t RwTid(const void* p){assert(p==&class_object);return owned_rw?123:0;}
bool bad_mutex_owner=false;void* calibration_rw=nullptr;
// Original Mutex/RW C2 and lock leaves model the pinned source declaration;
// they are explicit unit ART leaf boundaries, not an Android16 device oracle.
void ConstructMutex(void*p,const char*n,uint8_t l,bool recursive){assert(!exclusive&&l==8&&!recursive);auto*m=new(p)MutexPrefix;m->name=n;m->level=l;m->respond=false;m->state=0;m->owner=0;m->recursion=0;m->monitor_id=0;}
void DestroyMutex(void*p){static_cast<MutexPrefix*>(p)->~MutexPrefix();}
void LockMutex(void*p,void*t){assert(t==&self&&!exclusive);auto*m=static_cast<MutexPrefix*>(p);m->state=1;m->owner=bad_mutex_owner?456:123;}
void UnlockMutex(void*p,void*t){assert(t==&self&&!exclusive);auto*m=static_cast<MutexPrefix*>(p);m->state=0;m->owner=0;}
void ConstructRw(void*p,const char*n,uint8_t l){assert(!exclusive&&l==8);auto*m=new(p)RwPrefix;m->name=n;m->level=l;m->respond=false;m->state=0;m->owner=0;m->contenders=0;calibration_rw=p;}
void DestroyRw(void*p){static_cast<RwPrefix*>(p)->~RwPrefix();calibration_rw=nullptr;}
void LockRw(void*p,void*t){assert(t==&self&&!exclusive);auto*m=static_cast<RwPrefix*>(p);m->state=-1;m->owner=123;}
void UnlockRw(void*p,void*t){assert(t==&self&&!exclusive);auto*m=static_cast<RwPrefix*>(p);m->state=0;m->owner=0;}
bool Shared(const void*p,const void*t){assert(t==&self);if(p==calibration_rw)return static_cast<const RwPrefix*>(p)->state==-1&&static_cast<const RwPrefix*>(p)->owner==123;if(p==&class_object)return shared_rw;assert(p==class_pointer);return static_cast<const RwPrefix*>(p)->state==-1&&static_cast<const RwPrefix*>(p)->owner==123;}
void* Barrier(void*p){assert(p==&compressed&&exclusive&&gc);++barriers;if(move_root)compressed=9;return compressed==9?&moved_object:&heap;}
void OriginalRoots(void*p,Visitor*v,uint8_t flags){assert(p==&runtime_object&&exclusive&&gc&&flags==129);++original_visits;
 Info pi{nullptr,1,0},ci{nullptr,3,987};void** ps[]{&pointer};uint32_t* cs[]{&compressed};
 v->VisitRoots(ps,1,pi);if(expire)foreign_current=true;v->VisitRoots(cs,1,ci);
}
class RootHooks:public Hooks {public:const char* root_missing=nullptr;
 void* FindSymbol(const char*lib,const char*n)override{if(root_missing&&!std::strcmp(n,root_missing))return nullptr;
 if(!std::strcmp(n,kVisit))return Fn(OriginalRoots);if(!std::strcmp(n,"artReadBarrierForRootSlow"))return Fn(Barrier);
 if(!std::strcmp(n,kTid))return Fn(Tid);if(!std::strcmp(n,kMutexOwner))return Fn(MutexTid);if(!std::strcmp(n,kRwOwner))return Fn(RwTid);if(!std::strcmp(n,kShared))return Fn(Shared);
 if(!std::strcmp(n,kMutexConstruct))return Fn(ConstructMutex);if(!std::strcmp(n,kMutexDestroy))return Fn(DestroyMutex);if(!std::strcmp(n,kMutexLock))return Fn(LockMutex);if(!std::strcmp(n,kMutexUnlock))return Fn(UnlockMutex);
 if(!std::strcmp(n,kRwConstruct))return Fn(ConstructRw);if(!std::strcmp(n,kRwDestroy))return Fn(DestroyRw);if(!std::strcmp(n,kRwLock))return Fn(LockRw);if(!std::strcmp(n,kRwUnlock))return Fn(UnlockRw);
 if(!std::strcmp(n,kTraceLock))return &trace_pointer;if(!std::strcmp(n,kClassLock))return &class_pointer;return Hooks::FindSymbol(lib,n);}
};
bool Extent(void*,const void*p,size_t*out){if(!p||deny_memory)return false;*out=1024;return true;}
struct Roots:RootSink {size_t seen=0;bool deny=false;bool Root(const RootRecord&r)noexcept override{
 assert(exclusive&&gc);if(r.representation==RootRepresentation::Pointer){assert(r.slot==&pointer&&r.value==pointer&&r.type==1&&r.thread_id==0);}
 else{assert(r.slot==&compressed&&r.value==(move_root?static_cast<void*>(&moved_object):static_cast<void*>(&heap))&&r.type==3&&r.thread_id==987);}++seen;return !deny;
}};
}

#ifndef FRAME_RELOCATION_TEST_ONLY
int main(){RootHooks hooks;std::string error;assert(native_source_stop::Install(hooks,&error));native_source_stop::Service service;
 assert(native_source_stop::Service::Bind(hooks,&heap,State(hooks),&service,&error));Calibrate(service);
 RootProvenance roots;assert(roots.Bind(hooks,&runtime_object,Current,&error)&&!roots.LocksCalibrated());
 RootProvenance calibrated;assert(calibrated.Bind(hooks,&runtime_object,Current,&error));
 runnable_jni::Context ordinary{&self,reinterpret_cast<JNIEnv*>(&heap),&runtime_object};
 bad_mutex_owner=true;assert(!calibrated.CalibrateLocks(ordinary,&error)&&!calibrated.LocksCalibrated()&&!calibration_rw);bad_mutex_owner=false;
 assert(calibrated.CalibrateLocks(ordinary,&error)&&calibrated.LocksCalibrated()&&!calibration_rw);
 modern_field_metadata::ReadContext context{&runtime_object,Extent};Roots sink;native_source_stop::Session expired(service);
 assert(!roots.Observe(expired,&self,context,sink).completed&&original_visits==0);
 for(int mode=0;mode<8;++mode){native_source_stop::Session stop(service);assert(stop.Begin(&self,sizeof(self)));const auto previous=original_visits;
 owned_trace=mode==1;owned_rw=mode==2;shared_rw=mode==3;deny_memory=mode==4;sink.deny=mode==5;move_root=mode==6;expire=mode==7;compressed=7;sink.seen=0;
 forbid_native_allocation=true;auto r=roots.Observe(stop,&self,context,sink);forbid_native_allocation=false;
 if(mode==0||mode==6){assert(r.completed&&r.pointer_roots==1&&r.compressed_roots==1&&sink.seen==2);}
 else{assert(!r.completed);if(mode<=4)assert(original_visits==previous);if(mode==5)assert(r.failure==RootReport::Failure::Sink);if(mode==7)assert(r.failure==RootReport::Failure::Owner);}
 foreign_current=false;owned_trace=owned_rw=shared_rw=deny_memory=false;assert(stop.RollbackBeforeSource());assert(!roots.Observe(stop,&self,context,sink).completed);
 }
 alignas(MutexPrefix) unsigned char trace_native[sizeof(MutexPrefix)];alignas(RwPrefix) unsigned char class_native[sizeof(RwPrefix)];
 ConstructMutex(trace_native,"actual-global-trace",8,false);ConstructRw(class_native,"actual-global-classes",8);calibration_rw=nullptr;
 trace_pointer=trace_native;class_pointer=class_native;expire=move_root=false;sink.deny=false;
 {native_source_stop::Session stop(service);assert(stop.Begin(&self,sizeof(self)));
  assert(calibrated.Observe(stop,&self,context,sink).completed);
  reinterpret_cast<MutexPrefix*>(trace_native)->owner=123;assert(!calibrated.Observe(stop,&self,context,sink).completed);reinterpret_cast<MutexPrefix*>(trace_native)->owner=0;
  reinterpret_cast<RwPrefix*>(class_native)->state=-1;reinterpret_cast<RwPrefix*>(class_native)->owner=123;assert(!calibrated.Observe(stop,&self,context,sink).completed);
  reinterpret_cast<RwPrefix*>(class_native)->state=2;assert(calibrated.Observe(stop,&self,context,sink).completed); // other readers: ignore stale exclusive owner
  reinterpret_cast<RwPrefix*>(class_native)->state=0;reinterpret_cast<RwPrefix*>(class_native)->owner=0;
  assert(stop.RollbackBeforeSource());}
 DestroyRw(class_native);DestroyMutex(trace_native);trace_pointer=&trace_object;class_pointer=&class_object;
 RootProvenance missing;hooks.root_missing=kShared;assert(!missing.Bind(hooks,&runtime_object,Current,&error));
 assert(original_visits&&barriers&&!exclusive&&!gc);
 std::cout<<"Original Runtime root visitor unit ART leaves + actual SourceStop: both overloads/slot/type/thread, barrier relocation, caller locks, memory/sink/expired/foreign owner refusal PASS; no public root calibration or narrowing granted\n";
}

#endif
