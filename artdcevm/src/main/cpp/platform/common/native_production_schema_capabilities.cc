#include "platform/common/native_production_schema_capabilities.h"
#include "platform/common/native_hierarchy_private_ledger.h"
#include "platform/abi/arm64/android11/thread_state.h"
#include "platform/abi/arm64/android13/thread_state.h"
#include <cstdlib>
#include <new>
namespace artdcevm::native_production_schema_capabilities {
namespace {
thread_local Owner* active=nullptr;
bool Error(std::string* e,const char* text){if(e)*e=text;return false;}
}
Owner::~Owner(){if(scope_||active==this)std::abort();}
bool Owner::Prepare(HookEngine& hooks,const modern_field_storage::Service& storage,
 const modern_field_metadata::FieldMetadata& metadata,const runtime_services::Snapshot& runtime,
 const runnable_jni::Context& context,std::string* error){
 if(prepared_){
  if(hooks_!=&hooks||storage_!=&storage||metadata_!=&metadata||runtime_.runtime!=runtime.runtime||
     runtime_.receivers.class_linker!=runtime.receivers.class_linker||runtime_.receivers.java_vm!=runtime.receivers.java_vm)
   return Error(error,"Production schema services cannot be rebound to another runtime");
  return ContextMatches(context)||Error(error,"Production schema preparation VM/context differs");
 }
 if(!runtime.runtime||!runtime.receivers.java_vm||!context.env||!context.self||context.vm!=runtime.receivers.java_vm)
  return Error(error,"Production schema preparation requires the actual Runtime/VM/context");
 current_=reinterpret_cast<abi::arm64::android11::CurrentThread>(hooks.FindSymbol("libart.so",abi::arm64::android11::kCurrentThread));
 if(!current_||current_()!=context.self)return Error(error,"Production schema Native preparation current Thread differs");
 JavaVM* actual=nullptr;
 if(context.env->GetJavaVM(&actual)!=JNI_OK||actual!=runtime.receivers.java_vm||context.env->ExceptionCheck())
  return Error(error,"Production schema preparation JNI VM does not match");
 if(!native_hierarchy_allocator::Service::Bind(hooks,metadata,roots_,runtime,&allocator_,error)||
    !native_source_dex_registry::Service::Bind(hooks,runtime,&registry_,error)||
    !native_class_lookup::Service::Bind(hooks,runtime,&lookup_,error)||
    !source_handle_roots::Service::Bind(hooks,storage,context,&roots_,error))return false;
 const auto state=__atomic_load_n(static_cast<const uint32_t*>(context.self),__ATOMIC_RELAXED);
 if((state>>abi::arm64::android13::kStateShift)==abi::arm64::android13::kNative){state_shift_=abi::arm64::android13::kStateShift;native_state_=abi::arm64::android13::kNative;}
 else if((state>>abi::arm64::android11::kStateShift)==abi::arm64::android11::kNative){state_shift_=abi::arm64::android11::kStateShift;native_state_=abi::arm64::android11::kNative;}
 else return Error(error,"Production schema preparation is not ordinary Native JNI");
 native_context_=context;hooks_=&hooks;storage_=&storage;metadata_=&metadata;runtime_=runtime;prepared_=true;return true;
}
bool Owner::ContextMatches(const runnable_jni::Context& c)const noexcept{
 if(!prepared_||!c.env||!c.self||c.vm!=runtime_.receivers.java_vm)return false;
 JavaVM* actual=nullptr;
 return c.env->GetJavaVM(&actual)==JNI_OK&&actual==runtime_.receivers.java_vm&&!c.env->ExceptionCheck();
}
bool Owner::InstallHooks(native_hierarchy_source_checks::FailureObserver observer,std::string* error){
 if(!prepared_||!observer)return Error(error,"Production schema hook owner is incomplete");
 if(installed_)return (observer==observer_&&native_hierarchy_source_checks::Ready(observer))||
   Error(error,"Production schema failure observer differs");
 if(!ContextMatches(native_context_)||current_()!=native_context_.self||
    (__atomic_load_n(static_cast<const uint32_t*>(native_context_.self),__ATOMIC_RELAXED)>>state_shift_)!=native_state_)
  return Error(error,"Production schema hook installation is not its actual ordinary Native context");
 auto retire=reinterpret_cast<void(*)(void*,uint8_t)>(hooks_->FindSymbol("libart.so","_ZN3art6mirror5Class15SetStatusLockedENS_11ClassStatusE"));
 if(!retire)return Error(error,"Missing named original Class retirement boundary");
 native_hierarchy_allocator::Functions functions{};
 functions.find=FindOriginal;functions.failure=native_hierarchy_source_checks::RefuseActualClass;functions.retire_status=retire;
 using namespace abi::arm64::android_common; using namespace ::artdcevm::abi::common;
 auto construct=reinterpret_cast<SuspendConstruct>(hooks_->FindSymbol("libart.so",kSuspendConstruct));
 auto destroy=reinterpret_cast<SuspendDestroy>(hooks_->FindSymbol("libart.so",kSuspendDestroy));
 if(!construct||!destroy)return Error(error,"Production schema installation requires actual named SuspendAll pair");
 if(!native_hierarchy_source_checks::Install(*hooks_,error))return false;
 SuspendAll stopped{};construct(&stopped,"artdcevm production schema capture",false);
 struct Resume{SuspendDestroy destroy;SuspendAll* scope;~Resume(){destroy(scope);}} resume{destroy,&stopped};
 if(!native_hierarchy_private_ledger::Install(*hooks_,runtime_.receivers.class_linker,error)||
    !native_hierarchy_session::Install(*hooks_,allocator_,functions,error))return false;
 native_hierarchy_source_checks::SetFailureObserver(observer);
 observer_=observer;installed_=native_hierarchy_source_checks::Ready(observer);return installed_;
}
bool Owner::Begin(void* driver,const runnable_jni::Context& context,jobject fixture,std::string* error){
 if(!installed_||scope_||active||!driver||!fixture||!ContextMatches(context))
  return Error(error,"Production schema calibration requires the unique original Runnable Run");
 // This source service independently checks current Thread, TLS env and fixed
 // Runnable state even before its HandleScope oracle has become calibrated.
 void* original=nullptr;
 if(!roots_.BorrowOwnedStrongGlobal(context,fixture,&original)||context.env->GetObjectRefType(fixture)!=JNIGlobalRefType||context.env->ExceptionCheck())
  return Error(error,"Production schema calibration Class is not a live owned Runnable global");
 try{
  auto scope=std::make_unique<native_hierarchy_session::CalibrationScope>(driver,context,fixture,roots_,allocator_);
  if(native_hierarchy_session::CalibrationScope::Current()!=scope.get())return Error(error,"Original Run driver did not match calibration source context");
  context_=context;driver_=driver;fixture_=fixture;scope_=std::move(scope);active=this;return true;
 }catch(const std::bad_alloc&){return Error(error,"Production calibration scope allocation failed");}
}
bool Owner::End(void* driver,bool success,std::string* error){
 if(!scope_||active!=this||driver_!=driver)return Error(error,"Production schema calibration Run end differs");
 scope_.reset();
 // Keep the independently checked context available only while calibrating
 // named lookup, so the allocator's second original operation can use it.
 bool ok=false;completed_=false;
 try{
  if(success&&ContextMatches(context_)&&roots_.ReadyForSourceAccess()){
   ok=lookup_.Calibrate(context_,static_cast<jclass>(fixture_),*metadata_,roots_,error)&&
      registry_.Calibrate(context_,fixture_,*metadata_,roots_,error)&&
      allocator_.CalibrateRetirement(context_,fixture_,error);
   if(ok)completed_=true;
  }else Error(error,"Original successful Run and calibrated source Handle are required");
 }catch(const std::bad_alloc&){ok=false;}
 active=nullptr;driver_=nullptr;fixture_=nullptr;context_={};return ok;
}
abi::arm64::android11::ReleaseObjectPointer Owner::FindOriginal(void* linker,void* self,const char* descriptor,void* loader){
 auto* owner=active;
 if(!owner||linker!=owner->runtime_.receivers.class_linker||self!=owner->context_.self||!owner->roots_.ReadyForSourceAccess())return {0};
 return owner->lookup_.Find(owner->context_,descriptor,loader);
}
bool Owner::Ready()const noexcept{return installed_&&completed_&&roots_.Calibrated()&&allocator_.Calibrated()&&lookup_.Calibrated()&&registry_.Calibrated();}
const char* Owner::Diagnostic()const noexcept{
 if(!prepared_)return "production-schema-services-unprepared";
 if(!installed_)return "production-schema-hooks-uninstalled";
 if(!roots_.Calibrated())return "requires-original-source-handle-proof";
 if(!lookup_.Calibrated()||!registry_.Calibrated()||!completed_)return "requires-ordinary-registry-lookup-retirement-proof";
 if(!allocator_.Calibrated())return "requires-second-ordinary-allocation-proof";
 return "production-schema-source-capabilities-calibrated";
}
}
