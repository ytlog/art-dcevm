#pragma once
#include "platform/abi/arm64/common/method_handle.h"
#include <cstdint>
#include <cstring>
#include <vector>
#include <jni.h>
namespace artdcevm::receiver_guard {
// Fixed original AOSP mirror/method_type.h declaration (form_, descriptor_,
// p_types_, r_type_, wrap_alt_). Android libcore findSpecial prepends the
// specialCaller to the original MethodType. asType/nominal type is not a caller.
// Geometry alone never enables a source: independent public roots below must
// witness every supported construction family before Seal.
using MethodTypeSourcePrefix=abi::arm64::android_common::MethodTypePrefix;
struct MethodHandleSourceAccess {
 // Every leaf runs in the actual source-owned shared mutator scope and may
 // not suspend. Borrowed raw identities never survive a safepoint.
 void* owner=nullptr;
 bool(*current)(void*,void*)noexcept=nullptr;
 // Source-owned readable extent, explicitly no OS lookup or fallback.
 bool(*extent)(void*,void*,const void*,size_t*)noexcept=nullptr;
 // Named/calibrated native read barrier, with no JNI transition or allocation.
 bool(*reference)(void*,void*,void*,size_t,void**)noexcept=nullptr;
 bool(*strong)(void*,void*,jobject,void**)noexcept=nullptr;
 // Actual sole outer MethodHandle owner, not a target offset guess.
 bool(*entry)(void*,void*,void*,uint32_t*,void**,bool*)noexcept=nullptr;
 bool(*declaring)(void*,void*,void*,bool,void**)noexcept=nullptr;
 bool(*special)(void*,void*,void*,void*,bool*)noexcept=nullptr;
};
struct MethodHandleSourceOracle {
 void*thread=nullptr;jobject handle=nullptr,type=nullptr,declaring=nullptr;
 std::vector<jobject> parameter_classes;
 void*member=nullptr;uint32_t kind=UINT32_MAX;bool field=false;
};
class MethodHandleReceiverSource final {
 public:
 explicit MethodHandleReceiverSource(MethodHandleSourceAccess a):access_(a){}
 // Ordinary independently collected handle.type() and
 // parameterType(i), plus decoded public member ID. Roots remain caller-owned.
 bool Observe(const MethodHandleSourceOracle&o)noexcept{
  if(sealed_||o.kind>11||(o.kind>=5&&o.kind<=7))return false;
  const uint32_t bit=uint32_t{1}<<o.kind;observed_&=~bit;
  if(!o.thread||!o.handle||!o.type||!o.declaring||!o.member||o.field!=(o.kind>=8)||
     !Current(o.thread)||!access_.strong||!access_.declaring)return false;
  void *handle=nullptr,*type=nullptr,*parameters=nullptr;uint32_t kind=UINT32_MAX;void*member=nullptr;bool field=false;
  if(!access_.strong(access_.owner,o.thread,o.handle,&handle)||!handle||
     !access_.strong(access_.owner,o.thread,o.type,&type)||!type||
     !Entry(o.thread,handle,&kind,&member,&field)||kind!=o.kind||member!=o.member||field!=o.field||
     !Bounds(o.thread,type,sizeof(MethodTypeSourcePrefix)))return false;
  void*public_declaring=nullptr,*native_declaring=nullptr;
  if(!access_.strong(access_.owner,o.thread,o.declaring,&public_declaring)||!public_declaring||
     !access_.declaring(access_.owner,o.thread,member,field,&native_declaring)||native_declaring!=public_declaring)return false;
  void*actual_type=nullptr;
  if(!Reference(o.thread,handle,offsetof(abi::arm64::android_common::MethodHandlePrefix,method_type),&actual_type)||actual_type!=type||
     !Reference(o.thread,type,offsetof(MethodTypeSourcePrefix,parameters),&parameters)||!parameters||
     !Bounds(o.thread,parameters,12))return false;
  int32_t count=0;std::memcpy(&count,static_cast<const char*>(parameters)+8,4);
  if(count<0||size_t(count)!=o.parameter_classes.size()||size_t(count)>(SIZE_MAX-12)/4||
     !Bounds(o.thread,parameters,12+size_t(count)*4)||(o.kind==1&&count==0))return false;
  for(size_t i=0;i<o.parameter_classes.size();++i){void*expected=nullptr,*actual=nullptr;
   if(!o.parameter_classes[i]||!access_.strong(access_.owner,o.thread,o.parameter_classes[i],&expected)||!expected||
      !Reference(o.thread,parameters,12+i*4,&actual)||actual!=expected)return false;
   if(o.kind==1&&i==0){bool related=false;
    if(!access_.special||!access_.special(access_.owner,o.thread,native_declaring,actual,&related)||!related)return false;
   }
  }
  if(!Current(o.thread))return false;observed_|=bit;return true;
 }
 bool Seal()noexcept{if(sealed_||observed_!=required)return false;sealed_=true;return true;}
 bool Proven()const noexcept{return sealed_;}
 // Production Guard Access.handle adapter. Live caller comes exclusively from
 // the source MethodType's first actual Class root for INVOKE_SUPER (kind 1).
 // Reads retain no borrowed managed pointer across callbacks/safepoints.
 bool Read(void*self,void*handle,uint32_t*kind,void**caller)const noexcept{
  if(!sealed_||!kind||!caller)return false;uint32_t observed=UINT32_MAX;void*member=nullptr;bool field=false;
  if(!Entry(self,handle,&observed,&member,&field)||observed>11)return false;
  void*special=nullptr;
  if(observed==1){void*type=nullptr,*parameters=nullptr;
   if(!Reference(self,handle,offsetof(abi::arm64::android_common::MethodHandlePrefix,method_type),&type)||!type||
      !Bounds(self,type,sizeof(MethodTypeSourcePrefix))||!Reference(self,type,offsetof(MethodTypeSourcePrefix,parameters),&parameters)||
      !parameters||!Bounds(self,parameters,16))return false;
   int32_t count=0;std::memcpy(&count,static_cast<const char*>(parameters)+8,4);
   if(count<=0||!Reference(self,parameters,12,&special)||!special)return false;
  }
  if(!Current(self))return false;*kind=observed;*caller=special;return true;
 }
 private:
 static constexpr uint32_t required=0x0f1fu; // Invoke 0..4, fields 8..11.
 bool Current(void*self)const noexcept{return self&&access_.current&&access_.current(access_.owner,self);}
 bool Bounds(void*self,const void*p,size_t n)const noexcept{size_t extent=0;return p&&Current(self)&&access_.extent&&access_.extent(access_.owner,self,p,&extent)&&extent>=n&&Current(self);}
 bool Reference(void*self,void*object,size_t offset,void**out)const noexcept{return out&&offset<=SIZE_MAX-4&&Bounds(self,object,offset+4)&&access_.reference&&access_.reference(access_.owner,self,object,offset,out)&&Current(self);}
 bool Entry(void*self,void*handle,uint32_t*kind,void**member,bool*field)const noexcept{return Bounds(self,handle,sizeof(abi::arm64::android_common::MethodHandlePrefix))&&access_.entry&&access_.entry(access_.owner,self,handle,kind,member,field)&&member&&*member&&Current(self);}
 MethodHandleSourceAccess access_;uint32_t observed_=0;bool sealed_=false;
};
}
