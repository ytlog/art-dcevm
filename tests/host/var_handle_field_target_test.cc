#include "platform/common/var_handle_field_target.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <string>
namespace art {
class ReflectionSourceInfo {public:virtual ~ReflectionSourceInfo()=default;
  uint32_t kind=4,pad=0;void* object=nullptr;};
}
namespace {
using namespace artdcevm::var_handle_field;
struct Mirror {void* field;bool wrong_kind=false,wrong_owner=false,double_visit=false,method_visit=false;};
void NativeVisit(void* receiver,art::ReflectiveValueVisitor* visitor){
  auto& mirror=*static_cast<Mirror*>(receiver);art::ReflectionSourceInfo source;
  source.kind=mirror.wrong_kind?3:4;source.object=mirror.wrong_owner?nullptr:receiver;
  if(mirror.method_visit){visitor->VisitMethod(nullptr,source);return;}
  auto* field=reinterpret_cast<art::ArtField*>(mirror.field);
  assert(visitor->VisitField(field,source)==field);
  if(mirror.double_visit)assert(visitor->VisitField(field,source)==field);
}
struct Hook final:artdcevm::HookEngine {
  std::string missing;bool no_decode=false;int symbol=0;size_t installs=0;
  bool Initialize(std::string*)override{return true;}
  void* FindSymbol(const char* library,const char* name)override{
    assert(std::string(library)=="libart.so");
    // Independent source/ELF catalog: accepting arbitrary names hid a real
    // CurrentFromGdb mangling typo. Do not derive this catalog from Resolver.
    static const char* known[]={
      "_ZN3art6mirror9VarHandle6AccessENS1_10AccessModeEPNS_11ShadowFrameEPKNS_19InstructionOperandsEPNS_6JValueE",
      "_ZN3art6Thread14CurrentFromGdbEv",
      "_ZN3art16ScopedSuspendAllC2EPKcb",
      "_ZN3art16ScopedSuspendAllD2Ev",
      "_ZN3art6mirror14FieldVarHandle11VisitTargetEPNS_22ReflectiveValueVisitorE",
      "_ZNK3art6Thread19DecodeGlobalJObjectEP8_jobject",
      "_ZNK3art6Thread13DecodeJObjectEP8_jobject"};
    if(no_decode&&(std::string(name)=="_ZNK3art6Thread19DecodeGlobalJObjectEP8_jobject"||std::string(name)=="_ZNK3art6Thread13DecodeJObjectEP8_jobject"))return nullptr;
    for(const auto* actual:known)if(std::string(name)==actual)return missing==name?nullptr:&symbol;
    return nullptr;
  }
  void* Install(void*,void*,void**,std::string*)override{++installs;return &symbol;}
  bool Remove(void*,std::string*)override{return true;}
};
struct Owner {Reader* reader;void* retired=nullptr;int exception=0,observations=0,rejections=0;};
void* Target(void* raw,void* handle){return static_cast<Owner*>(raw)->reader->Target(handle);}
bool Retired(void* raw,void* field){return static_cast<Owner*>(raw)->retired==field;}
void Reject(void* raw,void* result){auto& owner=*static_cast<Owner*>(raw);
  ++owner.rejections;owner.exception=1;if(result)*static_cast<uint64_t*>(result)=0;}
void Unavailable(void* raw){static_cast<Owner*>(raw)->exception=2;}
void Observed(void* raw,void*){++static_cast<Owner*>(raw)->observations;}
int originals=0;void *expected_handle=nullptr,*expected_frame=nullptr;const void* expected_operands=nullptr;
Owner* original_owner=nullptr;bool original_result=true;
bool Original(void* handle,int mode,void* frame,const void* operands,void* result){
  assert(handle==expected_handle&&frame==expected_frame&&operands==expected_operands&&mode==19);
  ++originals;*static_cast<uint64_t*>(result)=0x1234;
  if(!original_result)original_owner->exception=3;
  return original_result;
}
}
int main(){
  int field_a=0,field_b=0,field_other=0;
  Mirror a{&field_a},b{&field_b};const auto original_a=a;
  Reader named(NativeVisit);
  assert(!named.Target(&a));assert(!named.Seal());
  assert(!named.ObserveOracle(&a,&field_other,false));
  assert(named.ObserveOracle(&a,&field_a,false));assert(!named.Seal());
  assert(!named.ObserveOracle(&a,&field_a,true));
  assert(named.ObserveOracle(&b,&field_b,true));assert(named.Seal());
  assert(named.Target(&a)==&field_a&&named.Target(&b)==&field_b);
  assert(a.field==original_a.field&&a.wrong_kind==original_a.wrong_kind);
  assert(!named.ObserveOracle(&a,&field_a,false));
  for(int failure=0;failure<4;++failure){Mirror invalid{&field_a};
    invalid.wrong_kind=failure==0;invalid.wrong_owner=failure==1;
    invalid.double_visit=failure==2;invalid.method_visit=failure==3;
    assert(!named.Target(&invalid));Reader candidate(NativeVisit);
    assert(!candidate.ObserveOracle(&invalid,&field_a,false));assert(!candidate.Seal());}
  Prefix first{},second{};first.field=reinterpret_cast<uintptr_t>(&field_a);
  second.field=reinterpret_cast<uintptr_t>(&field_b);
  first.klass=0x111;second.access_modes=0x123;
  const auto saved_first=first,saved_second=second;
  Reader fixed;
  assert(!fixed.Target(&first));assert(!fixed.ObserveOracle(&first,&field_b,false));
  assert(fixed.ObserveOracle(&first,&field_a,false));
  assert(fixed.ObserveOracle(&second,&field_b,true));assert(fixed.Seal());
  assert(fixed.Target(&first)==&field_a&&fixed.Target(&second)==&field_b);
  assert(!std::memcmp(&first,&saved_first,sizeof(first))&&!std::memcmp(&second,&saved_second,sizeof(second)));
  Hook hook;Declarations declarations;assert(declarations.Resolve(hook)&&declarations.visit);assert(hook.installs==0);
  const char* required[]={kAccess,kCurrent,artdcevm::abi::common::kSuspendConstruct,
      artdcevm::abi::common::kSuspendDestroy};
  for(const auto* missing:required){hook.missing=missing;assert(!declarations.Resolve(hook));assert(hook.installs==0);}
  hook.missing.clear();hook.no_decode=true;assert(!declarations.Resolve(hook));hook.no_decode=false;
  hook.missing=kVisit;assert(declarations.Resolve(hook)&&!declarations.visit);
  Owner owner{&named};original_owner=&owner;expected_handle=&a;
  int frame=0,operands=0;expected_frame=&frame;expected_operands=&operands;
  Dispatch dispatch{&owner,Target,Retired,Reject,Unavailable,Observed,Original};uint64_t result=99;
  assert(dispatch.Invoke(&a,19,&frame,&operands,&result));assert(result==0x1234&&originals==1&&owner.observations==1);
  owner.retired=&field_a;result=99;
  assert(!dispatch.Invoke(&a,19,&frame,&operands,&result));
  assert(result==0&&originals==1&&owner.exception==1&&owner.rejections==1);
  owner.retired=nullptr;original_result=false;owner.exception=0;
  assert(!dispatch.Invoke(&a,19,&frame,&operands,&result));
  assert(originals==2&&owner.exception==3&&result==0x1234);
  Mirror invalid{&field_a,true};owner.exception=0;
  assert(!dispatch.Invoke(&invalid,19,&frame,&operands,&result));assert(originals==2&&owner.exception==2);
  dispatch.original=nullptr;assert(!dispatch.Invoke(&a,19,&frame,&operands,&result));assert(originals==2);
  owner.reader=&fixed;Reader unproved;owner.reader=&unproved;dispatch.original=Original;owner.exception=0;
  assert(!dispatch.Invoke(&first,19,&frame,&operands,&result));assert(originals==2&&owner.exception==2);
  std::puts("VarHandle native target: actual typed dispatch, readonly oracle refusal, fixed declaration proof, missing symbols, keep/retire/exception forwarding passed");
}
