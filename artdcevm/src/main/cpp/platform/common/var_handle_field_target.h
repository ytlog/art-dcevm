#pragma once
#include "platform/common/native_reflective_value_abi.h"
#include "api/hook_engine.h"
#include "platform/abi/arm64/common/suspend_all.h"
#include "platform/abi/arm64/android14/jni_global_decode.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
namespace artdcevm::var_handle_field {
using Visit=void (*)(void*,art::ReflectiveValueVisitor*);
// Fixed source mirror/var_handle.cc outer bool member, before subclass dispatch.
inline constexpr char kAccess[]="_ZN3art6mirror9VarHandle6AccessENS1_10AccessModeEPNS_11ShadowFrameEPKNS_19InstructionOperandsEPNS_6JValueE";
inline constexpr char kVisit[]="_ZN3art6mirror14FieldVarHandle11VisitTargetEPNS_22ReflectiveValueVisitorE";
inline constexpr char kCurrent[]="_ZN3art6Thread14CurrentFromGdbEv";
struct Declarations {
  void *access=nullptr,*current=nullptr,*construct=nullptr,*destroy=nullptr,*visit=nullptr,*decode=nullptr;
  bool Resolve(HookEngine& hooks){
    access=hooks.FindSymbol("libart.so",kAccess);current=hooks.FindSymbol("libart.so",kCurrent);
    construct=hooks.FindSymbol("libart.so",abi::arm64::android_common::kSuspendConstruct);
    destroy=hooks.FindSymbol("libart.so",abi::arm64::android_common::kSuspendDestroy);
    visit=hooks.FindSymbol("libart.so",kVisit);
    decode=hooks.FindSymbol("libart.so",abi::arm64::android14::kDecodeGlobalJObject);
    if(!decode)decode=hooks.FindSymbol("libart.so",abi::arm64::android11::kDecodeJObject);
    return access&&current&&construct&&destroy&&decode;
  }
};
// Fixed mirror/var_handle.h + managed VarHandle: Object, three Class
// references, access-mode mask, then FieldVarHandle::art_field_. No scanning.
#pragma pack(push,4)
struct Prefix {uint32_t klass,monitor,coordinate0,coordinate1,variable_type;
  int32_t access_modes;int64_t field;};
#pragma pack(pop)
static_assert(sizeof(Prefix)==32&&offsetof(Prefix,field)==24);
class TargetVisitor final:public art::ReflectiveValueVisitor {
 public:
  explicit TargetVisitor(void* object):object_(object){}
  art::ArtMethod* VisitMethod(art::ArtMethod* method,const art::ReflectionSourceInfo&)override {
    valid_=false;return method;
  }
  art::ArtField* VisitField(art::ArtField* field,const art::ReflectionSourceInfo& info)override {
    uint32_t kind=0;void* object=nullptr;
    std::memcpy(&kind,reinterpret_cast<const char*>(&info)+8,4);
    std::memcpy(&object,reinterpret_cast<const char*>(&info)+16,8);
    if(++visits_!=1||kind!=4||object!=object_||!field)valid_=false;
    field_=field;return field; // Strictly readonly: native VisitTarget keeps its target.
  }
  void* Field()const{return valid_&&visits_==1?field_:nullptr;}
 private:
  void* object_;void* field_=nullptr;size_t visits_=0;bool valid_=true;
};
class Reader {
 public:
  explicit Reader(Visit visit=nullptr):visit_(visit){}
  bool ObserveOracle(void* handle,void* independent_field,bool is_static) {
    if(sealed_||!handle||!independent_field)return false;
    void* field=Read(handle);
    if(field!=independent_field)return false;
    for(void* previous:fields_)if(previous==field)return false;
    fields_.push_back(field);static_seen_|=is_static;instance_seen_|=!is_static;
    return true;
  }
  bool Seal(){sealed_=fields_.size()>=2&&static_seen_&&instance_seen_;return sealed_;}
  bool Prepared()const{return sealed_;}
  bool UsesNamedVisitor()const{return visit_!=nullptr;}
  Visit Visitor()const{return visit_;}
  void* Target(void* handle)const{return sealed_&&handle?Read(handle):nullptr;}
  const std::vector<void*>& Fields()const{return fields_;}
 private:
  void* Read(void* handle)const {
    if(visit_){TargetVisitor visitor(handle);visit_(handle,&visitor);return visitor.Field();}
    uint64_t field=0;std::memcpy(&field,static_cast<const char*>(handle)+offsetof(Prefix,field),8);
    return reinterpret_cast<void*>(field);
  }
  Visit visit_;std::vector<void*> fields_;bool sealed_=false,static_seen_=false,instance_seen_=false;
};
// Real callers can forward Access only when a calibrated target is readable.
// Invalid target and retired target each return false after their own exception.
using Access=bool (*)(void*,int,void*,const void*,void*);
struct Dispatch {
  void* owner=nullptr;
  void* (*target)(void*,void*)=nullptr;
  bool (*retired)(void*,void*)=nullptr;
  void (*reject)(void*,void*)=nullptr;
  void (*unavailable)(void*)=nullptr;
  void (*observed)(void*,void*)=nullptr;
  Access original=nullptr;
  bool Invoke(void* handle,int mode,void* frame,const void* operands,void* result)const {
    if(!owner||!target||!retired||!reject||!unavailable||!original)return false;
    void* field=target(owner,handle);
    if(!field){unavailable(owner);return false;}
    if(retired(owner,field)){reject(owner,result);return false;}
    const bool passed=original(handle,mode,frame,operands,result);
    if(passed&&observed)observed(owner,field);
    return passed;
  }
};
}
