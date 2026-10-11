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
