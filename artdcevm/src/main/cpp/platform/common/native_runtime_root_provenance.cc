#include "platform/common/native_typed_heap_walk.h"
#include <cstring>

namespace artdcevm::native_typed_heap_walk {
namespace {
bool Fail(std::string*error,const char*text){if(error)*error=text;return false;}
bool RootMapped(const modern_field_metadata::ReadContext& c,const void* p,size_t bytes)noexcept{
 size_t extent=0;return p&&bytes&&c.owner&&c.extent&&c.extent(c.owner,p,&extent)&&extent>=bytes;
}
struct SourceRoots final:abi::arm64::runtime_root_visit::Visitor {
 const native_source_stop::Session& stop;void* self;void*(*current)();
 const modern_field_metadata::ReadContext& context;void*(*barrier)(void*);RootSink& sink;RootReport& report;bool frame_details;
 SourceRoots(const native_source_stop::Session&s,void*t,void*(*f)(),const modern_field_metadata::ReadContext&c,void*(*b)(void*),RootSink&k,RootReport&r,bool frames):stop(s),self(t),current(f),context(c),barrier(b),sink(k),report(r),frame_details(frames){}
 bool Owner()noexcept {if(current()!=self||!stop.IsStopped(self)){report.failure=RootReport::Failure::Owner;return false;}return true;}
 template<class Slot> void Batch(Slot** slots,size_t count,const abi::arm64::runtime_root_visit::Info& info,bool compressed)noexcept {
  if(report.failure!=RootReport::Failure::None)return;
  if(!Owner())return;
  if(count>SIZE_MAX/sizeof(Slot*)||(count&&!RootMapped(context,slots,count*sizeof(Slot*)))||!RootMapped(context,&info,sizeof(info))){report.failure=RootReport::Failure::Memory;return;}
  for(size_t i=0;i<count;++i){
   Slot* slot=nullptr;std::memcpy(&slot,slots+i,sizeof(slot));
   if(!Owner())return;
   if(!RootMapped(context,slot,compressed?sizeof(uint32_t):sizeof(void*))){report.failure=RootReport::Failure::Memory;return;}
   void* value=nullptr;
   if(compressed)value=barrier(slot);else std::memcpy(&value,slot,sizeof(value));
   if(!Owner())return;
   RootRecord record{slot,value,info.type,info.thread_id,compressed?RootRepresentation::Compressed:RootRepresentation::Pointer};
   if(frame_details&&info.type==abi::arm64::runtime_root_visit::kJavaFrameRoot){
    using JavaFrame=abi::arm64::runtime_root_visit::JavaFrameInfo;
    if(!RootMapped(context,&info,sizeof(JavaFrame))){report.failure=RootReport::Failure::Memory;return;}
    JavaFrame frame{};std::memcpy(&frame,&info,sizeof(frame));
    if(!frame.visitor){report.failure=RootReport::Failure::Original;return;}
    record.original_frame_visitor=frame.visitor;record.vreg=frame.vreg;
   }
   if(compressed)++report.compressed_roots;else ++report.pointer_roots;
   if(!sink.Root(record)){report.failure=RootReport::Failure::Sink;return;}
   if(!Owner())return;
  }
 }
 void VisitRoots(void*** slots,size_t n,const abi::arm64::runtime_root_visit::Info& info)override {Batch(slots,n,info,false);}
 void VisitRoots(uint32_t** slots,size_t n,const abi::arm64::runtime_root_visit::Info& info)override {Batch(slots,n,info,true);}
};
}
bool RootProvenance::Bind(HookEngine& hooks,void* runtime,void*(*current)(),std::string* error){
 if(runtime_||!runtime||!current)return Fail(error,"Runtime root provenance requires a fresh independent Runtime/current owner");
 using namespace abi::arm64::runtime_root_visit;
 const auto find=[&](const char*name){void*p=hooks.FindSymbol("libart.so",name);return p?p:hooks.FindSymbol("libartbase.so",name);};
 auto visit=reinterpret_cast<Visit>(find(kVisit));auto barrier=reinterpret_cast<void*(*)(void*)>(find("artReadBarrierForRootSlow"));
 auto tid=reinterpret_cast<int32_t(*)()>(find(kTid));auto mo=reinterpret_cast<OwnerTid>(find(kMutexOwner));auto ro=reinterpret_cast<OwnerTid>(find(kRwOwner));auto shared=reinterpret_cast<SharedHeld>(find(kShared));
 auto cl=reinterpret_cast<void**>(find(kClassLock));auto tl=reinterpret_cast<void**>(find(kTraceLock));
 if(!visit||!barrier||!tid||!shared||!cl||!tl)return Fail(error,"Named Runtime root visitor/barrier or source lock-owner proof unavailable");
 runtime_=runtime;current_=current;visit_=visit;barrier_=barrier;tid_=tid;mutex_owner_=mo;rw_owner_=ro;shared_=shared;class_lock_=cl;trace_lock_=tl;
 mutex_construct_=reinterpret_cast<MutexConstruct>(find(kMutexConstruct));rw_construct_=reinterpret_cast<RwConstruct>(find(kRwConstruct));
 mutex_destroy_=reinterpret_cast<Destroy>(find(kMutexDestroy));rw_destroy_=reinterpret_cast<Destroy>(find(kRwDestroy));
 mutex_lock_=reinterpret_cast<Lock>(find(kMutexLock));mutex_unlock_=reinterpret_cast<Lock>(find(kMutexUnlock));rw_lock_=reinterpret_cast<Lock>(find(kRwLock));rw_unlock_=reinterpret_cast<Lock>(find(kRwUnlock));return true;
}
bool RootProvenance::CalibrateLocks(const runnable_jni::Context& c,std::string* error){
 using namespace abi::arm64::runtime_root_visit;constexpr uint8_t level=kGenericBottom;
 if(locks_calibrated_||!runtime_||!c.self||!c.env||!c.vm||current_()!=c.self||!mutex_construct_||!rw_construct_||!mutex_destroy_||!rw_destroy_||!mutex_lock_||!mutex_unlock_||!rw_lock_||!rw_unlock_)
  return Fail(error,"Original private Mutex/RW constructor/lock oracle unavailable");
 // Compute addresses from the actual inherited source declarations, not
 // hardcoded offsets; never dereference a foreign source vptr as our model.
 MutexPrefix model_mutex;RwPrefix model_rw;
 const auto offset=[](const void*base,const void*member){return uintptr_t(member)-uintptr_t(base);};
 const auto ms=offset(&model_mutex,&model_mutex.state),mo_at=offset(&model_mutex,&model_mutex.owner);
 const auto rs=offset(&model_rw,&model_rw.state),ro_at=offset(&model_rw,&model_rw.owner);
 alignas(MutexPrefix) unsigned char mutex[sizeof(MutexPrefix)]{};
 alignas(RwPrefix) unsigned char rw[sizeof(RwPrefix)]{};
 const char*mn="art-dcevm-root-provenance-mutex",*rn="art-dcevm-root-provenance-rw";
 mutex_construct_(mutex,mn,level,false);rw_construct_(rw,rn,level);
 const auto integer=[](const unsigned char*p,size_t at){int32_t v;std::memcpy(&v,p+at,sizeof(v));return v;};
 const auto pointer=[](const unsigned char*p,size_t at){const char*v;std::memcpy(&v,p+at,sizeof(v));return v;};
 const auto byte=[](const unsigned char*p,size_t at){return p[at];};
 const auto tid=tid_();bool valid=tid>0&&pointer(mutex,offset(&model_mutex,&model_mutex.name))==mn&&pointer(rw,offset(&model_rw,&model_rw.name))==rn&&byte(mutex,offset(&model_mutex,&model_mutex.level))==level&&byte(rw,offset(&model_rw,&model_rw.level))==level&&!byte(mutex,offset(&model_mutex,&model_mutex.recursive))&&integer(mutex,ms)==0&&integer(rw,rs)==0&&integer(mutex,mo_at)==0&&integer(rw,ro_at)==0&&!shared_(rw,c.self);
 mutex_lock_(mutex,c.self);valid=valid&&current_()==c.self&&integer(mutex,mo_at)==tid&&integer(mutex,ms)==1;
 mutex_unlock_(mutex,c.self);valid=valid&&integer(mutex,mo_at)==0&&integer(mutex,ms)==0;
 rw_lock_(rw,c.self);valid=valid&&current_()==c.self&&integer(rw,ro_at)==tid&&integer(rw,rs)==-1&&shared_(rw,c.self);
 rw_unlock_(rw,c.self);valid=valid&&integer(rw,ro_at)==0&&integer(rw,rs)==0&&!shared_(rw,c.self);
 rw_destroy_(rw);mutex_destroy_(mutex);
 if(!valid)return Fail(error,"Original private source Mutex/RW owner oracle differs from pinned declarations");
 mutex_owner_offset_=mo_at;rw_owner_offset_=ro_at;rw_state_offset_=rs;locks_calibrated_=true;return true;
}
RootReport RootProvenance::Observe(const native_source_stop::Session& stop,void* self,const modern_field_metadata::ReadContext& context,RootSink& sink)const noexcept {return ObserveImpl(stop,self,context,sink,true);}
RootReport RootProvenance::ObserveFrameRoots(const native_source_stop::Session& stop,void* self,const modern_field_metadata::ReadContext& context,RootSink& sink)const noexcept {return ObserveImpl(stop,self,context,sink,false);}
RootReport RootProvenance::ObserveImpl(const native_source_stop::Session& stop,void* self,const modern_field_metadata::ReadContext& context,RootSink& sink,bool precise)const noexcept {
 RootReport report;
 if(!runtime_||!current_||!visit_||(!locks_calibrated_&&(!mutex_owner_||!rw_owner_)))return report;
 const auto owner=[&](){return self&&current_()==self&&stop.IsStopped(self);};
 if(!owner()){report.failure=RootReport::Failure::Owner;return report;}
 if(!RootMapped(context,class_lock_,sizeof(void*))||!RootMapped(context,trace_lock_,sizeof(void*))){report.failure=RootReport::Failure::Memory;return report;}
 void* classes=nullptr,*trace=nullptr;std::memcpy(&classes,class_lock_,sizeof(classes));std::memcpy(&trace,trace_lock_,sizeof(trace));
 if(!RootMapped(context,classes,sizeof(void*))||!RootMapped(context,trace,sizeof(void*))){report.failure=RootReport::Failure::Memory;return report;}
 if(locks_calibrated_&&(!RootMapped(context,trace,sizeof(abi::arm64::runtime_root_visit::MutexPrefix))||!RootMapped(context,classes,sizeof(abi::arm64::runtime_root_visit::RwPrefix)))){report.failure=RootReport::Failure::Memory;return report;}
 const auto locks=[&](){const auto tid=tid_();int32_t mo=0,ro=0;
  if(locks_calibrated_){mo=__atomic_load_n(reinterpret_cast<const int32_t*>(static_cast<const unsigned char*>(trace)+mutex_owner_offset_),__ATOMIC_RELAXED);
   // Exact original source RW getter: unheld=0, reader-held=-1; only
   // exclusive state consumes owner. A stale owner under readers is no grant.
   const auto state=__atomic_load_n(reinterpret_cast<const int32_t*>(static_cast<const unsigned char*>(classes)+rw_state_offset_),__ATOMIC_RELAXED);
   ro=state==0?0:state>0?-1:__atomic_load_n(reinterpret_cast<const int32_t*>(static_cast<const unsigned char*>(classes)+rw_owner_offset_),__ATOMIC_RELAXED);}
  else{mo=mutex_owner_(trace);ro=rw_owner_(classes);}
  return tid>0&&mo!=tid&&ro!=tid&&!shared_(classes,self);};
 if(!locks()||!owner()){report.failure=RootReport::Failure::Lock;return report;}
 report.failure=RootReport::Failure::None;
 SourceRoots visitor(stop,self,current_,context,barrier_,sink,report,!precise);
 // The original source internally acquires/releases its own permitted locks;
 // only the caller's pre/post lock ownership is constrained. Callback owner
 // checks do not incorrectly forbid locks legitimately held by VisitRoots.
 visit_(runtime_,&visitor,precise?abi::arm64::runtime_root_visit::kAllPrecise:abi::arm64::runtime_root_visit::kAll);
 if(!owner())report.failure=RootReport::Failure::Owner;
 else if(!locks())report.failure=RootReport::Failure::Lock;
 report.completed=report.failure==RootReport::Failure::None;return report;
}
}
