#include "platform/common/native_initializer_tid_writer.h"
#include "platform/common/class_metadata_abi.h"
#include "platform/common/runnable_executor.h"
#include <cstring>
namespace artdcevm::native_initializer_tid_writer {
namespace {
bool Fail(std::string*e,const char*s){if(e)*e=s;return false;}
struct Probe{Service*service=nullptr;JNIEnv*env=nullptr;jobject anchor=nullptr,fixture=nullptr;uint32_t tid=0;size_t calls=0;bool accepted=false;};
thread_local Probe*probe=nullptr;
}
struct Producer {
 static bool Borrow(const runnable_jni::Context&c,void*raw){auto&p=*static_cast<Probe*>(raw);return c.env==p.env&&p.service->RoundTrip(c,p.fixture,p.tid);}
 static jboolean Observe(JNIEnv*env,jclass,jclass type,jint tid){
  auto*p=probe;if(!p||p->env!=env||p->calls++||tid<=0||env->ExceptionCheck()||!env->IsSameObject(type,p->fixture))return JNI_FALSE;
  p->tid=uint32_t(tid);
  // JNI MonitorEnter precedes every native borrow; the actual private Class
  // monitor remains held through the complete no-JNI Runnable round trip.
  if(env->MonitorEnter(p->fixture)!=JNI_OK||env->ExceptionCheck())return JNI_FALSE;
  p->accepted=runnable_executor::With(env,p->anchor,Borrow,p)==runnable_jni::Result::kSuccess;
  if(env->MonitorExit(p->fixture)!=JNI_OK||env->ExceptionCheck())p->accepted=false;
  return p->accepted?JNI_TRUE:JNI_FALSE;
 }
};
bool Service::InactiveTransaction()const noexcept{
 if(!calibrated_||!runtime_||!active_)return false;
 try{return !active_(runtime_);}catch(...){return false;}
}
bool Service::ReadyFor(const modern_field_metadata::FieldMetadata&m,const source_handle_roots::Service&r)const noexcept{
 return calibrated_&&metadata_==&m&&roots_==&r&&m.Calibrated()&&r.Calibrated()&&state_.CalibratedFor(m);
}
bool Service::RoundTrip(const runnable_jni::Context&c,jobject fixture,uint32_t tid)noexcept{
 void*klass=nullptr;size_t extent=0;
 if(!metadata_||!roots_||!active_||!record_||!runtime_||!tid||tid>0x3fffffu||
    !roots_->BorrowOwnedStrongGlobal(c,fixture,&klass)||!klass||
    !modern_field_metadata::ReadMappingExtent(nullptr,klass,&extent))return false;
 struct View{const void*klass;size_t size;}view{klass,extent};
 modern_field_metadata::ReadContext context{&view,[](void*p,const void*k,size_t*out){auto&v=*static_cast<View*>(p);if(k!=v.klass||!out)return false;*out=v.size;return true;}};
 native_initializer_state::State before{},changed{},restored{};
 if(!state_.Read(*metadata_,klass,context,&before)||before.status!=13||before.initializing_tid!=tid)return false;
 const uint32_t alternative=tid==1?2:1;
 const size_t offset=metadata_->StorageModel()==modern_field_metadata::Model::kSplit?
     offsetof(class_metadata::Class11,clinit_thread_id):offsetof(class_metadata::Class16,clinit_thread_id);
 bool wrote=false,success=false;
 try{
  // A true transaction is not created or inferred by this public producer.
  // Final stopped use must reject true before any recorder allocation.
  if(active_(runtime_))return false;
  if(setter_)setter_(klass,int32_t(alternative));
  else abi::arm64::android11::initializer_transaction::SetClinitThreadId(runtime_,klass,{offset},int32_t(alternative),active_,record_);
  wrote=true;
  success=state_.Read(*metadata_,klass,context,&changed)&&changed.status==13&&changed.initializing_tid==alternative;
  if(setter_)setter_(klass,int32_t(tid));
  else abi::arm64::android11::initializer_transaction::SetClinitThreadId(runtime_,klass,{offset},int32_t(tid),active_,record_);
  wrote=false;
  return success&&state_.Read(*metadata_,klass,context,&restored)&&restored.status==13&&restored.initializing_tid==tid;
 }catch(...){
  if(wrote){try{if(setter_)setter_(klass,int32_t(tid));else abi::arm64::android11::initializer_transaction::SetClinitThreadId(runtime_,klass,{offset},int32_t(tid),active_,record_);}catch(...){}}
  return false;
 }
}
bool Service::Calibrate(HookEngine&hooks,JNIEnv*env,jobject anchor,
 const modern_field_metadata::FieldMetadata&m,const source_handle_roots::Service&r,
 const runtime_services::Snapshot&runtime,std::string*error){
 if(metadata_)return ReadyFor(m,r)?true:Fail(error,"Initializer writer private calibration already consumed");
 if(!env||!anchor||env->ExceptionCheck()||probe||!m.Calibrated()||!r.Calibrated()||
    !runtime.runtime||!runtime.receivers.java_vm||!runtime.receivers.class_linker)
  return Fail(error,"Initializer writer requires actual runtime/Class/root owners");
 using namespace abi::arm64::android11::initializer_transaction;
 auto active=reinterpret_cast<ActiveTransaction>(hooks.FindSymbol("libart.so",kActive));
 auto record=reinterpret_cast<RecordWrite32>(hooks.FindSymbol("libart.so",kRecordConst));
 if(!record)record=reinterpret_cast<RecordWrite32>(hooks.FindSymbol("libart.so",kRecord));
 auto setter=reinterpret_cast<SetClinitTid>(hooks.FindSymbol("libart.so",kSetTid));
 JavaVM*vm=nullptr;auto current=reinterpret_cast<void*(*)()>(hooks.FindSymbol("libart.so","_ZN3art6Thread14CurrentFromGdbEv"));
 void* singleton=hooks.FindSymbol("libart.so","_ZN3art7Runtime9instance_E");size_t singleton_extent=0;void* actual_runtime=nullptr;
 if(!singleton||!modern_field_metadata::ReadMappingExtent(nullptr,singleton,&singleton_extent)||singleton_extent<sizeof(void*))
  return Fail(error,"Original Runtime singleton identity unavailable");
 std::memcpy(&actual_runtime,singleton,sizeof(actual_runtime));
 if(actual_runtime!=runtime.runtime)return Fail(error,"Initializer writer Runtime differs from actual named source singleton");
 if(!active||!record||!current||env->GetJavaVM(&vm)!=JNI_OK||vm!=runtime.receivers.java_vm||
    !r.OrdinaryNative({current(),env,vm}))return Fail(error,"Original runtime transaction query/recorder or Native owner unavailable");
 if(!state_.Calibrate(env,anchor,m,error))return false;
 metadata_=&m;roots_=&r;runtime_=runtime.runtime;active_=active;record_=record;setter_=setter;
 jclass oracle=env->FindClass("com/github/ytlog/artdcevm/internal/InitializerWriterOracle");
 const JNINativeMethod native{const_cast<char*>("observe"),const_cast<char*>("(Ljava/lang/Class;I)Z"),reinterpret_cast<void*>(Producer::Observe)};
 auto type=oracle?env->GetStaticMethodID(oracle,"type","()Ljava/lang/Class;"):nullptr;
 auto run=oracle?env->GetStaticMethodID(oracle,"run","()I"):nullptr;
 bool ok=type&&run&&!env->ExceptionCheck()&&env->RegisterNatives(oracle,&native,1)==JNI_OK;
 jobject local=ok?env->CallStaticObjectMethod(oracle,type):nullptr;
 jobject fixture=local&&!env->ExceptionCheck()?env->NewGlobalRef(local):nullptr;
 jobject owned=fixture?env->NewGlobalRef(anchor):nullptr;
 Probe p{this,env,owned,fixture};
 if(fixture&&owned&&!env->ExceptionCheck()){
  probe=&p;const jint result=env->CallStaticIntMethod(oracle,run);probe=nullptr;
  ok=!env->ExceptionCheck()&&result==0x13718419&&p.calls==1&&p.accepted;
 }else ok=false;
 if(owned)env->DeleteGlobalRef(owned);if(fixture)env->DeleteGlobalRef(fixture);
 if(local)env->DeleteLocalRef(local);if(oracle)env->DeleteLocalRef(oracle);
 if(!ok)return Fail(error,"Original initializer writer public body/root/monitor/roundtrip oracle rejected");
 calibrated_=true;if(error)error->clear();return true;
}
}
