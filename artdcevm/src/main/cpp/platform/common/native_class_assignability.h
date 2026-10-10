#pragma once
#include "api/hook_engine.h"
#include "platform/abi/arm64/common/class_assignability.h"
namespace artdcevm::native_class_assignability {
// Actual named source leaf only. No reconstructed Class layout, SDK selection,
// historical descriptor alias, heap coverage or narrowing authority. Caller
// supplies real mutator ownership and independently rooted Class identities.
class Service final {
 public:
  struct Oracle {void* target;void* source;bool expected;};
  bool Bind(HookEngine& hooks)noexcept {
    calibrated_=false;
    object_=reinterpret_cast<abi::arm64::android_common::ClassAssignableObjPtr>(hooks.FindSymbol("libart.so",abi::arm64::android_common::kClassAssignableObjPtr));
    code_=object_?nullptr:reinterpret_cast<abi::arm64::android_common::ClassAssignableCode>(hooks.FindSymbol("libart.so",abi::arm64::android_common::kClassAssignableCode));
    return object_||code_;
  }
  // Oracle outcomes were observed through public JNI before this no-JNI native
  // interval. Calibration requires identity plus asymmetric positive/negative
  // relations, and rejects every noncanonical C result (including 256).
  bool Calibrate(const Oracle* oracles,size_t count)noexcept {
    calibrated_=false;if(!oracles||!count||(!object_&&!code_))return false;
    bool identity=false,positive=false,negative=false;
    for(size_t i=0;i<count;++i){const auto&o=oracles[i];bool actual=false;
      if(!Read(o.target,o.source,&actual)||actual!=o.expected)return false;
      identity|=o.target==o.source&&actual;
      positive|=o.target!=o.source&&actual;negative|=!actual;
    }
    calibrated_=identity&&positive&&negative;return calibrated_;
  }
  bool Assignable(void* target,void* source,bool* result)const noexcept {
    return calibrated_&&Read(target,source,result);
  }
  bool Calibrated()const noexcept{return calibrated_;}
  bool UsesCodeLeaf()const noexcept{return code_!=nullptr;}
 private:
  bool Read(void* target,void* source,bool* result)const noexcept {
    if(!target||!source||!result)return false;
    if(object_){*result=object_(target,source);return true;}
    if(!code_)return false;const size_t value=code_(target,source);
    if(value>1)return false;*result=value==1;return true;
  }
  abi::arm64::android_common::ClassAssignableObjPtr object_=nullptr;
  abi::arm64::android_common::ClassAssignableCode code_=nullptr;
  bool calibrated_=false;
};
}
