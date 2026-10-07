#include "device_profile_fixture.h"
#include "platform/android13/compiled_frame_diagnostics.h"
#include "platform/android13/compiled_frame_diagnostics_abi.h"
#include "platform/android13/member_decode_abi.h"
#include "platform/android13/art_method_abi.h"
#include "platform/android13/runtime_access_abi.h"
#include "platform/android13/native_object_access.h"
#include "platform/android13/thread_suspend_scope.h"
#include "platform/android13/stack_access.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <dlfcn.h>
#include <vector>
using namespace artdcevm::android13;
using Reason=compiled_frame_abi::SuspendReason;using Method=art_method_abi::ArtMethod;using Executable=member_decode_abi::Executable;
alignas(8) unsigned char self[384]{},target[384]{};runtime_access_abi::RuntimePrefix runtime{};int list,cache;
bool target_suspended=false,missing_target=false,timed_out=false,resume_ok=true,has_cache=true,cached=true,probe=false,mark_ok=true,supported=true;
int suspends=0,resumes=0,marks=0,resets=0,starts=0,ends=0;void* pending=nullptr;const void* code=nullptr;uint64_t hits=42;
struct Frame {void* method;const void* header;uintptr_t pc;bool quick=true,inlined=false;};std::vector<Frame> frames;
void Native(void* t){assert(t==self&&*reinterpret_cast<uint32_t*>(t)==0);*reinterpret_cast<uint32_t*>(t)=92u<<24;++starts;}
void Runnable(void* t){assert(t==self&&(*reinterpret_cast<uint32_t*>(t)>>24)==92);*reinterpret_cast<uint32_t*>(t)=0;++ends;}
void SuspendAll(void*,const char*,bool){assert((*reinterpret_cast<uint32_t*>(self)>>24)==92);}
void ResumeAll(void*){}
void* SuspendPeer(void* l,jobject p,Reason why,bool* timeout){assert(l==&list&&p==reinterpret_cast<jobject>(target)&&why==Reason::kInternal&&(*reinterpret_cast<uint32_t*>(self)>>24)==92&&!target_suspended);++suspends;*timeout=timed_out;if(missing_target)return nullptr;target_suspended=true;return target;}
bool Resume(void* l,void* t,Reason why){assert(l==&list&&t==target&&why==Reason::kInternal&&target_suspended&&(*reinterpret_cast<uint32_t*>(self)>>24)==92);target_suspended=false;++resumes;return resume_ok;}
bool Contains(const void* h,uintptr_t pc){uint32_t size;std::memcpy(&size,h,4);uintptr_t begin=reinterpret_cast<uintptr_t>(h)+4;return begin<=pc&&pc<=begin+size;}
bool Cached(const void* c,const void*){assert(c==&cache);return cached;}
const void* Code(void*,size_t width){assert(width==8&&target_suspended&&*reinterpret_cast<uint32_t*>(self)==0);return code;}
void Construct(void* raw,void* t,void*,stack_abi::WalkKind k,bool check){assert(check);auto* v=static_cast<stack_abi::Visitor*>(raw);std::memset(static_cast<unsigned char*>(raw)+8,0,sizeof(*v)-8);v->thread=t;v->kind=k;void* foreign=reinterpret_cast<void*>(0x5678);std::memcpy(raw,&foreign,8);}
void Walk(void* raw,bool transitions){assert(!transitions);auto* v=static_cast<stack_abi::Visitor*>(raw);if(v->thread==self)return;assert(v->thread==target&&target_suspended&&*reinterpret_cast<uint32_t*>(self)==0);for(size_t i=0;i<frames.size();++i){auto& f=frames[i];if(f.inlined&&v->kind==stack_abi::WalkKind::kSkipInlined)continue;v->depth=i;v->header=f.header;v->pc=f.pc;v->quick_frame=f.quick?&f.method:nullptr;if(!v->VisitFrame())break;}}
void* GetMethod(const void* v){return frames.at(static_cast<const stack_abi::Visitor*>(v)->depth).method;}
namespace artdcevm::android13 {
void* CurrentHierarchyThread(){return self;}void* RuntimeInstance(){return &runtime;}void* RuntimeJitCodeCache(){return has_cache?&cache:nullptr;}
void SetAsyncDeoptProbe(bool b){probe=b;}bool MarkEvolutionSuspendDeopt(void* t,uint64_t*){assert(t==target&&target_suspended);++marks;if(!mark_ok)return false;pending=t;return true;}
void ResetEvolutionSuspendDeopt(){++resets;pending=nullptr;}uint64_t EvolutionSuspendDeoptHits(){return hits;}
NativeObjectAccess::NativeObjectAccess(JNIEnv*):self_(self),supported_(supported),transitioned_((*reinterpret_cast<uint32_t*>(self)>>24)==92){if(transitioned_)Runnable(self);}
NativeObjectAccess::~NativeObjectAccess(){if(transitioned_)Native(self);}void* NativeObjectAccess::Decode(jobject object)const{assert(*reinterpret_cast<uint32_t*>(self)==0);return object;}
}
class Hooks:public artdcevm::HookEngine{public:bool missing=false;bool Initialize(std::string*)override{return true;}void* FindSymbol(const char*,const char* n)override{if(missing)return nullptr;
#define FN(part,f) if(std::strstr(n,part))return reinterpret_cast<void*>(f)
FN("StackVisitorC2",Construct);FN("WalkStack",Walk);FN("GetMethodEv",GetMethod);FN("artJniMethodStart",Native);FN("artJniMethodEnd",Runnable);FN("ScopedSuspendAllC2",SuspendAll);FN("ScopedSuspendAllD2",ResumeAll);FN("SuspendThreadByPeer",SuspendPeer);FN("ThreadList6Resume",Resume);FN("OatQuickMethodHeader8Contains",Contains);FN("JitCodeCache10ContainsPc",Cached);FN("GetOatMethodQuickCode",Code);
#undef FN
return nullptr;}void* Install(void*,void*,void**,std::string*)override{return nullptr;}bool Remove(void*,std::string*)override{return true;}};
int main(){ auto profile=MatchedProfileForTest();runtime.thread_list=&list;Hooks hooks;std::string error;assert(!ConfigureThreadSuspendScopes(hooks,artdcevm::android13::DeviceProfile{},&error));assert(ConfigureThreadSuspendScopes(hooks,profile,&error));assert(!ConfigureStackAccess(hooks,artdcevm::android13::DeviceProfile{},&error));assert(ConfigureStackAccess(hooks,profile,&error));hooks.missing=true;assert(!ConfigureCompiledFrameDiagnostics(hooks,profile,&error));hooks.missing=false;assert(!ConfigureCompiledFrameDiagnostics(hooks,artdcevm::android13::DeviceProfile{},&error));assert(ConfigureCompiledFrameDiagnostics(hooks,profile,&error));
Method caller{},callee{},other{};Executable a{},b{};a.art_method=reinterpret_cast<uint64_t>(&caller);b.art_method=reinterpret_cast<uint64_t>(&callee);jobject peer=reinterpret_cast<jobject>(target),executable=reinterpret_cast<jobject>(&a),second=reinterpret_cast<jobject>(&b);
alignas(4) uint32_t machine[32]{20};const void* header=machine;code=machine+1;uintptr_t pc=reinterpret_cast<uintptr_t>(code);caller.entry_point=reinterpret_cast<uint64_t>(code);
missing_target=true;int before=resumes;assert(DiagnosticAotFrameState(nullptr,peer,executable)==-1&&resumes==before);timed_out=true;assert(DiagnosticJitActiveFrameState(nullptr,peer,executable,second)==-2&&resumes==before);assert(DiagnosticAotSuspendDeoptProbe(nullptr,peer,executable)==-2);missing_target=timed_out=false;
frames={{&other,header,pc}};assert(DiagnosticAotFrameState(nullptr,peer,executable)==0);
frames={{&caller,nullptr,0,false}};assert(DiagnosticAotFrameState(nullptr,peer,executable)==1);
frames={{&caller,header,pc-1}};assert(DiagnosticAotFrameState(nullptr,peer,executable)==3);
frames={{&caller,header,pc}};assert(DiagnosticAotFrameState(nullptr,peer,executable)==7);
frames={{&caller,header,pc+20}};assert(DiagnosticAotFrameState(nullptr,peer,executable)==7);
frames={{&caller,header,pc+21}};assert(DiagnosticAotFrameState(nullptr,peer,executable)==3);
frames={{&caller,header,pc,false}};assert(DiagnosticAotFrameState(nullptr,peer,executable)==5); // source checks PC even on a shadow frame
frames={{&caller,header,pc,true,true}};assert(DiagnosticAotFrameState(nullptr,peer,executable)==0);
frames={{&caller,header,pc-1},{&caller,header,pc}};assert(DiagnosticAotFrameState(nullptr,peer,executable)==3); // first matching physical method
frames={{&caller,header,pc},{&callee,header,pc,true,true}};assert(DiagnosticJitActiveFrameState(nullptr,peer,executable,second)==15);
cached=false;assert(DiagnosticJitActiveFrameState(nullptr,peer,executable,second)==5);cached=true;has_cache=false;assert(DiagnosticJitActiveFrameState(nullptr,peer,executable,second)==5);has_cache=true;
frames={{&caller,header,pc},{&callee,header,pc+21,true,true}};assert(DiagnosticJitActiveFrameState(nullptr,peer,executable,second)==7);
frames={{&caller,header,pc}};assert(DiagnosticJitActiveFrameState(nullptr,peer,executable,executable)==15);
resume_ok=false;assert(DiagnosticAotFrameState(nullptr,peer,executable)==-3);assert(DiagnosticJitActiveFrameState(nullptr,peer,executable,second)==-3);resume_ok=true;
frames={{&other,header,pc}};probe=false;before=marks;assert(DiagnosticAotSuspendDeoptProbe(nullptr,peer,executable)==0&&!probe&&marks==before);
frames={{&caller,header,pc}};assert(DiagnosticAotSuspendDeoptProbe(nullptr,peer,executable)==1&&probe&&pending==target);
mark_ok=false;probe=false;assert(DiagnosticAotSuspendDeoptProbe(nullptr,peer,executable)==0&&probe);mark_ok=true;
frames={{&caller,machine+3,pc}};probe=false;assert(DiagnosticAotSuspendDeoptProbe(nullptr,peer,executable)==0&&!probe);
code=nullptr;frames={{&caller,nullptr,pc}};assert(DiagnosticAotSuspendDeoptProbe(nullptr,peer,executable)==1); // exact original null-header equality
resume_ok=false;assert(DiagnosticAotSuspendDeoptProbe(nullptr,peer,executable)==-3&&pending==target);resume_ok=true;
assert(DiagnosticAotSuspendDeoptHits(false)==42&&pending==target);before=resets;assert(DiagnosticAotSuspendDeoptHits(true)==42&&!pending&&!probe&&resets==before+1);
assert(!target_suspended&&starts==ends&&*reinterpret_cast<uint32_t*>(self)==0);assert(suspends==resumes+3);std::puts("compiled frame diagnostics: target suspension, AOT/JIT/inlined PCs, failures, pending state and reset passed");}
