#include "platform/common/native_typed_heap_walk.h"
#include <atomic>
#include "platform/common/runnable_executor.h"
#include "platform/abi/arm64/art_field.h"
namespace artdcevm::native_typed_heap_walk {
namespace {bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}}
struct Service::Episode {
 const Service& service;void* self;Sink& sink;Episode* previous;
 const void* visitor=nullptr;size_t* failure_counter=nullptr;
 size_t constructors=0,references=0;bool valid=true;
};
thread_local Service::Episode* Service::current_episode_=nullptr;
Service::Ctor Service::original_ctor_=nullptr;
Service::Reference Service::original_reference_=nullptr;
void Service::Construct(void* receiver,void* self,void* heap,size_t* failures,bool referents){
 original_ctor_(receiver,self,heap,failures,referents);
 if(auto* e=current_episode_){
  if(!receiver||self!=e->self||heap!=e->service.heap_||!failures||!referents||e->service.current_()!=self){e->valid=false;return;}
  if(e->failure_counter&&e->failure_counter!=failures)e->valid=false;
  e->visitor=receiver;e->failure_counter=failures;++e->constructors;
 }
}
bool Service::Observe(const void* receiver,void* object,void* value,uint32_t offset){
 // Preserve ART's actual liveness checks, counters and root traversal.
 const bool result=original_reference_(receiver,object,value,offset);
 if(auto* e=current_episode_){
  ++e->references;
  if(receiver!=e->visitor||!e->failure_counter||e->service.current_()!=e->self||!result||
     !e->sink.Reference(object,value,offset))e->valid=false;
 }
 return result;
}
bool Service::Bind(HookEngine& hooks,void* heap,void*(*current)(),std::string* error){
 if(heap_||!heap||!current)return Fail(error,"Typed heap walk lacks the independent Heap/current owner");
 auto verify=reinterpret_cast<Verify>(hooks.FindSymbol("libart.so",kVerifyHeap));
 auto ctor=hooks.FindSymbol("libart.so",kVisitorCtor);auto reference=hooks.FindSymbol("libart.so",kVerifyReference);
 if(!verify||!ctor||!reference)return Fail(error,"Named complete Heap verifier/reference declaration unavailable");
 // One process owner. Multiple callers can share the installed observations;
 // no duplicate inline hook is installed on the same source leaf.
 static std::atomic<bool> installing{false};bool expected=false;
 if(!installing.compare_exchange_strong(expected,true))return Fail(error,"Typed heap observer requires its unique process owner");
 void* ctor_token=hooks.Install(ctor,reinterpret_cast<void*>(Construct),reinterpret_cast<void**>(&original_ctor_),error);
 if(!ctor_token){installing.store(false);return false;}
 void* reference_token=hooks.Install(reference,reinterpret_cast<void*>(Observe),reinterpret_cast<void**>(&original_reference_),error);
 if(!reference_token){std::string ignored;if(hooks.Remove(ctor_token,&ignored))installing.store(false);return false;}
 heap_=heap;current_=current;verify_=verify;return true;
}
bool Service::Walk(void* self,Sink& sink)const noexcept{return Calibrated()&&WalkUncalibrated(self,sink);}
bool Service::WalkUncalibrated(void* self,Sink& sink)const noexcept{
 if(!heap_||!current_||!verify_||!self||current_()!=self||current_episode_)return false;
 Episode episode{*this,self,sink,current_episode_};current_episode_=&episode;
 // This exact original function performs VisitObjectsPaused across every
 // source space/allocation-stack/bitmap and visits real Runtime roots.
 const auto failures=verify_(heap_,true);current_episode_=episode.previous;
 return !failures&&episode.valid&&episode.constructors&&episode.references;
}
namespace {
struct ProbeRequest {Service* service;bool pass=false;};
struct ProbeSink final:Sink {
 void* array;void* left;void* right;size_t first=0,second=0;bool array_root=false,left_root=false,right_root=false;
 ProbeSink(void*a,void*l,void*r):array(a),left(l),right(r){}
 bool Reference(void* object,void* value,uint32_t offset)noexcept override{
  if(!object){array_root|=value==array;left_root|=value==left;right_root|=value==right;return true;}
  if(object!=array)return true;
  if(offset==12){if(value!=left)return false;++first;}
  if(offset==16){if(value!=right)return false;++second;}
  return true;
 }
 bool Passed()const{return first&&second&&array_root&&left_root&&right_root;}
};
}
bool Service::Probe(const exclusive_owned_roots::Context& context,void* raw){
 auto& r=*static_cast<ProbeRequest*>(raw);if(context.count!=3||!context.roots[0]||!context.roots[1]||!context.roots[2]||context.roots[1]==context.roots[2])return false;
 ProbeSink sink(context.roots[0],context.roots[1],context.roots[2]);
 r.pass=r.service->WalkUncalibrated(context.self,sink)&&sink.Passed();return r.pass;
}
bool Service::Calibrate(JNIEnv* env,jobjectArray array,jobject left,jobject right,const exclusive_owned_roots::Service& exclusive,std::string* error){
 if(calibrated_||!heap_||!env||!array||!left||!right||env->ExceptionCheck()||env->GetArrayLength(array)!=2||env->IsSameObject(left,right))return Fail(error,"Typed heap walk needs a distinct public two-reference array oracle");
 auto a=env->GetObjectArrayElement(array,0),b=env->GetObjectArrayElement(array,1);
 const bool public_values=a&&b&&!env->ExceptionCheck()&&env->IsSameObject(a,left)&&env->IsSameObject(b,right);
 if(a)env->DeleteLocalRef(a);if(b)env->DeleteLocalRef(b);if(!public_values)return Fail(error,"Typed heap public array values differ");
 const jobject roots[]{array,left,right};ProbeRequest request{this};
 if(!exclusive.With(env,roots,3,Probe,&request,error)||!request.pass)return Fail(error,"Actual whole-heap source episode/public roots did not match");
 calibrated_=true;return true;
}
}

namespace artdcevm::native_typed_heap_walk {
namespace {
struct FieldsRequest {
 Service* service;const modern_field_metadata::FieldMetadata* metadata;
 void* instance_field;void* static_field;uint32_t instance_offset,static_offset;bool pass=false;
};
struct FieldsSink final:Sink {
 void* instance;void* owner;void* left;void* right;uint32_t instance_offset,static_offset;
 bool instance_seen=false,static_seen=false;bool roots[4]{};
 FieldsSink(void*i,void*o,void*l,void*r,uint32_t io,uint32_t so):instance(i),owner(o),left(l),right(r),instance_offset(io),static_offset(so){}
 bool Reference(void* object,void* value,uint32_t offset)noexcept override{
  if(!object){roots[0]|=value==instance;roots[1]|=value==owner;roots[2]|=value==left;roots[3]|=value==right;return true;}
  if(object==instance&&offset==instance_offset){if(value!=left)return false;instance_seen=true;}
  if(object==owner&&offset==static_offset){if(value!=right)return false;static_seen=true;}
  return true;
 }
 bool Passed()const{return instance_seen&&static_seen&&roots[0]&&roots[1]&&roots[2]&&roots[3];}
};
}
bool Service::ProbeFields(const exclusive_owned_roots::Context& c,void* raw){
 auto&r=*static_cast<FieldsRequest*>(raw);if(c.count!=4||!c.roots[0]||!c.roots[1]||!c.roots[2]||!c.roots[3]||c.roots[2]==c.roots[3]||
    r.metadata->Reference(c.roots[0],0)!=c.roots[1]||r.metadata->FieldDeclaringClass(r.instance_field)!=c.roots[1]||r.metadata->FieldDeclaringClass(r.static_field)!=c.roots[1])return false;
 FieldsSink sink(c.roots[0],c.roots[1],c.roots[2],c.roots[3],r.instance_offset,r.static_offset);
 r.pass=r.service->WalkUncalibrated(c.self,sink)&&sink.Passed();return r.pass;
}
bool Service::CalibrateFields(HookEngine& hooks,JNIEnv* env,jobject instance,jclass owner,jobject reflected_instance,jobject reflected_static,jobject left,jobject right,const modern_field_metadata::FieldMetadata& metadata,const exclusive_owned_roots::Service& exclusive,std::string* error){
 if(fields_calibrated_||!heap_||!env||!instance||!owner||!reflected_instance||!reflected_static||!left||!right||!metadata.Calibrated())return Fail(error,"Typed heap instance/static declaration owner unavailable");
 auto instance_id=env->FromReflectedField(reflected_instance),static_id=env->FromReflectedField(reflected_static);
 auto klass=env->GetObjectClass(instance);auto a=instance_id?env->GetObjectField(instance,instance_id):nullptr;
 auto b=static_id?env->GetStaticObjectField(owner,static_id):nullptr;
 const bool public_match=klass&&a&&b&&!env->ExceptionCheck()&&env->IsSameObject(klass,owner)&&env->IsSameObject(a,left)&&env->IsSameObject(b,right)&&!env->IsSameObject(left,right);
 if(klass)env->DeleteLocalRef(klass);if(a)env->DeleteLocalRef(a);if(b)env->DeleteLocalRef(b);
 if(!public_match)return Fail(error,"Typed heap independent public instance/static values differ");
 using Leaf=void*(*)(void*);auto leaf=reinterpret_cast<Leaf>(hooks.FindSymbol("libart.so","_ZN3art6mirror5Field11GetArtFieldEv"));
 if(!leaf)return Fail(error,"Typed heap actual reflection leaf unavailable");
 struct Request {Leaf leaf;jobject instance_mirror,static_mirror;const modern_field_metadata::FieldMetadata* metadata;std::string* error;FieldsRequest fields;};
 Request request{leaf,reflected_instance,reflected_static,&metadata,error,{this,&metadata,nullptr,nullptr,0,0}};
 const auto borrow=[](const runnable_jni::Context& c,void* raw){auto&r=*static_cast<Request*>(raw);void*im=nullptr,*sm=nullptr;
  if(!runnable_executor::DecodeStrongGlobal(c,r.instance_mirror,&im)||!runnable_executor::DecodeStrongGlobal(c,r.static_mirror,&sm)||!im||!sm)return false;
  auto*ifield=r.leaf(im),*sfield=r.leaf(sm);field_copy::CopyFieldView iv,sv;
  if(!ifield||!sfield||!r.metadata->FieldIdentity(ifield,&iv,r.error)||!r.metadata->FieldIdentity(sfield,&sv,r.error)||!iv.descriptor||!sv.descriptor||
     (iv.descriptor[0]!='L'&&iv.descriptor[0]!='[')||(sv.descriptor[0]!='L'&&sv.descriptor[0]!='[')||
     (static_cast<abi::arm64::Field*>(ifield)->access_flags&8)||!(static_cast<abi::arm64::Field*>(sfield)->access_flags&8))return false;
  r.fields.instance_field=ifield;r.fields.static_field=sfield;r.fields.instance_offset=static_cast<abi::arm64::Field*>(ifield)->offset;r.fields.static_offset=static_cast<abi::arm64::Field*>(sfield)->offset;return true;};
 if(runnable_executor::With(env,owner,borrow,&request)!=runnable_jni::Result::kSuccess)return Fail(error,"Typed heap named reference fields do not match public oracle");
 const jobject roots[]{instance,owner,left,right};
 if(!exclusive.With(env,roots,4,ProbeFields,&request.fields,error)||!request.fields.pass)return Fail(error,"Actual heap episode omitted independently declared instance/static cells or roots");
 fields_calibrated_=true;return true;
}
}

namespace artdcevm::native_typed_heap_walk {
namespace {
bool RootMapped(const modern_field_metadata::ReadContext& c,const void* p,size_t bytes)noexcept{
 size_t extent=0;return p&&bytes&&c.owner&&c.extent&&c.extent(c.owner,p,&extent)&&extent>=bytes;
}
struct SourceRoots final:abi::arm64::runtime_root_visit::Visitor {
 const native_source_stop::Session& stop;void* self;void*(*current)();
 const modern_field_metadata::ReadContext& context;void*(*barrier)(void*);RootSink& sink;RootReport& report;
 SourceRoots(const native_source_stop::Session&s,void*t,void*(*f)(),const modern_field_metadata::ReadContext&c,void*(*b)(void*),RootSink&k,RootReport&r):stop(s),self(t),current(f),context(c),barrier(b),sink(k),report(r){}
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
   const RootRecord record{slot,value,info.type,info.thread_id,compressed?RootRepresentation::Compressed:RootRepresentation::Pointer};
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
RootReport RootProvenance::Observe(const native_source_stop::Session& stop,void* self,const modern_field_metadata::ReadContext& context,RootSink& sink)const noexcept {
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
 SourceRoots visitor(stop,self,current_,context,barrier_,sink,report);
 // The original source internally acquires/releases its own permitted locks;
 // only the caller's pre/post lock ownership is constrained. Callback owner
 // checks do not incorrectly forbid locks legitimately held by VisitRoots.
 visit_(runtime_,&visitor,abi::arm64::runtime_root_visit::kAllPrecise);
 if(!owner())report.failure=RootReport::Failure::Owner;
 else if(!locks())report.failure=RootReport::Failure::Lock;
 report.completed=report.failure==RootReport::Failure::None;return report;
}
}
