#include "platform/common/production_installed_dex.h"
#include "platform/common/bootstrap_dex_oracle.h"
#include "platform/common/production_final_dex.h"
#include <cstdlib>

namespace artdcevm::production_bootstrap {
namespace {
bool Fail(std::string*error,const char*why){if(error)*error=why;return false;}
struct Local {JNIEnv*env;jobject value=nullptr;~Local(){if(value)env->DeleteLocalRef(value);}};
}
InstalledDex::~InstalledDex(){
 if(!loader_)return;
 JNIEnv*env=nullptr;
 if(!vm_||vm_->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6)!=JNI_OK||!env)std::abort();
 env->DeleteGlobalRef(loader_);
}
bool InstalledDex::Bind(JNIEnv*env,jvmtiEnv*ti,jclass anchor,std::string*error){
 if(vm_||!env||!ti||!anchor||apk_.empty()||env->ExceptionCheck())
  return Fail(error,"installed-DEX-source-invalid-or-repeated-binding");
 JavaVM*vm=nullptr;JNIEnv*current=nullptr;
 if(env->GetJavaVM(&vm)!=JNI_OK||!vm||vm->GetEnv(reinterpret_cast<void**>(&current),JNI_VERSION_1_6)!=JNI_OK||current!=env)
  return Fail(error,"installed-DEX-source-current-VM-owner-unavailable");
 if(BorrowFinalDexRecords(vm)!=&provenance_)
  return Fail(error,"installed-DEX-source-process-record-or-operation-lease-unavailable");
 Local loader{env};
 if(ti->GetClassLoader(anchor,&loader.value)!=JVMTI_ERROR_NONE||!loader.value||env->ExceptionCheck())
  return Fail(error,"installed-DEX-source-APK-loader-anchor-unavailable");
 jobject retained=env->NewGlobalRef(loader.value);
 if(!retained||env->ExceptionCheck()){if(retained)env->DeleteGlobalRef(retained);return false;}
 vm_=vm;ti_=ti;loader_=retained;return true;
}
bool InstalledDex::Read(const runnable_jni::Context&context,jclass klass,const std::string&descriptor,
 std::vector<unsigned char>*out,std::string*error)const{
 if(!vm_||!ti_||!loader_||!out||!klass||descriptor.empty()||context.vm!=vm_||!context.env||
    context.env->ExceptionCheck()||!runnable_jni::Bridge::BorrowOriginalScope(context))
  return Fail(error,"installed-DEX-source-original-Runnable-owner-unavailable");
 if(BorrowFinalDexRecords(static_cast<JavaVM*>(context.vm))!=&provenance_)
  return Fail(error,"installed-DEX-source-process-record-or-operation-lease-unavailable");
 std::vector<unsigned char>candidate;
 if(provenance_.Find(context.env,klass,&candidate)){
  *out=std::move(candidate);return true;
 }
 if(context.env->ExceptionCheck())return false;
 Local loader{context.env};
 if(ti_->GetClassLoader(klass,&loader.value)!=JVMTI_ERROR_NONE||context.env->ExceptionCheck())
  return Fail(error,"installed-DEX-source-Class-loader-read-failed");
 if(!loader.value||!context.env->IsSameObject(loader.value,loader_)||context.env->ExceptionCheck())
  return Fail(error,"installed-DEX-source-Class-outside-recorded-definitions-and-APK-loader");
 evolution::ClassSchema schema;evolution::MappedDexMemberCounts counts;InstalledClassLocation location;
 if(!SelectInstalledClass(apk_,descriptor.c_str(),&schema,&counts,error,nullptr,&location))return false;
 candidate=apk_[location.file_index];
 *out=std::move(candidate);return true;
}
}
