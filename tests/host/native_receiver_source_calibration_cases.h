#include "platform/common/native_receiver_source_calibration.h"
#include <array>
namespace rg=artdcevm::receiver_guard;
struct SourceCase {
 std::array<unsigned char,80> frame{};std::array<uint16_t,3> code{0x106e,0,0};
 jobject root;void*self,*method,*target;bool move=false,wrong_target=false;
};
static bool SCurrent(void*p,void*t)noexcept{return static_cast<SourceCase*>(p)->self==t;}
static bool SFrame(void*p,void*t,void*m,uint32_t,const void**out)noexcept{auto&s=*static_cast<SourceCase*>(p);if(t!=s.self||m!=s.method)return false;*out=s.frame.data();return true;}
static bool SCode(void*p,void*,void*,rg::CodeView*out)noexcept{auto&s=*static_cast<SourceCase*>(p);*out={s.code.data(),s.code.size(),2,2,0};return true;}
static bool SExtent(void*p,void*,const void*address,size_t*out)noexcept{auto&s=*static_cast<SourceCase*>(p);*out=address==s.frame.data()?s.frame.size():sizeof(rg::InstructionOperands);return true;}
static bool SReference(void*,void*,uint32_t value,void**out)noexcept{*out=reinterpret_cast<void*>(uintptr_t(value));return true;}
static bool SResolve(void*p,void*,void*,const rg::InvokeInstruction&i,void**out,void**special)noexcept{auto&s=*static_cast<SourceCase*>(p);
 if(s.move){Reference(s.root).object=reinterpret_cast<Object*>(0x5678);uint32_t root=0x5678;std::memcpy(s.frame.data()+72,&root,4);s.move=false;}
 *out=s.wrong_target?s.method:s.target;*special=i.kind==rg::InvokeKind::Super?reinterpret_cast<void*>(0x8765):nullptr;return true;
}
static bool SPending(void*,void*)noexcept{return false;}
static void TestSourceCalibration(Decoder&decoder,JNIEnv*env,jobject a,jobject b){
 auto*before_a=Reference(a).object;auto*before_b=Reference(b).object;Reference(a).object=reinterpret_cast<Object*>(0x1234);Reference(b).object=reinterpret_cast<Object*>(0x8765);
 std::string error;assert(decoder.RegisterStrong(env,b,&error));int method=0,target=0;SourceCase state{{},{0x106e,0,0},a,&thread,&method,&target};void*m=&method;uint32_t count=2,ref=0x1234;
 std::memcpy(state.frame.data()+8,&m,8);std::memcpy(state.frame.data()+48,&count,4);std::memcpy(state.frame.data()+72,&ref,4);
 rg::FrameReader frames({&state,SCurrent,SExtent,SReference});rg::BytecodeSource source(frames,{&state,SCurrent,SFrame,SCode,SResolve,SPending});rg::SourceCalibration cal(frames,source,decoder);
 auto stage=[&](rg::InvokeKind kind,bool range,const unsigned char*code){return cal.Stage(&thread,&method,&target,kind==rg::InvokeKind::Static?nullptr:a,kind==rg::InvokeKind::Super?b:nullptr,kind,range,code,state.code.size()*2);};
 assert(!cal.Seal()&&!cal.Proven());state.wrong_target=true;assert(stage(rg::InvokeKind::Virtual,false,reinterpret_cast<unsigned char*>(state.code.data())));cal.Observer().DexPcMoved(&thread,&method,0);assert(!cal.Finish(true)&&!cal.Seal());state.wrong_target=false;
 auto public_code=state.code;public_code[1]=1;assert(stage(rg::InvokeKind::Virtual,false,reinterpret_cast<unsigned char*>(public_code.data())));cal.Observer().DexPcMoved(&thread,&method,0);assert(!cal.Finish(true));
 assert(stage(rg::InvokeKind::Virtual,false,reinterpret_cast<unsigned char*>(state.code.data())));cal.Observer().DexPcMoved(&wrong,&method,0);cal.Observer().DexPcMoved(&thread,&target,0);assert(!cal.Finish(true));
 assert(stage(rg::InvokeKind::Virtual,false,reinterpret_cast<unsigned char*>(state.code.data())));cal.Observer().DexPcMoved(&thread,&method,0);assert(!cal.Finish(false)&&!cal.Seal());
 for(unsigned kind=0;kind<5;++kind)for(bool range:{false,true}){state.code[0]=uint16_t((range?0x74:0x6e)+kind)|uint16_t(1<<(range?8:12));state.move=kind==0&&!range;
  assert(stage(static_cast<rg::InvokeKind>(kind),range,reinterpret_cast<unsigned char*>(state.code.data())));assert(!stage(static_cast<rg::InvokeKind>(kind),range,reinterpret_cast<unsigned char*>(state.code.data())));cal.Observer().DexPcMoved(&thread,&method,0);assert(cal.Finish(true));
 }
 assert(cal.Seal()&&cal.Proven()&&frames.Proven()&&source.Proven());assert(!cal.Seal()&&!stage(rg::InvokeKind::Virtual,false,reinterpret_cast<unsigned char*>(state.code.data())));
 rg::SourceCalibration unobserved(frames,source,decoder);assert(!unobserved.Proven()&&!unobserved.Seal());
 Reference(a).object=before_a;Reference(b).object=before_b;decoder.Forget(b);
 std::cout<<"Source observer protocol: ten actual callback forms, whole public bytes/member identity, fresh post-resolve roots, bad result/foreign caller/wrong target refusal and no borrowed presealed readiness PASS\n";
}
