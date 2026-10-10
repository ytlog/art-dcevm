#pragma once
#include "platform/common/native_receiver_entry_source.h"
namespace artdcevm::receiver_guard {
// One ordinary owner of independently supplied public mirrors and receivers.
// Strong refs are retained only for the synchronous EntrySource calibration.
class EntrySamples final {
 public:
 explicit EntrySamples(JNIEnv*env):env_(env){}
 ~EntrySamples(){for(auto root:roots_)if(root)env_->DeleteGlobalRef(root);}
 EntrySamples(const EntrySamples&)=delete;EntrySamples&operator=(const EntrySamples&)=delete;
 bool Read(jobject input){
  if(attempted_||!env_||!input||env_->ExceptionCheck())return false;attempted_=true;
  Local type{env_,env_->GetObjectClass(input)};if(!type.value||env_->ExceptionCheck())return false;
  const char* names[]={"receiverEntryMethods","receiverEntryFields","receiverEntryObjects"};
  const char* signatures[]={"[Ljava/lang/reflect/Method;","[Ljava/lang/reflect/Field;","[Ljava/lang/Object;"};
  for(size_t group=0;group<3;++group){
   auto id=env_->GetFieldID(static_cast<jclass>(type.value),names[group],signatures[group]);if(!id||env_->ExceptionCheck())return false;
   Local array{env_,env_->GetObjectField(input,id)};if(!array.value||env_->ExceptionCheck())return false;
   if(env_->GetArrayLength(static_cast<jarray>(array.value))!=2||env_->ExceptionCheck())return false;
   for(size_t i=0;i<2;++i){Local value{env_,env_->GetObjectArrayElement(static_cast<jobjectArray>(array.value),static_cast<jsize>(i))};if(!value.value||env_->ExceptionCheck())return false;
    roots_[group*2+i]=env_->NewGlobalRef(value.value);if(!roots_[group*2+i]||env_->ExceptionCheck())return false;
   }
  }
  for(size_t i=0;i<2;++i){samples_.methods[i]=roots_[i];samples_.fields[i]=roots_[2+i];samples_.receivers[i]=roots_[4+i];}ready_=true;return true;
 }
 const EntrySource::Samples* Samples()const noexcept{return ready_?&samples_:nullptr;}
 private:
 struct Local {JNIEnv*env;jobject value;~Local(){if(value)env->DeleteLocalRef(value);}};
 JNIEnv*env_;bool attempted_=false,ready_=false;jobject roots_[6]{};EntrySource::Samples samples_{};
};
}
