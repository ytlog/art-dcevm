// Includes the real sole listener owner and original installer fixture in this
// translation unit so calls use its actual source-declared virtual callbacks.
#include "listener_fixture.cc"
#include "platform/common/native_final_write_listener.cc"
#include "platform/common/native_receiver_source_calibration.h"
#include "platform/common/native_reference_decoder.h"
#include <array>
#include <cstdlib>
#include <new>
static bool reject_allocations=false;
void*operator new(size_t n){if(reject_allocations)std::abort();if(void*p=std::malloc(n))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}
void*operator new[](size_t n){return ::operator new(n);}
void operator delete[](void*p)noexcept{::operator delete(p);}
#include "platform/common/native_receiver_service.h"
#include "platform/common/native_receiver_bytecode.h"
namespace rg=artdcevm::receiver_guard;
struct RClass {RClass* parent;};struct RObject {RClass* klass;};struct RMember {RClass* owner;};
struct RState {void* thread;RObject* object;RMember* method;RClass* caller=nullptr;bool valid=true;const char* pending=nullptr;};
static bool RCurrent(void*p,void*t)noexcept{return static_cast<RState*>(p)->thread==t;}
static bool RMemberRead(void*,void*m,bool,uint32_t*f,void**o)noexcept{*f=0;*o=static_cast<RMember*>(m)->owner;return true;}
static bool RClassRead(void*,void*o,void**c)noexcept{*c=static_cast<RObject*>(o)->klass;return true;}
static bool RNormalize(void*,void*,void*c,void*,void**o)noexcept{*o=c;return true;}
static bool RAssignable(void*,void*d,void*c,bool*out)noexcept{*out=false;for(auto*k=static_cast<RClass*>(c);k;k=k->parent)if(k==d)*out=true;return true;}
static bool RSpecial(void*p,void*d,void*c,bool*out)noexcept{return RAssignable(p,d,c,out);}
static bool RCompressed(void*p,void*,uint32_t v,void**o)noexcept{if(v!=0x1234)return false;*o=static_cast<RState*>(p)->object;return true;}
static std::array<uint16_t,7> source_instructions{0,0x106e,0,0,0x106f,0,0};
static bool RSourceFrame(void*p,void*,void*caller,uint32_t,const void**frame)noexcept;
static bool RSourceCode(void*,void*,void*,rg::CodeView*out)noexcept{*out={source_instructions.data(),source_instructions.size(),2,2,2};return true;}
static bool RSourceResolve(void*p,void*,void*,const rg::InvokeInstruction&invoke,void**member,void**special)noexcept{auto&s=*static_cast<RState*>(p);*member=s.method;*special=invoke.kind==rg::InvokeKind::Super?s.caller:nullptr;return true;}
static bool RPending(void*p,void*)noexcept{return static_cast<RState*>(p)->pending!=nullptr;}
static void RRaise(void*p,void*,const char*t,const char*)noexcept{static_cast<RState*>(p)->pending=t;}
static std::array<unsigned char,80> native_frame{};static const void* native_operands=nullptr;
class ROperands:public rg::InstructionOperands {public:ROperands():InstructionOperands(1){}uint32_t GetOperand(size_t i)const override{assert(i==0);return 0;}};
static bool RSourceFrame(void*p,void*,void*caller,uint32_t,const void**frame)noexcept{auto&s=*static_cast<RState*>(p);if(!s.valid||caller!=s.method)return false;*frame=native_frame.data();return true;}
static void RPosition(uint32_t pc){std::memcpy(native_frame.data()+52,&pc,4);}
static bool RExtent(void*,void*,const void*p,size_t*n)noexcept{if(p==native_frame.data()){*n=native_frame.size();return true;}if(p==native_operands){*n=sizeof(ROperands);return true;}return false;}
int main(int argc,char**argv){assert(argc==2);assert(ExistingFinalMain(argc,argv)==0);final_flags=0;
 int thread=0;RClass root{nullptr},ancestor{&root},before{&ancestor},after{&root};RObject object{&before};RMember method{&ancestor},field{&ancestor};RState state{&thread,&object,&method,&before};
 ROperands operands;native_operands=&operands;rg::FrameReader frames({&state,RCurrent,RExtent,RCompressed});const uint32_t count=2,frame_reference=0x1234;void*source_method=&method;std::memcpy(native_frame.data()+8,&source_method,8);std::memcpy(native_frame.data()+48,&count,4);std::memcpy(native_frame.data()+72,&frame_reference,4);assert(frames.Observe(native_frame.data(),&operands,{&thread,&method,reinterpret_cast<void*>(0x1234),0})&&frames.Seal());
 rg::BytecodeSource bytecode(frames,{&state,RCurrent,RSourceFrame,RSourceCode,RSourceResolve,RPending});
 for(unsigned kind=0;kind<5;++kind)for(bool range:{false,true}){RPosition(1);const unsigned count=kind==3?0:1;source_instructions[1]=uint16_t((range?0x74:0x6e)+kind)|uint16_t(count<<(range?8:12));rg::InvokeInstruction invoke{static_cast<rg::InvokeKind>(kind),0,0,count,range};assert(bytecode.Observe({&thread,&method,1,invoke,&method,kind==3?nullptr:&object,kind==1?&before:nullptr}));}assert(bytecode.Seal());source_instructions[1]=0x106e;
 rg::Access access{&state,RCurrent,RMemberRead,RClassRead,RNormalize,RAssignable,RSpecial,nullptr,RCompressed,RRaise};access.bytecode=&bytecode;access.frames=&frames;rg::Guard guard(access);
 assert(rg::Bind(guard));rg::PermanentDispatch lease(guard,frames);std::string error;assert(!lease.Prepare(&error)&&!lease.Validate());int foreign_owner=0;assert(!BindReceiverEvents(&foreign_owner,guard,&error));assert(BindReceiverEvents(receiver,guard,&error));assert(!ReceiverListenerProven(guard)&&!ValidateReceiverProof(FreezeReceiverProof(guard)));
 artdcevm::native_reference_decoder::Decoder missing_roots;rg::SourceCalibration source_calibration(frames,bytecode,missing_roots);assert(!source_calibration.Proven()&&!source_calibration.Seal());
 assert(!BindSourceEvents(&foreign_owner,source_calibration.Observer(),&error));assert(BindSourceEvents(receiver,source_calibration.Observer(),&error));assert(InstallSourceEventsAtExclusiveBoundary(&error)&&adds==2);assert(InstallSourceEventsAtExclusiveBoundary(&error)&&adds==2);
 assert(InstallReceiverEventsAtExclusiveBoundary(&error));assert(adds==3&&receiver_events==1);assert(InstallReceiverEventsAtExclusiveBoundary(&error)&&adds==3);assert(!ReceiverListenerProven(guard));
 auto* actual=static_cast<FieldOnlyListener*>(actual_listener);uint32_t reference=0x1234;Handle handle{&reference};unsigned bodies=0;uint64_t sentinel=71;
 for(unsigned i=0;i<static_cast<unsigned>(rg::Path::Count);++i){auto path=static_cast<rg::Path>(i);bool f=path==rg::Path::BytecodeRead||path==rg::Path::BytecodeWrite||path==rg::Path::JniRead||path==rg::Path::JniWrite||path==rg::Path::ReflectRead||path==rg::Path::ReflectWrite||path==rg::Path::HandleRead||path==rg::Path::HandleWrite||path==rg::Path::VarHandle;auto*m=f?&field:&method;
  for(bool rejection:{false,true}){object.klass=rejection?&after:&before;state.pending=nullptr;sentinel=71;const unsigned previous=bodies;rg::Invocation invocation{path,&thread,m,&object,(path==rg::Path::HandleSpecial||path==rg::Path::BytecodeSpecial)?&before:nullptr,nullptr,f};assert(guard.BeginProof({invocation,&ancestor,object.klass,rejection}));
   if(path==rg::Path::BytecodeInvoke){RPosition(1);actual->DexPcMoved(&thread,handle,&method,1);}
   else if(path==rg::Path::BytecodeSpecial){RPosition(4);actual->DexPcMoved(&thread,handle,&method,4);}
   else if(path==rg::Path::BytecodeRead)actual->FieldRead(&thread,handle,&method,1,&field);
   else if(path==rg::Path::BytecodeWrite){const uint64_t value=99;actual->FieldWritten(&thread,handle,&method,1,&field,value);}
   else guard.Check(invocation); // Other entry owners have separate proxy tests.
   // Fixed interpreter original checks pending exception before invoking body,
   // loading the field result or writing the field; preserve that same order.
   if(!state.pending){++bodies;sentinel=99;}assert(bodies==previous+!rejection);assert(sentinel==(rejection?71u:99u));if(rejection)assert(!std::strcmp(state.pending,"Ljava/lang/IncompatibleClassChangeError;"));assert(guard.EndProof(!rejection,rejection,sentinel==71));
  }
 }
 assert(guard.Proven()&&ReceiverListenerProven(guard));rg::PermanentDispatch stale_lease(guard,frames);assert(stale_lease.Prepare(&error)&&stale_lease.Validate());auto witness=FreezeReceiverProof(guard);assert(ValidateReceiverProof(witness));object.klass=&before;state.pending=nullptr;rg::Invocation probe{rg::Path::BytecodeInvoke,&thread,&method,&object};assert(guard.BeginProof({probe,&ancestor,&before,false}));assert(!ValidateReceiverProof(witness)&&!stale_lease.Validate());RPosition(1);actual->DexPcMoved(&thread,handle,&method,1);assert(guard.EndProof(true,false,true));auto current=FreezeReceiverProof(guard);assert(!ValidateReceiverProof(witness)&&ValidateReceiverProof(current));
 listener.guard->ResetUncommittedProof();assert(!ValidateReceiverProof(current)&&!ValidateReceiverProof(FreezeReceiverProof(guard)));final_flags=0x10;assert(listener.guard->BeginProof({&thread,&method,&field}));const int previous_throws=throws;const uint64_t value=99;actual->FieldWritten(&thread,handle,&method,1,&field,value);assert(throws==previous_throws+1&&listener.guard->EndProof(true,true));final_flags=0;auto complete=FreezeReceiverProof(guard);assert(!ValidateReceiverProof(current)&&ValidateReceiverProof(complete));assert(deopts==1);rg::FrameReader foreign_frames({&state,RCurrent,RExtent,RCompressed});rg::PermanentDispatch foreign_lease(guard,foreign_frames);assert(!foreign_lease.Prepare(&error)&&!foreign_lease.Validate());assert(lease.Prepare(&error));rg::consumer.store(nullptr,std::memory_order_release);assert(!lease.Validate());rg::consumer.store(&guard,std::memory_order_release);reject_allocations=true;assert(lease.Validate());lease.BeginCommit();reject_allocations=false;assert(!listener.guard->BeginProof({&thread,&method,&field})&&deopts==2&&guard.Active()&&ValidateReceiverProof(complete)&&!guard.BeginProof({probe,&ancestor,&before,false}));state.pending=nullptr;RPosition(0);actual->DexPcMoved(&thread,handle,&method,0);assert(!state.pending);state.valid=false;actual->DexPcMoved(&thread,handle,&method,1);assert(!std::strcmp(state.pending,"Ljava/lang/IllegalStateException;"));
 rg::SourceCalibration other_source(frames,bytecode,missing_roots);assert(!BindSourceEvents(receiver,other_source.Observer(),&error));assert(!source_calibration.Proven());rg::Guard other(access);assert(!BindReceiverEvents(receiver,other,&error)&&!ReceiverListenerProven(other));assert(deopts==2);
 if(argv[1][0]=='6'){disable_observer(receiver,"owner-lease");assert(!ValidateReceiverProof(complete)&&!lease.Validate());}
 std::cout<<"Sole listener actual virtual DexPcMoved/FieldRead/FieldWritten: exact extra mask, actual caller-PC source identity, same old receiver migration, ICCE before body/read/write, foreign/missing resolver fail closed PASS; host leaves explicit, active AOT recovery not claimed\n";
}
