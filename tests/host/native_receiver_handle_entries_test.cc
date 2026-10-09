#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "native_receiver_handle_fixture.inc"
#pragma clang diagnostic pop
#include "platform/common/native_receiver_frame.h"
#include <array>
using namespace receiver_guard;
namespace {
struct Class{Class*parent=nullptr;};Class base,before{&base},after,caller{&base};Class*receiver_class=&before;void*receiver=reinterpret_cast<void*>(0x1000);
std::array<unsigned char,96> frame{};
struct Operands:InstructionOperands{Operands():InstructionOperands(1){}uint32_t GetOperand(size_t i)const override{assert(!i);return 0;}} operands;
static bool NativeCurrent(void*,void*self)noexcept{return self==current;}
static bool Extent(void*,void*,const void*p,size_t*out)noexcept{if(p==frame.data())*out=frame.size();else if(p==&operands)*out=sizeof(operands);else return false;return true;}
static bool Reference(void*,void*,uint32_t value,void**out)noexcept{*out=reinterpret_cast<void*>(uintptr_t(value));return *out==receiver;}
static bool ReadMember(void*,void*m,bool field,uint32_t*flags,void**owner)noexcept{if(m!=Identity(Object(field?4:1)))return false;*flags=1;*owner=&base;return true;}
static bool ObjectClass(void*,void*r,void**klass)noexcept{if(r!=receiver)return false;*klass=receiver_class;return true;}
static bool Normalize(void*,void*,void*d,void*,void**owner)noexcept{*owner=d;return true;}
static bool Assignable(void*,void*d,void*r,bool*out)noexcept{*out=false;for(auto*k=static_cast<Class*>(r);k;k=k->parent)if(k==d)*out=true;return true;}
static bool Special(void*,void*d,void*c,bool*out)noexcept{return Assignable(nullptr,d,static_cast<Class*>(c)->parent,out);}
static void Raise(void*,void*,const char*type,const char*)noexcept{exception_kind=!std::strcmp(type,"Ljava/lang/IncompatibleClassChangeError;")?8:9;}
static bool SourceKind(void*,void*,void*object,uint32_t*kind,void**source)noexcept{if(object!=Object(IsField(object)?4:1))return false;*kind=override_kind;*source=&caller;return true;}
static bool Original(void*self,void*f,retirement::Handle,retirement::Handle,const void*args,void*result){assert(self==current&&f==frame.data()&&args==&operands);++invoked;*static_cast<uint64_t*>(result)=99;return true;}
}
int main(){using namespace retirement;FrameReader frames({nullptr,NativeCurrent,Extent,Reference});uint32_t count=2,reference=0x1000;void*caller_method=Identity(Object(1));std::memcpy(frame.data()+8,&caller_method,8);std::memcpy(frame.data()+48,&count,4);std::memcpy(frame.data()+64+count*4,&reference,4);assert(frames.Observe(frame.data(),&operands,{current,caller_method,receiver,0})&&frames.Seal());
 receiver_guard::Guard guard({nullptr,NativeCurrent,ReadMember,ObjectClass,Normalize,Assignable,Special,nullptr,nullptr,Raise,nullptr,&frames,SourceKind});assert(Bind(guard));state.installed=true;state.binding.root=ReadRoot;state.binding.visit=::Visit;state.binding.current=Current;state.originals[0]=state.originals[1]=Original;
 for(uint32_t kind:{0u,1u,2u,4u,8u,9u})for(bool refuse:{false,true}){
  const bool field=kind>=8;override_kind=kind;receiver_class=refuse?&after:&before;caller.parent=&base;exception_kind=0;uint32_t root=uint32_t(uintptr_t(Object(field?4:1)));uint64_t result=123;const auto bodies=invoked;const Path path=field?(kind==8?Path::HandleRead:Path::HandleWrite):(kind==1?Path::HandleSpecial:Path::HandleMethod);Invocation invocation{path,current,Identity(Object(field?4:1)),receiver,kind==1?&caller:nullptr,&result,field};assert(guard.BeginProof({invocation,&base,receiver_class,refuse}));const bool succeeded=Dispatch<false>(current,frame.data(),{&root},{nullptr},&operands,&result);assert(succeeded==!refuse);assert(guard.EndProof(succeeded,refuse,true));assert(invoked==bodies+(refuse?0:1));assert(result==(refuse?0:99));if(refuse)assert(exception_kind==8);
 }
 // Preserve receiver relation while invalidating only the cached special
 // caller's former superclass. It must reject independently of ordinary calls.
 override_kind=1;receiver_class=&before;caller.parent=nullptr;uint32_t root=uint32_t(uintptr_t(Object(1)));uint64_t result=123;const auto bodies=invoked;
 Invocation special{Path::HandleSpecial,current,Identity(Object(1)),receiver,&caller,&result,false};assert(guard.BeginProof({special,&base,&before,true}));assert(!Dispatch<true>(current,frame.data(),{&root},{nullptr},&operands,&result));assert(guard.EndProof(false,true,true)&&exception_kind==8&&result==0&&invoked==bodies);caller.parent=&base;
 // A bogus private kind / member role does not turn into ordinary delegation
 // or successful source proof, even with a readable target and receiver.
 override_kind=8;Invocation invalid{Path::HandleMethod,current,Identity(Object(1)),receiver,nullptr,&result,false};assert(guard.BeginProof({invalid,&base,&before,false}));assert(!Dispatch<false>(current,frame.data(),{&root},{nullptr},&operands,&result));assert(!guard.EndProof(false,false,true)&&exception_kind==9&&invoked==bodies);
 assert(!guard.Proven());std::puts("Sole exported MethodHandle owner: virtual/direct/interface/special/field get-put, native source operand0 and same migrated receiver, both original success and ICCE-before-body PASS; incomplete paths never ready");
}
