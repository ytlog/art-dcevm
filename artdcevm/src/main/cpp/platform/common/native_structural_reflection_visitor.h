#pragma once
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <unordered_map>
#include "platform/common/native_reflective_value_abi.h"
#include "platform/common/resolution_invalidation_plan.h"
namespace artdcevm::native_structural_reflection {
namespace detail {
constexpr uint32_t kExecutable=1,kMethodHandle=3,kFieldVarHandle=4,kDexMethod=8,kDexField=9;
using Visit=void (*)(void*,art::ReflectiveValueVisitor*);
inline uint32_t Type(const art::ReflectionSourceInfo& info){uint32_t value;std::memcpy(&value,reinterpret_cast<const char*>(&info)+8,4);return value;}
class Visitor final:public art::ReflectiveValueVisitor {
 public:
  Visitor(const std::unordered_map<void*,void*>& methods,const std::unordered_map<void*,void*>& fields,bool validate,
          bool method_handles=false,bool field_var_handles=false,bool field_method_handles=false,
          const resolution_invalidation::ResolutionInvalidationPlan* resolution=nullptr)
      :methods_(methods),fields_(fields),validate_(validate),
       method_handles_(method_handles),field_var_handles_(field_var_handles),field_method_handles_(field_method_handles),resolution_(resolution){}
  art::ArtMethod* VisitMethod(art::ArtMethod* method,const art::ReflectionSourceInfo& info)override {
    if(!method)return nullptr;
    const auto kind=Type(info);
    if(Invalidate(method,resolution_invalidation::Kind::Method,kind))return validate_?method:nullptr;
    auto found=methods_.find(method);if(found==methods_.end())return method;
    if(validate_){if(!found->second&&kind==kMethodHandle&&!method_handles_)unsupported_=true;return method;}
    if(!found->second)return kind==kDexMethod?nullptr:method;
    if(kind==kExecutable){
      void* object=nullptr;std::memcpy(&object,reinterpret_cast<const char*>(&info)+16,8);
      if(!object)std::abort();
      uint32_t flags;std::memcpy(&flags,static_cast<const char*>(found->second)+4,4);
      __atomic_store_n(reinterpret_cast<uint32_t*>(static_cast<char*>(object)+32),flags,__ATOMIC_RELAXED);
    }
    return static_cast<art::ArtMethod*>(found->second);
  }
  art::ArtField* VisitField(art::ArtField* field,const art::ReflectionSourceInfo& info)override {
    if(!field)return nullptr;
    const auto kind=Type(info);
    if(Invalidate(field,resolution_invalidation::Kind::Field,kind))return validate_?field:nullptr;
    auto found=fields_.find(field);if(found==fields_.end())return field;
    if(validate_){
      if(!found->second&&((kind==kFieldVarHandle&&!field_var_handles_)||
                         (kind==kMethodHandle&&!field_method_handles_)))unsupported_=true;
      return field;
    }
    return !found->second?(kind==kDexField?nullptr:field):static_cast<art::ArtField*>(found->second);
  }
  bool Unsupported()const{return unsupported_;}
  bool ResolutionUnavailable()const{return resolution_unavailable_;}
  const resolution_invalidation::FailureInfo& ResolutionFailure()const{return resolution_failure_;}
 private:
  bool Invalidate(void* member,resolution_invalidation::Kind kind,uint32_t source) {
    if(!resolution_)return false;
    resolution_invalidation::FailureInfo failure;
    const auto decision=resolution_->Inspect(member,kind,source,&failure);
    if(decision==resolution_invalidation::Decision::Unavailable){
      if(!validate_)std::abort();unsupported_=true;if(!resolution_unavailable_)resolution_failure_=failure;resolution_unavailable_=true;return false;
    }
    return decision==resolution_invalidation::Decision::Clear;
  }
  const std::unordered_map<void*,void*>& methods_;const std::unordered_map<void*,void*>& fields_;
  bool validate_,method_handles_,field_var_handles_,field_method_handles_,unsupported_=false,resolution_unavailable_=false;
  resolution_invalidation::FailureInfo resolution_failure_;
  const resolution_invalidation::ResolutionInvalidationPlan* resolution_;
};
}
}
