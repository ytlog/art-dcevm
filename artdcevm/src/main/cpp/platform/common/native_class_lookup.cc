#include "platform/common/native_class_lookup.h"
#include <new>
namespace artdcevm::native_class_lookup {
namespace {
bool Fail(std::string* e,const char* text){if(e)*e=text;return false;}
}
bool Service::Bind(HookEngine& hooks,const runtime_services::Snapshot& runtime,Service* out,std::string* error){
 if(!out||!runtime.receivers.class_linker)return Fail(error,"Class lookup requires the independently observed ClassLinker");
 Service service;service.linker_=runtime.receivers.class_linker;
 service.plain_=reinterpret_cast<Plain>(hooks.FindSymbol("libart.so","_ZN3art11ClassLinker9FindClassEPNS_6ThreadEPKcNS_6HandleINS_6mirror11ClassLoaderEEE"));
 if(!service.plain_){
  service.hashed_=reinterpret_cast<Hashed>(hooks.FindSymbol("libart.so","_ZN3art11ClassLinker9FindClassEPNS_6ThreadEPKcmNS_6HandleINS_6mirror11ClassLoaderEEE"));
  service.hash_=reinterpret_cast<Hash>(hooks.FindSymbol("libdexfile.so","_ZN3art23ComputeModifiedUtf8HashEPKc"));
  if(!service.hashed_||!service.hash_)return Fail(error,"Named descriptor lookup/hash declarations are incomplete");
 }
 *out=service;return true;
}
bool Service::Calibrate(const runnable_jni::Context& context,jclass strong,
 const modern_field_metadata::FieldMetadata& metadata,source_handle_roots::Service& roots,std::string* error){
 calibrated_=false;
 if(!linker_||(!plain_&&(!hashed_||!hash_))||!metadata.Calibrated()||!roots.ReadyForSourceAccess())return Fail(error,"Class lookup source identities are incomplete");
 try{
  void* actual=nullptr;if(!roots.BorrowOwnedStrongGlobal(context,strong,&actual))return Fail(error,"Class lookup calibration requires an owned public Class");
  field_copy::CopyClassView identity;if(!metadata.ClassIdentity(actual,&identity,error)||!identity.descriptor||identity.descriptor[0]!='L')return Fail(error,"Class lookup requires a public ordinary Class namespace");
  source_handle_roots::Pool pool(roots);auto* expected=pool.Add(actual,error);auto* loader=pool.Add(identity.loader,error);if(!expected||!loader)return false;
  auto result=plain_?plain_(linker_,context.self,identity.descriptor,loader):hashed_(linker_,context.self,identity.descriptor,size_t(hash_(identity.descriptor)),loader);
  void* current=nullptr;if(!pool.Read(expected,&current)||context.env->ExceptionCheck()||result.reference!=uintptr_t(current))return Fail(error,"Named Class lookup did not roundtrip the exact public namespace");
  roots_=&roots;calibrated_=true;return true;
 }catch(const std::bad_alloc&){return Fail(error,"Class lookup calibration native root allocation failed");}
}
abi::arm64::android11::ReleaseObjectPointer Service::Find(const runnable_jni::Context& context,const char* descriptor,const void* loader)const{
 void* actual=nullptr;if(!calibrated_||!roots_||!descriptor||!roots_->BorrowHandle(context,loader,&actual))return {0};
 return plain_?plain_(linker_,context.self,descriptor,loader):hashed_(linker_,context.self,descriptor,size_t(hash_(descriptor)),loader);
}
}
