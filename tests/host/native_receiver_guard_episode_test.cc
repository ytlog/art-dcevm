#include "platform/common/native_receiver_guard_episode.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <array>
using namespace artdcevm::receiver_guard;
// Native leaves are explicit. Relations are read from actual changing fixture
// Class identities; no Enabled/deny bit substitutes for receiver assignability.
struct Class {Class* parent=nullptr;std::array<Class*,4> interfaces{};};
struct Object {Class* klass;};struct Member {uint32_t flags;Class* declaring;};
struct State {void* self=nullptr;Class* old_class=nullptr;Class* current_class=nullptr;bool context=true;const char* exception=nullptr;unsigned raised=0;uint32_t handle_kind=3;};
static bool Current(void*p,void*t)noexcept{auto&s=*static_cast<State*>(p);return s.context&&s.self==t;}
static bool ReadMember(void*,void*m,bool,uint32_t*f,void**c)noexcept{auto& member=*static_cast<Member*>(m);*f=member.flags;*c=member.declaring;return true;}
static bool ObjectClass(void*,void*o,void**c)noexcept{*c=static_cast<Object*>(o)->klass;return true;}
static bool Normalize(void*p,void*,void*c,void*,void**n)noexcept{auto&s=*static_cast<State*>(p);*n=c==s.old_class?s.current_class:c;return *n;}
static bool Related(Class*expected,Class*actual)noexcept{for(auto*c=actual;c;c=c->parent){if(c==expected)return true;for(auto*i:c->interfaces)if(i&&(i==expected||Related(expected,i)))return true;}return false;}
static bool Assignable(void*,void*d,void*r,bool*out)noexcept{*out=Related(static_cast<Class*>(d),static_cast<Class*>(r));return true;}
static bool Special(void*,void*d,void*c,bool*out)noexcept{auto*caller=static_cast<Class*>(c);*out=caller->parent&&Related(static_cast<Class*>(d),caller->parent);for(auto*i:caller->interfaces)if(i&&Related(static_cast<Class*>(d),i))*out=true;return true;}
static void Raise(void*p,void*,const char*type,const char*)noexcept{auto&s=*static_cast<State*>(p);s.exception=type;++s.raised;}

using namespace artdcevm::receiver_guard::episode;
struct Episodes final:Source {
 Class object,declaring{&object},before{&declaring},after{&object},special{&declaring};
 Object receiver{&before};Member method{1,&declaring},field{1,&declaring};int self=0;
 State state{&self,&before,&before};Guard* guard=nullptr;Snapshot snapshot{0,71,0x10203040};
 Prepared current{};bool absent=false,missing_event=false,tamper=false,wrong_identity=false,wrong_return=false,run_fail=false,observe_fail=false;
 bool Prepare(Path path,bool rejection,Prepared*out)noexcept override {
  if(absent&&rejection)return false;receiver.klass=rejection?&after:&before;
  const bool f=path==Path::BytecodeRead||path==Path::BytecodeWrite||path==Path::JniRead||path==Path::JniWrite||path==Path::ReflectRead||path==Path::ReflectWrite||path==Path::HandleRead||path==Path::HandleWrite||path==Path::VarHandle;
  current={{{path,&self,f?static_cast<void*>(&field):&method,&receiver,&special,nullptr,f},&declaring,receiver.klass,rejection},snapshot,89,f?snapshot.field_value:113};
  if(wrong_identity)current.oracle.invocation.path=Path::Count;
  *out=current;return true;
 }
 bool Run(Path,Completion*out)noexcept override {
  // Unit leaf-boundary simulation of dispatch into the actual Guard core.
  // This does not exercise production JNI/listener hooks or the real ART body.
  state.exception=nullptr;const bool rejected=missing_event?false:guard->Check(current.oracle.invocation);
  out->succeeded=!rejected;out->incompatible_class_change=state.exception&&!std::strcmp(state.exception,"Ljava/lang/IncompatibleClassChangeError;");
  if(!rejected){const auto p=current.oracle.invocation.path;
   if(p==Path::BytecodeRead||p==Path::JniRead||p==Path::ReflectRead||p==Path::HandleRead||p==Path::VarHandle)out->value=snapshot.field_value;
   else if(p==Path::BytecodeWrite||p==Path::JniWrite||p==Path::ReflectWrite||p==Path::HandleWrite)snapshot.field_value=current.write_value;
   else {++snapshot.body_calls;out->value=wrong_return?112:113;}
  }else if(tamper)++snapshot.sentinel;
  return !run_fail;
 }
 bool Observe(Snapshot*out)noexcept override{*out=snapshot;return !observe_fail;}
 Access Leaves(){return {&state,Current,ReadMember,ObjectClass,Normalize,Assignable,Special,nullptr,nullptr,Raise};}
};
int main(){
 {Episodes source;Guard guard(source.Leaves());source.guard=&guard;auto result=Calibrate(guard,source);assert(result.failure==Failure::None&&result.completed==32&&guard.Proven());assert(guard.ValidateProof(guard.FreezeProof()));assert(source.snapshot.body_calls==7);}
 {Episodes source;Guard guard(source.Leaves());source.guard=&guard;source.absent=true;auto result=Calibrate(guard,source);assert(result.failure==Failure::Prepare&&result.completed==1&&!guard.Proven());}
 {Episodes source;Guard guard(source.Leaves());source.guard=&guard;source.missing_event=true;auto result=Calibrate(guard,source);assert(result.failure==Failure::End&&result.completed==0&&!guard.Proven());}
 {Episodes source;Guard guard(source.Leaves());source.guard=&guard;source.tamper=true;auto result=Calibrate(guard,source);assert(result.failure==Failure::Oracle&&result.completed==1&&!guard.Proven());}
 {Episodes source;Guard guard(source.Leaves());source.guard=&guard;source.wrong_identity=true;auto result=Calibrate(guard,source);assert(result.failure==Failure::Identity&&!guard.InUse()&&!guard.Proven());}
 {Episodes source;Guard guard(source.Leaves());source.guard=&guard;source.wrong_return=true;auto result=Calibrate(guard,source);assert(result.failure==Failure::Oracle&&!result.completed&&!guard.InUse()&&!guard.Proven());}
 {Episodes source;Guard guard(source.Leaves());source.guard=&guard;source.run_fail=true;auto result=Calibrate(guard,source);assert(result.failure==Failure::Original&&!result.completed&&!guard.InUse()&&!guard.Proven());}
 {Episodes source;Guard guard(source.Leaves());source.guard=&guard;source.observe_fail=true;auto result=Calibrate(guard,source);assert(result.failure==Failure::Readback&&!result.completed&&!guard.InUse()&&!guard.Proven());}
 std::puts("Guard core+driver unit leaf dispatch: 32 episodes with Class topology, method return/body/read/write/sentinel witnesses; missing migration/event, wrong identity/return, changed sentinel, Run/Observe failures refuse and close probes; real production JNI/listener/source episodes pending");
}
