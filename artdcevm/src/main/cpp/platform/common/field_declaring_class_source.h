#pragma once
#include "api/hook_engine.h"
#include "platform/abi/arm64/android11/jni_object_decode.h"
#include "platform/common/field_array_view.h"
#include "platform/common/jvmti_field_flags.h"
#include <unordered_set>
#include <string>
namespace artdcevm::field_declaring_class_source {
// Fixed ArtField::GetDeclaringClass<kWithReadBarrier> calls GcRoot::Read
// with its actual ArtField GcRootSource. The ObjPtr return carrier is a
// candidate until exact independently rooted public owners prove its codec.
class Source {
 using Original=abi::arm64::android11::ReleaseObjectPointer (*)(void*);
 using Generic=void* (*)(void*);
 Original original_=nullptr;Generic generic_=nullptr;
 bool bound_=false,ready_=false,failed_=false,statics_=false,instances_=false;
 std::unordered_set<void*> observed_;
 static bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
 void* Candidate(void* field)const{return original_?reinterpret_cast<void*>(original_(field).reference):generic_(field);}
 public:
 static constexpr const char* kOriginal="_ZN3art8ArtField17GetDeclaringClassILNS_17ReadBarrierOptionE0EEENS_6ObjPtrINS_6mirror5ClassEEEv";
 bool Bind(HookEngine& hooks,std::string* error){
  if(bound_)return true;
  original_=reinterpret_cast<Original>(hooks.FindSymbol("libart.so",kOriginal));
  generic_=reinterpret_cast<Generic>(hooks.FindSymbol("libart.so","artReadBarrierForRootSlow"));
  if(!original_&&!generic_)return Fail(error,"Field declaring source has no original getter/root candidate");
  bound_=true;return true;
 }
 void Begin(){ready_=false;failed_=false;statics_=instances_=false;observed_.clear();}
 // Called immediately after the caller decodes the current owned public Class,
 // with an original Field::GetArtField from its retained public fixture mirror.
 // No JNI/safepoint or retained managed pointer is introduced by this producer.
 bool Observe(void* field,void* public_owner,uint32_t public_flags,std::string* error){
  if(!bound_||failed_||!field||!public_owner){failed_=true;return Fail(error,"Field declaring source lacks current public/native calibration owner");}
  if(jvmti_field_flags::Public(field_array::Flags(field))!=jvmti_field_flags::Public(public_flags)){
   failed_=true;return Fail(error,"Field declaring source native modifiers/static storage differ from public oracle");
  }
  if(Candidate(field)!=public_owner){failed_=true;return Fail(error,original_?"Original ArtField declaring Class ObjPtr codec differs from exact public owner":"Generic field GcRoot reader differs from exact public owner");}
  if(!observed_.insert(field).second){failed_=true;return Fail(error,"Field declaring source public native oracle duplicated");}
  if(public_flags&8u)statics_=true;else instances_=true;return true;
 }
 void Reject(){failed_=true;ready_=false;}
 bool Complete(std::string* error){
  ready_=bound_&&!failed_&&statics_&&instances_&&observed_.size()>=2;
  if(!ready_)return Fail(error,"Field declaring source requires distinct original public static and instance oracles");
  observed_.clear();return true;
 }
 bool Ready()const{return ready_;}
 bool Named()const{return original_!=nullptr;}
 void* Read(void* field)const{return ready_&&field?Candidate(field):nullptr;}
 const char* MappingFailure()const{return original_?"Field declaring Class source prefix is not readable [producer=original ArtField::GetDeclaringClass<kWithReadBarrier>]":"Field declaring Class source prefix is not readable [producer=calibrated generic GcRoot reader]";}
};
}
