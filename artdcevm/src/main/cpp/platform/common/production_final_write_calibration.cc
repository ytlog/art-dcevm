#include "platform/common/production_final_write_calibration.h"
#include "platform/common/source_fixture_interpretation.h"
#include "platform/common/runnable_executor.h"
#include "platform/common/redefinition_gate.h"
#include "platform/abi/arm64/art_field.h"
#include <array>
namespace artdcevm::production_bootstrap {
namespace {
bool Fail(std::string*error,const char*message){if(error)*error=message;return false;}
struct Field {void*native=nullptr;uint32_t original=0;bool changed=false;};
struct Request {
 HookEngine&hooks;const MetadataCalibration&metadata;const source_handle_roots::Service&handles;
 const native_source_stop::Service&stop;final_write_guard::Guard&guard;
 native_selective_interpretation::Service&journal;field_mirror::NamedField leaf;
 std::string*error;bool completed=false;
};
struct Patch {
 Request&request;source_fixture_roots::Owner&roots;bounded_managed_roots::Slot target;
 void*self;std::array<Field,4>&fields;size_t index;bool restore;
};
bool Change(void*raw,native_source_stop::Session&stop,const modern_field_metadata::ReadContext&read)noexcept{
 auto&r=*static_cast<Patch*>(raw);
 try{
  if(!stop.IsStopped(r.self))return false;
  void*klass=nullptr;if(!r.roots.Store().Read(r.target,&klass)||!klass)return false;
  const auto&metadata=r.request.metadata.Fields();const auto&oracles=r.request.metadata.Inputs().FinalFields();
  // Preflight the whole restore cohort before writing any flags. Every native
  // field remains a member of the exact rooted, independently verified Target.
  for(size_t n=0;n<r.fields.size();++n){
   auto&field=r.fields[n];if(r.restore?!field.changed:n!=r.index)continue;
   field_array::View view;uint32_t physical=0;size_t extent=0;
   if(!field.native||!metadata.Fields(klass,(n&1)!=0,&view,nullptr,&read)||
      !view.IndexOf(field.native,&physical)||!modern_field_metadata::ReadMappingExtent(&read,field.native,&extent)||
      extent<sizeof(abi::arm64::Field)||metadata.FieldDeclaringClass(field.native)!=klass)return false;
   auto*native=static_cast<abi::arm64::Field*>(field.native);
   if(native->dex_field_index!=oracles[n].dex_index)return false;
   const auto flags=__atomic_load_n(&native->access_flags,__ATOMIC_RELAXED);
   if(r.restore){if(flags!=(field.original|0x10u))return false;}
   else {
    if(field.changed||(flags&0xffffu)!=oracles[n].modifiers||(flags&0x10u))return false;
    field.original=flags;
   }
  }
  for(size_t n=0;n<r.fields.size();++n){
   auto&field=r.fields[n];if(r.restore?!field.changed:n!=r.index)continue;
   auto*native=static_cast<abi::arm64::Field*>(field.native);
   __atomic_store_n(&native->access_flags,r.restore?field.original:field.original|0x10u,__ATOMIC_RELAXED);
   field.changed=!r.restore;
  }
  return true;
 }catch(...){return false;}
}
bool Cleanup(Patch&patch,const runnable_jni::Context&actual)noexcept{
 bool restored=false;
 try{patch.restore=true;restored=source_fixture_interpretation::Inspect(
     patch.request.stop,actual,patch.roots,&patch,Change,patch.request.error);}catch(...){}
 // Attempt both restorations even when one refuses. Neither borrowed native
 // ownership may survive destruction of the fixture's rooted episode.
 const bool refunded=source_fixture_interpretation::Refund(patch.request.stop,actual,
     patch.roots,patch.request.journal,patch.request.error);
 for(const auto&field:patch.fields)if(field.changed)std::abort();
 if(patch.request.journal.PendingRefund())std::abort();
 return restored&&refunded;
}
bool Observe(const runnable_jni::Context&actual,void*raw){
 auto&r=*static_cast<Request*>(raw);const auto&inputs=r.metadata.Inputs();
 if(!actual.env||actual.env->ExceptionCheck()||!actual.original_leaf||!r.leaf||
    actual.vm!=r.metadata.Runtime().receivers.java_vm||!runnable_jni::Bridge::BorrowOriginalScope(actual)||
    !redefinition_gate::Owned(static_cast<JavaVM*>(actual.vm)))
  return Fail(r.error,"final-write-calibration-actual-original-context-unavailable");
 if(!r.metadata.Fields().VerifyProvenDeclaration(actual,inputs.FinalTargetClass(),
     inputs.FinalFieldsSchema().descriptor.c_str(),inputs.FinalFields().data(),inputs.FinalFields().size(),r.error))return false;
 source_fixture_roots::Owner roots(actual,r.handles,r.metadata.Fields());
 std::vector<bounded_managed_roots::Slot>slots;
 if(!roots.Stage({inputs.FinalWriterClass(),inputs.FinalTargetClass()},&slots,r.error)||slots.size()!=2)return false;
 const abi::common::RunnableJniScope scope{actual.self,actual.env,actual.vm};
 void*writer=actual.original_leaf(scope,inputs.FinalWriter().strong_global_method);
 if(!writer||actual.env->ExceptionCheck())return false;
 std::array<Field,4>fields{};
 for(size_t n=0;n<fields.size();++n){
  void*mirror=nullptr;
  if(!runnable_executor::DecodeStrongGlobal(actual,inputs.FinalFields()[n].strong_global_field,&mirror)||!mirror)return false;
  fields[n].native=r.leaf(mirror);if(!fields[n].native||actual.env->ExceptionCheck())return false;
  for(size_t previous=0;previous<n;++previous)if(fields[previous].native==fields[n].native)return false;
 }
 const auto&oracle=inputs.FinalWriter();
 const native_selective_interpretation::Method method{writer,slots[0],slots[0],oracle.dex_method_index,oracle.java_modifiers};
 if(!r.journal.Prepare(r.hooks,r.metadata.Methods(),r.metadata.Fields(),roots.Store(),&method,1,r.error)||
    !r.journal.PrepareRefund(r.hooks,r.error)||!r.guard.BeginCalibration())return false;
 Patch patch{r,roots,slots[1],actual.self,fields,0,false};bool passed=false;
 try{
  if(source_fixture_interpretation::Activate(r.stop,actual,roots,r.journal,r.error)){
   uint32_t witnessed=0;
   for(size_t n=0;n<fields.size();++n){
    if(!inputs.PrepareFinalWrite(actual.env,n,r.error))break;
    patch.index=n;patch.restore=false;
    if(!source_fixture_interpretation::Inspect(r.stop,actual,roots,&patch,Change,r.error))break;
    const final_write_guard::Oracle proof{actual.self,writer,fields[n].native,n>=2};
    if(!r.guard.BeginProof(proof))break;
    bool illegal=false,unchanged=false;
    const bool executed=inputs.ExerciseFinalWrite(actual.env,n,&illegal,&unchanged,r.error);
    const bool matched=r.guard.EndProof(executed&&illegal,executed&&unchanged);
    patch.restore=true;
    if(!source_fixture_interpretation::Inspect(r.stop,actual,roots,&patch,Change,r.error)||!matched)break;
    witnessed|=1u<<n;
   }
   passed=witnessed==0xfu&&!actual.env->ExceptionCheck();
  }
 }catch(...){
  r.guard.ResetUncommittedProof();Cleanup(patch,actual);throw;
 }
 const bool cleaned=Cleanup(patch,actual);
 r.completed=passed&&cleaned&&!actual.env->ExceptionCheck()&&
     runnable_jni::Bridge::BorrowOriginalScope(actual)&&r.guard.CompleteCalibration();
 if(!r.completed){r.guard.ResetUncommittedProof();return Fail(r.error,"final-write-four-original-storage-episodes-not-proven-or-restored");}
 return true;
}
}
bool CalibrateFinalWrites(HookEngine&hooks,JNIEnv*env,const MetadataCalibration&metadata,
 const source_handle_roots::Service&handles,const native_source_stop::Service&stop,
 final_write_guard::Guard&guard,std::string*error){
 JavaVM*vm=nullptr;
 if(!env||env->ExceptionCheck()||env->GetJavaVM(&vm)!=JNI_OK||vm!=metadata.Runtime().receivers.java_vm||
    !redefinition_gate::Owned(vm)||!handles.Calibrated()||!stop.Calibrated())
  return Fail(error,"final-write-calibration-ordinary-owner-or-source-dependencies-unavailable");
 auto leaf=reinterpret_cast<field_mirror::NamedField>(hooks.FindSymbol("libart.so","_ZN3art6mirror5Field11GetArtFieldEv"));
 if(!leaf)return Fail(error,"final-write-calibration-named-field-reader-unavailable");
 native_selective_interpretation::Service journal;
 if(!journal.CalibrateOwner(hooks,env,error))return false;
 Request request{hooks,metadata,handles,stop,guard,journal,leaf,error};
 const auto result=runnable_executor::WithOriginalScope(env,metadata.Inputs().Anchor(),Observe,&request);
 if(journal.PendingRefund())std::abort();
 const bool completed=result==runnable_jni::Result::kSuccess&&request.completed&&!env->ExceptionCheck();
 if(!completed)guard.ResetUncommittedProof();return completed;
}
}
