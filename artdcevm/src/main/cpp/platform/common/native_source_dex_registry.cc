#include "platform/common/native_source_dex_registry.h"
#include "platform/common/class_metadata_abi.h"
#include "platform/common/dex_mapping_abi.h"
#include "platform/common/method_identity_calibration.h"
#include <cstring>
#include <new>
namespace artdcevm::native_source_dex_registry {
namespace {
bool Mapped(const void*p,size_t n){size_t extent=0;return p&&method_identity::ReadableMappingExtent(p,&extent)&&extent>=n;}
bool Error(std::string*e,const char*s){if(e)*e=s;return false;}
const void*Dex(void*cache){
 if(!Mapped(cache,24))return nullptr;
 uint64_t native;std::memcpy(&native,static_cast<unsigned char*>(cache)+16,sizeof(native));
 auto*file=reinterpret_cast<const void*>(uintptr_t(native));
 return Mapped(file,sizeof(dex_mapping_abi::Prefix))?file:nullptr;
}
}
bool Service::Bind(HookEngine&hooks,const runtime_services::Snapshot&s,Service*out,std::string*error){
 if(!out||!s.receivers.class_linker)return Error(error,"DEX registry requires independently observed ClassLinker");
 Service value;value.linker_=s.receivers.class_linker;
 value.registered_=reinterpret_cast<Registered>(hooks.FindSymbol("libart.so","_ZN3art11ClassLinker19IsDexFileRegisteredEPNS_6ThreadERKNS_7DexFileE"));
 value.find_=reinterpret_cast<Find>(hooks.FindSymbol("libart.so","_ZN3art11ClassLinker12FindDexCacheEPNS_6ThreadERKNS_7DexFileE"));
 if(!value.registered_||!value.find_)return Error(error,"Missing fixed named DEX registry declarations");
 *out=value;return true;
}
bool Service::Calibrate(const runnable_jni::Context&ctx,jobject fixture,const modern_field_metadata::FieldMetadata&metadata,source_handle_roots::Service&roots,std::string*error){
 if(!registered_||!find_||!ctx.env||!ctx.self||!metadata.Calibrated()||!roots.Calibrated())return Error(error,"DEX registry calibration requires actual metadata and source roots");
 try{
  void*klass=nullptr;if(!roots.BorrowOwnedStrongGlobal(ctx,fixture,&klass))return Error(error,"DEX registry fixture is not in the actual Runnable owned global context");
  source_handle_roots::Pool pool(roots);auto*class_root=pool.Add(klass,error);if(!class_root)return false;
  auto*cache_root=pool.Add(metadata.Reference(klass,offsetof(class_metadata::Class11,dex_cache)),error);if(!cache_root)return false;
  void*cache=nullptr;if(!pool.Read(cache_root,&cache))return Error(error,"DEX registry public cache root could not be read in the actual source context");const void*file=Dex(cache);if(!file)return Error(error,"DEX registry public DexCache has no readable fixed native DexFile");
  if(!registered_(linker_,ctx.self,file)||!pool.Read(cache_root,&cache)||Dex(cache)!=file)return Error(error,"Public fixture DEX is not in actual source registry");
  const auto found=find_(linker_,ctx.self,file);
  if(!pool.Read(class_root,&klass)||!pool.Read(cache_root,&cache)||Dex(cache)!=file||found.reference!=uintptr_t(cache)||metadata.Reference(klass,offsetof(class_metadata::Class11,dex_cache))!=cache)return Error(error,"Native registry does not return the exact public fixture DexCache");
  calibrated_=true;return true;
 }catch(const std::bad_alloc&){return Error(error,"DEX registry calibration roots exhausted native storage");}
}
bool Service::Unregistered(const runnable_jni::Context&ctx,source_handle_roots::Pool&pool,const void*cache_root,const void*file,std::string*error)const{
 if(!calibrated_||!ctx.env||!ctx.self||!file||!pool.ReadyForSourceAccess())return Error(error,"Unregistered source cache lacks calibrated registry/root owner");
 void*cache=nullptr;if(!pool.Read(cache_root,&cache)||Dex(cache)!=file)return Error(error,"Private source cache DEX identity differs");
 const bool registered=registered_(linker_,ctx.self,file);
 if(!pool.Read(cache_root,&cache)||Dex(cache)!=file||registered)return Error(error,"Incoming source DEX is already registered or moved outside its root");
 const auto found=find_(linker_,ctx.self,file);
 if(!pool.Read(cache_root,&cache)||Dex(cache)!=file||found.reference)return Error(error,"Incoming source DEX has a published registry cache");
 return true;
}
}
