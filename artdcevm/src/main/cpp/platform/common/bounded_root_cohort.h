#pragma once
#include "platform/common/bounded_managed_roots.h"
#include <type_traits>
namespace artdcevm::bounded_managed_roots {
// Ordinary-JNI queue only. Destination is an owner-relative index/role or a
// separately stable Slot*: no native managed pointer survives publication.
// All original-array/retained-slot proof remains solely in Store::StageMany.
template<class Destination> class RootCohort final {
 static_assert(std::is_trivially_copyable<Destination>::value,"Root destination must be a stable value");
 public:
 using Publish=bool(*)(void*,const Destination&,Slot) noexcept;
 RootCohort(Store& roots,JNIEnv* env,std::string* error,void* owner,Publish publish):roots_(roots),env_(env),error_(error),owner_(owner),publish_(publish){
  objects_.reserve(Store::kCohortSize);destinations_.reserve(Store::kCohortSize);
 }
 // A source cohort lives inside this original callback. Preserve the exact
 // Context object; a matching-field copy cannot borrow its original JNI scope.
 // The original Context must remain live; this cohort cannot escape its callback.
 RootCohort(Store& roots,const runnable_jni::Context& context,std::string* error,void* owner,Publish publish):RootCohort(roots,context.env,error,owner,publish){context_=&context;}
 RootCohort(Store&,runnable_jni::Context&&,std::string*,void*,Publish)=delete;
 RootCohort(Store&,const runnable_jni::Context&&,std::string*,void*,Publish)=delete;
 RootCohort(const RootCohort&)=delete;RootCohort& operator=(const RootCohort&)=delete;
 RootCohort(RootCohort&&)=delete;RootCohort& operator=(RootCohort&&)=delete;
 ~RootCohort(){Release();}
 bool Add(jobject object,Destination destination){
  if(failed_||!object||!env_||!publish_||env_->ExceptionCheck()){failed_=true;return false;}
  if(objects_.size()==Store::kCohortSize&&!Flush())return false;
  auto local=env_->NewLocalRef(object);if(!local||env_->ExceptionCheck()){if(local)env_->DeleteLocalRef(local);failed_=true;return false;}
  objects_.push_back(local);destinations_.push_back(destination);return true;
 }
 bool Flush(){
  if(failed_)return false;if(objects_.empty())return true;std::vector<Slot> slots;
  if(!(context_?roots_.StageMany(*context_,objects_,&slots,error_):roots_.StageMany(env_,objects_,&slots,error_))||slots.size()!=destinations_.size()){failed_=true;return false;}
  for(size_t i=0;i<slots.size();++i)if(!publish_(owner_,destinations_[i],slots[i])){failed_=true;return false;}
  Release();return true;
 }
 private:
 void Release()noexcept{for(auto object:objects_)env_->DeleteLocalRef(object);objects_.clear();destinations_.clear();}
 Store& roots_;JNIEnv* env_;std::string* error_;void* owner_;Publish publish_;
 const runnable_jni::Context* context_=nullptr;
 bool failed_=false;
 std::vector<jobject> objects_;std::vector<Destination> destinations_;
};
}
