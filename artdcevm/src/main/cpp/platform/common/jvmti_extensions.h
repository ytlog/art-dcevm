#pragma once
#include <jvmti.h>
#include <cstring>
namespace artdcevm::jvmti_extensions {
using Structural=jvmtiError(*)(jvmtiEnv*,jint,const jvmtiClassDefinition*);
// Own the public JVMTI enumeration, including nested allocations, across any
// native allocation failure. Function pointers remain valid after Deallocate.
class Functions final {
 public:
 explicit Functions(jvmtiEnv*env):env_(env){}
 ~Functions(){
  if(!values_)return;
  for(jint n=0;n<count_;++n){auto&f=values_[n];
   Release(f.id);Release(f.short_description);
   if(f.params)for(jint p=0;p<f.param_count;++p)Release(f.params[p].name);
   Release(f.params);Release(f.errors);
  }
  Release(values_);
 }
 Functions(const Functions&)=delete;Functions&operator=(const Functions&)=delete;
 jvmtiError Load(){
  if(!env_)return JVMTI_ERROR_INVALID_ENVIRONMENT;
  if(loaded_)return JVMTI_ERROR_ILLEGAL_ARGUMENT;
  loaded_=true;return env_->GetExtensionFunctions(&count_,&values_);
 }
 jint Count()const noexcept{return count_;}
 const jvmtiExtensionFunctionInfo&At(jint n)const noexcept{return values_[n];}
 private:
 template<class T>void Release(T*p){if(p)env_->Deallocate(reinterpret_cast<unsigned char*>(p));}
 jvmtiEnv*env_;jvmtiExtensionFunctionInfo*values_=nullptr;jint count_=0;bool loaded_=false;
};
inline jvmtiError FindStructural(jvmtiEnv*env,Structural*out){
 if(!out)return JVMTI_ERROR_NULL_POINTER;*out=nullptr;
 Functions functions(env);const auto result=functions.Load();if(result!=JVMTI_ERROR_NONE)return result;
 Structural candidate=nullptr;
 for(jint n=0;n<functions.Count();++n){const auto&f=functions.At(n);
  if(f.id&&!std::strcmp(f.id,"com.android.art.class.structurally_redefine_classes")){
   if(candidate||!f.func)return JVMTI_ERROR_INTERNAL;
   candidate=reinterpret_cast<Structural>(f.func);
  }
 }
 if(!candidate)return JVMTI_ERROR_NOT_AVAILABLE;
 *out=candidate;return JVMTI_ERROR_NONE;
}
}
