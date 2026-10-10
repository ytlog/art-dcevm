#include "platform/common/native_initializer_state_calibration.h"
#include "platform/common/class_metadata_abi.h"
#include "platform/common/runnable_executor.h"
#include <cstring>
namespace artdcevm::native_initializer_state {
namespace {
constexpr uint32_t kTidUnusedBitsMask=abi::arm64::android17::kClinitTidUnusedBitsMask;
constexpr jint kMarker=0x18491315;
constexpr size_t kLegacyTid=offsetof(class_metadata::Class11,clinit_thread_id);
constexpr size_t kMergedTid=offsetof(class_metadata::Class17,clinit_thread_id_or_hash);
static_assert(kMergedTid==offsetof(class_metadata::Class16,clinit_thread_id));
bool Fail(std::string* e,const char* text){if(e)*e=text;return false;}
bool ReadRaw(const modern_field_metadata::FieldMetadata& metadata,const void* klass,
    const modern_field_metadata::ReadContext* context,State* result)noexcept{
 if(!result||!metadata.Calibrated())return false;
 const bool split=metadata.StorageModel()==modern_field_metadata::Model::kSplit;
 const auto& layout=split?class_metadata::kLegacy:class_metadata::k17;
 size_t extent=0;class_metadata::State state{};
 if(!modern_field_metadata::ReadMappingExtent(context,klass,&extent)||
    !class_metadata::ReadState(layout,klass,extent,&state))return false;
 const size_t offset=split?kLegacyTid:kMergedTid;
 const uint32_t raw=__atomic_load_n(reinterpret_cast<const uint32_t*>(
     static_cast<const unsigned char*>(klass)+offset),__ATOMIC_SEQ_CST);
 *result={state.status,0,raw};return true;
}
struct Probe {
 JNIEnv* env=nullptr;jobject anchor=nullptr,fixture=nullptr;
 jclass oracle=nullptr;jmethodID reenter=nullptr;
 const modern_field_metadata::FieldMetadata* metadata=nullptr;
 State during{},after{};jint tid=0,hash=0;size_t observations=0;
 bool initialized=false;
};
thread_local Probe* active=nullptr;
bool Borrow(const runnable_jni::Context& context,void* data){
 auto& p=*static_cast<Probe*>(data);void* klass=nullptr;
 if(context.env!=p.env||!context.self||!runnable_executor::DecodeStrongGlobal(context,p.fixture,&klass))return false;
 return ReadRaw(*p.metadata,klass,nullptr,p.initialized?&p.after:&p.during);
}
jboolean Observe(JNIEnv* env,jclass,jclass fixture,jint tid,jint hash){
 auto* p=active;
 if(!p||env!=p->env||p->observations||tid<=0||
    !env->IsSameObject(fixture,p->fixture)||env->ExceptionCheck())return JNI_FALSE;
 p->tid=tid;p->hash=hash;++p->observations;
 if(runnable_executor::With(env,p->anchor,Borrow,p)!=runnable_jni::Result::kSuccess||
    p->during.status!=13||p->during.raw_slot!=uint32_t(tid))return JNI_FALSE;
 // A genuine public static access reenters the original ClassLinker while the
 // same Linux thread owns status13. It must return the unassigned default cell,
 // execute no second body/observer, and leave that live state/tid unchanged.
 if(!p->oracle||!p->reenter||env->CallStaticIntMethod(p->oracle,p->reenter)!=0||
    env->ExceptionCheck()||p->observations!=1)return JNI_FALSE;
 return runnable_executor::With(env,p->anchor,Borrow,p)==runnable_jni::Result::kSuccess&&
     p->during.status==13&&p->during.raw_slot==uint32_t(tid)?JNI_TRUE:JNI_FALSE;
}
}
bool Calibration::Calibrate(JNIEnv* env,jobject anchor,
    const modern_field_metadata::FieldMetadata& metadata,std::string* error){
 metadata_=nullptr;
 if(!env||!anchor||active||!metadata.Calibrated()||env->ExceptionCheck())
  return Fail(error,"Class initialization calibration requires the actual ordinary JNI owner");
 jclass oracle=env->FindClass("com/github/ytlog/artdcevm/internal/InitializerStateOracle");
 if(!oracle||env->ExceptionCheck())return Fail(error,"Private initialization oracle unavailable");
 const JNINativeMethod observer{const_cast<char*>("observe"),const_cast<char*>("(Ljava/lang/Class;II)Z"),reinterpret_cast<void*>(Observe)};
 jmethodID inactive=env->GetStaticMethodID(oracle,"inactiveClass","()Ljava/lang/Class;");
 jmethodID run=env->GetStaticMethodID(oracle,"run","()I");
 jmethodID reenter=env->GetStaticMethodID(oracle,"reenter","()I");
 jmethodID failed_class=env->GetStaticMethodID(oracle,"failedClass","()Ljava/lang/Class;");
 jmethodID run_failure=env->GetStaticMethodID(oracle,"runFailure","()I");
 bool ok=inactive&&run&&reenter&&failed_class&&run_failure&&!env->ExceptionCheck()&&env->RegisterNatives(oracle,&observer,1)==JNI_OK;
 jobject local=ok?env->CallStaticObjectMethod(oracle,inactive):nullptr;
 jobject fixture=local&&!env->ExceptionCheck()?env->NewGlobalRef(local):nullptr;
 jobject owned_anchor=fixture?env->NewGlobalRef(anchor):nullptr;
 Probe probe{};probe.env=env;probe.anchor=owned_anchor;probe.fixture=fixture;
 probe.oracle=oracle;probe.reenter=reenter;probe.metadata=&metadata;
 if(!fixture||!owned_anchor||env->ExceptionCheck())ok=false;
 if(ok){active=&probe;const jint value=env->CallStaticIntMethod(oracle,run);active=nullptr;
  ok=!env->ExceptionCheck()&&value==kMarker&&probe.observations==1;
  if(ok){probe.initialized=true;ok=runnable_executor::With(env,owned_anchor,Borrow,&probe)==runnable_jni::Result::kSuccess&&probe.after.status>=14&&probe.after.status<=15;}}
 TidStorage storage=TidStorage::kDedicated;
 if(ok){
  if(probe.after.raw_slot==uint32_t(probe.tid))storage=TidStorage::kDedicated;
  else if(metadata.StorageModel()==modern_field_metadata::Model::kMerged&&
      (probe.after.raw_slot&kTidUnusedBitsMask)!=0&&probe.after.raw_slot==~uint32_t(probe.hash))
   storage=TidStorage::kDescriptorHashShared;
  else ok=false;
 }
 // Independent public failure semantics must agree with the same status
 // declaration: one EIIE body, later NCDFE without rerunning it, then source
 // ErrorResolved(2), because static access ran a resolved Class initializer. This remains a fixture observation, not waiter authority.
 jobject failure_local=ok?env->CallStaticObjectMethod(oracle,failed_class):nullptr;
 jobject failure=failure_local&&!env->ExceptionCheck()?env->NewGlobalRef(failure_local):nullptr;
 if(ok){
  ok=failure&&!env->ExceptionCheck()&&env->CallStaticIntMethod(oracle,run_failure)==1&&
      !env->ExceptionCheck();
  if(ok){probe.fixture=failure;probe.initialized=true;
   ok=runnable_executor::With(env,owned_anchor,Borrow,&probe)==runnable_jni::Result::kSuccess&&
       probe.after.status==2;}
 }
 if(failure)env->DeleteGlobalRef(failure);
 if(failure_local)env->DeleteLocalRef(failure_local);
 if(owned_anchor)env->DeleteGlobalRef(owned_anchor);
 if(fixture)env->DeleteGlobalRef(fixture);if(local)env->DeleteLocalRef(local);env->DeleteLocalRef(oracle);
 if(!ok)return Fail(error,"Actual public clinit body/state/tid/hash oracle disagrees with fixed source declarations");
 metadata_=&metadata;storage_=storage;if(error)error->clear();return true;
}
bool Calibration::CalibratedFor(const modern_field_metadata::FieldMetadata& metadata)const noexcept{
 return metadata_==&metadata&&metadata.Calibrated();
}
bool Calibration::Read(const modern_field_metadata::FieldMetadata& metadata,const void* klass,
    const modern_field_metadata::ReadContext& context,State* output)const noexcept{
 State value{};
 if(!output||!context.owner||!context.extent||!CalibratedFor(metadata)||
    !ReadRaw(metadata,klass,&context,&value))return false;
 if(storage_==TidStorage::kDedicated)value.initializing_tid=value.raw_slot;
 else if((value.raw_slot&kTidUnusedBitsMask)==0)value.initializing_tid=value.raw_slot;
 else if(value.status==13)return false;
 if(value.status==13&&value.initializing_tid==0)return false;
 *output=value;return true;
}
}
