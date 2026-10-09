// Includes the real sole listener owner and original installer fixture in this
// translation unit so calls use its actual source-declared virtual callbacks.
#include "listener_fixture.cc"
#include "platform/common/native_final_write_listener.cc"
#include <array>
namespace rg=artdcevm::receiver_guard;
struct RClass {RClass* parent;};struct RObject {RClass* klass;};struct RMember {RClass* owner;};
struct RState {void* thread;RObject* object;RMember* method;bool valid=true;const char* pending=nullptr;};
static bool RCurrent(void*p,void*t)noexcept{return static_cast<RState*>(p)->thread==t;}
static bool RMemberRead(void*,void*m,bool,uint32_t*f,void**o)noexcept{*f=0;*o=static_cast<RMember*>(m)->owner;return true;}
static bool RClassRead(void*,void*o,void**c)noexcept{*c=static_cast<RObject*>(o)->klass;return true;}
static bool RNormalize(void*,void*,void*c,void*,void**o)noexcept{*o=c;return true;}
static bool RAssignable(void*,void*d,void*c,bool*out)noexcept{*out=false;for(auto*k=static_cast<RClass*>(c);k;k=k->parent)if(k==d)*out=true;return true;}
static bool RSpecial(void*p,void*d,void*c,bool*out)noexcept{return RAssignable(p,d,c,out);}
static bool RCompressed(void*p,void*,uint32_t v,void**o)noexcept{if(v!=0x1234)return false;*o=static_cast<RState*>(p)->object;return true;}
static bool RBytecode(void*p,void*,void*caller,uint32_t pc,void**m,uint32_t*r)noexcept{auto&s=*static_cast<RState*>(p);if(!s.valid||caller!=s.method||pc>1)return false;*m=pc==1?s.method:nullptr;*r=0x1234;return true;}
static void RRaise(void*p,void*,const char*t,const char*)noexcept{static_cast<RState*>(p)->pending=t;}
int main(int argc,char**argv){assert(argc==2);assert(argv[1][0]=='5');assert(ExistingFinalMain(argc,argv)==0);final_flags=0;
 int thread=0;RClass root{nullptr},ancestor{&root},before{&ancestor},after{&root};RObject object{&before};RMember method{&ancestor},field{&ancestor};RState state{&thread,&object,&method};
 rg::Access access{&state,RCurrent,RMemberRead,RClassRead,RNormalize,RAssignable,RSpecial,nullptr,RCompressed,RRaise};access.bytecode=RBytecode;rg::Guard guard(access);
 std::string error;int foreign_owner=0;assert(!BindReceiverEvents(&foreign_owner,guard,&error));assert(BindReceiverEvents(receiver,guard,&error));assert(!ReceiverListenerProven(guard));assert(InstallReceiverEventsAtExclusiveBoundary(&error));assert(adds==2&&receiver_events==1);assert(InstallReceiverEventsAtExclusiveBoundary(&error)&&adds==2);assert(!ReceiverListenerProven(guard));
 auto* actual=static_cast<FieldOnlyListener*>(actual_listener);uint32_t reference=0x1234;Handle handle{&reference};unsigned bodies=0;uint64_t sentinel=71;
 for(unsigned i=0;i<static_cast<unsigned>(rg::Path::Count);++i){auto path=static_cast<rg::Path>(i);bool f=path==rg::Path::BytecodeRead||path==rg::Path::BytecodeWrite||path==rg::Path::JniRead||path==rg::Path::JniWrite||path==rg::Path::ReflectRead||path==rg::Path::ReflectWrite||path==rg::Path::HandleRead||path==rg::Path::HandleWrite||path==rg::Path::VarHandle;auto*m=f?&field:&method;
  for(bool rejection:{false,true}){object.klass=rejection?&after:&before;state.pending=nullptr;sentinel=71;const unsigned previous=bodies;rg::Invocation invocation{path,&thread,m,&object,path==rg::Path::HandleSpecial?&before:nullptr,nullptr,f};assert(guard.BeginProof({invocation,&ancestor,object.klass,rejection}));
   if(path==rg::Path::BytecodeInvoke)actual->DexPcMoved(&thread,handle,&method,1);
   else if(path==rg::Path::BytecodeRead)actual->FieldRead(&thread,handle,&method,1,&field);
   else if(path==rg::Path::BytecodeWrite){const uint64_t value=99;actual->FieldWritten(&thread,handle,&method,1,&field,value);}
   else guard.Check(invocation); // Other entry owners have separate proxy tests.
   // Fixed interpreter original checks pending exception before invoking body,
   // loading the field result or writing the field; preserve that same order.
   if(!state.pending){++bodies;sentinel=99;}assert(bodies==previous+!rejection);assert(sentinel==(rejection?71u:99u));if(rejection)assert(!std::strcmp(state.pending,"Ljava/lang/IncompatibleClassChangeError;"));assert(guard.EndProof(!rejection,rejection,sentinel==71));
  }
 }
 assert(guard.Proven()&&ReceiverListenerProven(guard));guard.Activate();state.pending=nullptr;actual->DexPcMoved(&thread,handle,&method,0);assert(!state.pending);state.valid=false;actual->DexPcMoved(&thread,handle,&method,1);assert(!std::strcmp(state.pending,"Ljava/lang/IllegalStateException;"));
 rg::Guard other(access);assert(!BindReceiverEvents(receiver,other,&error)&&!ReceiverListenerProven(other));assert(deopts==1);RetainInterpretationAtCommit();assert(deopts==2);
 std::cout<<"Sole listener actual virtual DexPcMoved/FieldRead/FieldWritten: exact extra mask, actual caller-PC source identity, same old receiver migration, ICCE before body/read/write, foreign/missing resolver fail closed PASS; host leaves explicit, active AOT recovery not claimed\n";
}
