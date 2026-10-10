#include "platform/common/native_receiver_guard.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <array>
using namespace artdcevm::receiver_guard;
// Native leaves are explicit. Relations are read from actual changing fixture
// Class identities; no Enabled/deny bit substitutes for receiver assignability.
struct Class {Class* parent=nullptr;std::array<Class*,4> interfaces{};};
struct Object {Class* klass;};struct Member {uint32_t flags;Class* declaring;};
struct State {void* self=nullptr;Class* old_class=nullptr;Class* current_class=nullptr;bool context=true;const char* exception=nullptr;unsigned raised=0;};
static bool Current(void*p,void*t)noexcept{auto&s=*static_cast<State*>(p);return s.context&&s.self==t;}
static bool ReadMember(void*,void*m,bool,uint32_t*f,void**c)noexcept{auto& member=*static_cast<Member*>(m);*f=member.flags;*c=member.declaring;return true;}
static bool ObjectClass(void*,void*o,void**c)noexcept{*c=static_cast<Object*>(o)->klass;return true;}
static bool Normalize(void*p,void*,void*c,void*,void**n)noexcept{auto&s=*static_cast<State*>(p);*n=c==s.old_class?s.current_class:c;return *n;}
static bool Related(Class*expected,Class*actual)noexcept{for(auto*c=actual;c;c=c->parent){if(c==expected)return true;for(auto*i:c->interfaces)if(i&&(i==expected||Related(expected,i)))return true;}return false;}
static bool Assignable(void*,void*d,void*r,bool*out)noexcept{*out=Related(static_cast<Class*>(d),static_cast<Class*>(r));return true;}
static bool Special(void*,void*d,void*c,bool*out)noexcept{auto*caller=static_cast<Class*>(c);*out=caller->parent&&Related(static_cast<Class*>(d),caller->parent);for(auto*i:caller->interfaces)if(i&&Related(static_cast<Class*>(d),i))*out=true;return true;}
static void Raise(void*p,void*,const char*type,const char*)noexcept{auto&s=*static_cast<State*>(p);s.exception=type;++s.raised;}
int main(){
 int thread=0;Class object,interface,ancestor{&object},before{&ancestor,{&interface}},after{&object},unrelated{&object},special_before{&ancestor},special_after{&object};
 Object receiver{&before};Member method{1,&ancestor},field{1,&ancestor};State state{&thread,&before,&before};Guard guard({&state,Current,ReadMember,ObjectClass,Normalize,Assignable,Special,nullptr,nullptr,Raise});assert(!guard.Proven()&&!guard.ValidateProof(guard.FreezeProof()));assert(Bind(guard));
 uint64_t result=123;for(unsigned p=0;p<static_cast<unsigned>(Path::Count);++p){
  const auto path=static_cast<Path>(p);const bool is_field=path==Path::BytecodeRead||path==Path::BytecodeWrite||path==Path::JniRead||path==Path::JniWrite||path==Path::ReflectRead||path==Path::ReflectWrite||path==Path::HandleRead||path==Path::HandleWrite||path==Path::VarHandle;
  auto* member=is_field?&field:&method;Invocation call{path,&thread,member,&receiver,&special_before,&result,is_field};
  // Successful original dispatch must be witnessed; a missing event/path or
  // wrong receiver identity cannot count as successful source coverage.
  assert(guard.BeginProof({call,&ancestor,&before,false}));auto foreign=call;foreign.receiver=nullptr;assert(!guard.Check(foreign));assert(!guard.EndProof(true,false,true));
  assert(guard.BeginProof({call,&ancestor,&before,false}));assert(!Check(call));assert(guard.EndProof(true,false,true));
  // Keep the same old object; migrate its Class identity and lose its former
  // superclass+interface. Every member entry must now reject before the caller
  // body/sentinel can run, including its cached native member identity.
  receiver.klass=&after;state.exception=nullptr;result=123;
  assert(guard.BeginProof({call,&ancestor,&after,true}));assert(Check(call));assert(result==0&&!std::strcmp(state.exception,"Ljava/lang/IncompatibleClassChangeError;"));assert(!guard.EndProof(false,true,false));
  assert(guard.BeginProof({call,&ancestor,&after,true}));assert(Check(call));assert(guard.EndProof(false,true,true));receiver.klass=&before;
 }
 assert(guard.Proven());auto witness=guard.FreezeProof();assert(guard.ValidateProof(witness));Guard foreign_guard({&state,Current,ReadMember,ObjectClass,Normalize,Assignable,Special,nullptr,nullptr,Raise});assert(!foreign_guard.ValidateProof(witness));
 Invocation reprobe{Path::JniMethod,&thread,&method,&receiver,nullptr,&result,false};assert(guard.BeginProof({reprobe,&ancestor,&before,false}));assert(!guard.ValidateProof(witness)&&!guard.Proven());assert(!guard.EndProof(true,false,true));assert(!guard.Proven()&&!guard.ValidateProof(guard.FreezeProof()));assert(guard.BeginProof({reprobe,&ancestor,&before,false}));assert(!guard.Check(reprobe)&&guard.EndProof(true,false,true));auto current_witness=guard.FreezeProof();assert(!guard.ValidateProof(witness)&&guard.ValidateProof(current_witness));guard.Activate(current_witness);assert(!guard.BeginProof({reprobe,&ancestor,&before,false})&&guard.ValidateProof(current_witness));receiver.klass=&after;
 Invocation call{Path::JniMethod,&thread,&method,&receiver,nullptr,&result,false};assert(guard.Check(call));
 // Current weak aliases must allow cached declarations in the replaced Class
 // itself, while continuing to reject genuinely removed ancestor members.
 method.declaring=&before;state.current_class=&after;assert(!guard.Check(call));method.declaring=&ancestor;assert(guard.Check(call));
 receiver.klass=&before;call.path=Path::HandleSpecial;call.special_caller=&special_after;assert(guard.Check(call));call.special_caller=&special_before;assert(!guard.Check(call));
 call.receiver=nullptr;assert(!guard.Check(call));call.receiver=&receiver;method.flags|=8;assert(!guard.Check(call));method.flags&=~8u;
 state.context=false;assert(guard.Check(call));assert(!std::strcmp(state.exception,"Ljava/lang/IllegalStateException;"));state.context=true;
 // A relocating identity is reread from the current object/member and alias
 // leaves each time; no old Class pointer is remembered by Guard.
 Class moved_after=after;receiver.klass=&moved_after;state.current_class=&moved_after;method.declaring=&before;call.path=Path::JniMethod;assert(!guard.Check(call));
 (void)unrelated;std::puts("Permanent receiver policy: all 16 paths require observed pass/reject, migrated old object loses superclass/interface, current aliases preserve own cached members, special caller/null/static/context/relocation PASS; native leaves explicit, live source entry proof pending");
}
