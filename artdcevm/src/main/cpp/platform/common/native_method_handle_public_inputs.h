#pragma once
#include "platform/common/native_method_handle_source_calibration.h"
#include <array>
namespace artdcevm::receiver_guard {
// Ordinary public JNI collection. Returned handle/member roots remain alive
// through the entire source producer and are released after that borrow ends.
class MethodHandlePublicInputs final {
 public:
 explicit MethodHandlePublicInputs(JNIEnv*env):env_(env){}
 ~MethodHandlePublicInputs(){for(size_t i=0;i<root_count_;++i)env_->DeleteGlobalRef(roots_[i]);}
 MethodHandlePublicInputs(const MethodHandlePublicInputs&)=delete;
 MethodHandlePublicInputs&operator=(const MethodHandlePublicInputs&)=delete;
 bool Read(jobject input){
  if(attempted_||!env_||!input||env_->ExceptionCheck())return false;
  attempted_=true;samples_.reserve(9);
  Local type{env_,env_->GetObjectClass(input)};if(!type.value||env_->ExceptionCheck())return false;
  auto id=env_->GetFieldID(static_cast<jclass>(type.value),"receiverHandleSamples","[Lcom/github/ytlog/artdcevm/internal/MethodHandleReceiverOracle$Sample;");if(!id||env_->ExceptionCheck())return false;
  Local array{env_,env_->GetObjectField(input,id)};if(!array.value||env_->ExceptionCheck())return false;
  auto count=env_->GetArrayLength(static_cast<jarray>(array.value));if(env_->ExceptionCheck()||count!=9)return false;
  for(jsize i=0;i<9;++i){
   Local sample{env_,env_->GetObjectArrayElement(static_cast<jobjectArray>(array.value),i)};if(!sample.value||env_->ExceptionCheck())return false;
   Local klass{env_,env_->GetObjectClass(sample.value)};if(!klass.value||env_->ExceptionCheck())return false;
   auto handle=env_->GetFieldID(static_cast<jclass>(klass.value),"handle","Ljava/lang/invoke/MethodHandle;");if(!handle||env_->ExceptionCheck())return false;
   auto member=env_->GetFieldID(static_cast<jclass>(klass.value),"member","Ljava/lang/reflect/Member;");if(!member||env_->ExceptionCheck())return false;
   auto kind=env_->GetFieldID(static_cast<jclass>(klass.value),"kind","I");if(!kind||env_->ExceptionCheck())return false;
   Local h{env_,env_->GetObjectField(sample.value,handle)};if(!h.value||env_->ExceptionCheck())return false;
   auto hr=Keep(h.value);if(!hr)return false;
   Local m{env_,env_->GetObjectField(sample.value,member)};if(!m.value||env_->ExceptionCheck())return false;
   auto mr=Keep(m.value);if(!mr)return false;
   jint k=env_->GetIntField(sample.value,kind);if(env_->ExceptionCheck()||k<0)return false;
   samples_.push_back({hr,mr,uint32_t(k)});
  }
  ready_=true;return true;
 }
 const std::vector<PublicHandleSample>*Samples()const noexcept{return ready_?&samples_:nullptr;}
 private:
 struct Local {JNIEnv*env;jobject value;~Local(){if(value)env->DeleteLocalRef(value);}};
 jobject Keep(jobject local){
  if(!local||root_count_==roots_.size()||env_->ExceptionCheck())return nullptr;
  auto root=env_->NewGlobalRef(local);
  // JNI can return a non-null handle together with a pending exception. Own
  // that handle before rejecting, including the eighteenth and final root.
  if(root)roots_[root_count_++]=root;
  return env_->ExceptionCheck()?nullptr:root;
 }
 JNIEnv*env_;std::array<jobject,18>roots_{};size_t root_count_=0;
 std::vector<PublicHandleSample>samples_;bool attempted_=false,ready_=false;
};
}
