#include "platform/android13/stack_access.h"
#include "platform/android13/async_deopt_hooks.h"
#include "platform/android13/selective_deopt.h"
#include "platform/android13/selective_deopt_abi.h"
#include "platform/android13/art_method_abi.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <dlfcn.h>
#include <unordered_map>
#include <vector>
#include <string>
#include <sys/mman.h>
using namespace artdcevm::android13;
using Method=art_method_abi::ArtMethod;
using Class=validation_abi::ClassPrefix;
using Cache=validation_abi::DexCachePrefix;
struct Frame {void* method;uintptr_t pc;const void* header;bool quick=true,inlined=false;uint8_t debug=1;uintptr_t return_pc=0;};
std::unordered_map<void*,std::vector<Frame>> stacks;
std::unordered_map<void*,void*> declaring,loaders;
std::unordered_map<void*,const void*> codes;
void* current=nullptr;bool required=false,original_result=false,oat_debug=true;
size_t walks=0,original_calls=0,cache_calls=0,code_calls=0,contexts_deleted=0;
std::vector<std::string> events;
bool locked=false;
selective_abi::RuntimePrefix runtime{};void* runtime_ptr=&runtime;int mutex_marker=0,list_marker=0;void* lock_ptr=&mutex_marker;
alignas(8) unsigned char thread1[320]{},thread2[320]{};
void (*set_probe)(bool)=nullptr;uint64_t (*get_hits)()=nullptr;bool (*proxy)(const void*,uintptr_t)=nullptr;
Cache* obsolete_cache=nullptr;
Frame& Active(const stack_abi::Visitor* visitor){return stacks.at(visitor->thread).at(visitor->depth);}
void Construct(void* raw,void* thread,void* context,stack_abi::WalkKind kind,bool suspended){
 assert(suspended);auto* visitor=static_cast<stack_abi::Visitor*>(raw);
 // Device ctor installs its own vptr. A missing enhancement restoration crashes the virtual walk.
 void* foreign_vptr=reinterpret_cast<void*>(0x12345678);
 std::memset(static_cast<unsigned char*>(raw)+sizeof(void*),0,sizeof(stack_abi::Visitor)-sizeof(void*));
 visitor->thread=thread;visitor->context=context;visitor->kind=kind;visitor->check_suspended=suspended;
 visitor->num_frames=0x7171;visitor->inline_frames.begin.row=0x1818;visitor->inline_info.info.flags=0x2121;
 if(locked)events.push_back("construct");
 std::memcpy(raw,&foreign_vptr,sizeof(foreign_vptr));
}
void Walk(void* raw,bool transitions){auto* visitor=static_cast<stack_abi::Visitor*>(raw);++walks;
 assert(visitor->num_frames==0x7171&&visitor->inline_frames.begin.row==0x1818&&visitor->inline_info.info.flags==0x2121);
 if(locked){assert(transitions&&visitor->kind==stack_abi::WalkKind::kIncludeInlined);events.push_back("walk");}
 else assert(!transitions&&visitor->kind==stack_abi::WalkKind::kSkipInlined);
 auto& frames=stacks[visitor->thread];for(size_t i=0;i<frames.size();++i){auto& frame=frames[i];if(frame.inlined&&visitor->kind==stack_abi::WalkKind::kSkipInlined)continue;
 visitor->depth=i;visitor->pc=frame.pc;visitor->header=frame.header;visitor->quick_frame=frame.quick?&frame.method:nullptr;
 visitor->shadow_frame=frame.quick?nullptr:reinterpret_cast<void*>(0x5511);visitor->inline_frames.end.row=uint32_t(i);
 if(!visitor->VisitFrame())break;}}
void* GetMethod(const void* visitor){return Active(static_cast<const stack_abi::Visitor*>(visitor)).method;}
void* Root(void* method){return declaring.at(method);}
void* GetObsolete(void*){++cache_calls;return obsolete_cache;}
const void* OatCode(void* method,size_t pointer_size){assert(pointer_size==8);++code_calls;return codes.at(method);}
bool Debuggable(const void*){return oat_debug;}
bool Original(const void*,uintptr_t){++original_calls;return original_result;}
namespace artdcevm::android13 {void* CurrentHierarchyThread(){return current;}bool ReceiverGuardRequired(){return required;}
void* ReadHeapReference(void* object,size_t offset,bool){assert(offset==offsetof(Class,class_loader));return loaders.at(object);}}
extern "C" bool ArtDcevmSourceProbeStackAccess(void (*test)(void*)){void* probe_thread=reinterpret_cast<void*>(0x9999);stacks[probe_thread]={};test(probe_thread);return true;}
extern "C" void ArtDcevmSourceConfigureAsyncDeopt(void (*set)(bool),uint64_t (*hits)()){set_probe=set;get_hits=hits;}
struct Context {virtual ~Context(){assert(locked);++contexts_deleted;events.push_back("delete");}};
void* MakeContext(){assert(locked);events.push_back("context");return new Context;}
void Lock(void* mutex,void* self){assert(!locked&&mutex==lock_ptr&&self==current);locked=true;events.push_back("lock");}
void Unlock(void* mutex,void* self){assert(locked&&mutex==lock_ptr&&self==current);events.push_back("unlock");locked=false;}
void ForEach(void* list,void (*fn)(void*,void*),void* data){assert(locked&&list==&list_marker);events.push_back("foreach");fn(thread1,data);fn(thread2,data);}
void Instrument(void* receiver,void* thread,bool force){assert(locked&&!force&&receiver==&runtime.instrumentation);events.push_back(thread==thread1?"instrument1":"instrument2");}
uintptr_t ReturnPc(const void* visitor){return Active(static_cast<const stack_abi::Visitor*>(visitor)).return_pc;}
uint8_t* FlagAddr(const void* visitor){return &Active(static_cast<const stack_abi::Visitor*>(visitor)).debug;}
extern "C" bool ArtDcevmSourceProbeSelectiveDeopt(selective_abi::Frame* (*find)(void*,uintptr_t),void* value,void* instrumentation){
 assert(value==&runtime&&instrumentation==&runtime.instrumentation);selective_abi::MapNode root{},left{},right{};
 root.key=7;left.key=3;right.key=11;root.base.left=&left.base;root.base.right=&right.base;
 selective_abi::Tree tree{&left,&root.base,3};assert(find(&tree,3)==&left.value&&find(&tree,11)==&right.value&&find(&tree,7)==&root.value&&!find(&tree,6));return true;}
class Hooks:public artdcevm::HookEngine {public:
 bool Initialize(std::string*)override{return true;}void* FindSymbol(const char*,const char* symbol)override{
 if(std::strstr(symbol,"StackVisitorC2"))return reinterpret_cast<void*>(Construct);
 if(std::strstr(symbol,"WalkStack"))return reinterpret_cast<void*>(Walk);
 if(std::strstr(symbol,"GetMethodEv"))return reinterpret_cast<void*>(GetMethod);
 if(std::strcmp(symbol,"artReadBarrierForRootSlow")==0)return reinterpret_cast<void*>(Root);
 if(std::strstr(symbol,"GetObsoleteDexCache"))return reinterpret_cast<void*>(GetObsolete);
 if(std::strstr(symbol,"GetOatMethodQuickCode"))return reinterpret_cast<void*>(OatCode);
 if(std::strstr(symbol,"IsDebuggable"))return reinterpret_cast<void*>(Debuggable);
 if(std::strstr(symbol,"IsAsyncDeoptimizeable"))return reinterpret_cast<void*>(Original);
 if(std::strstr(symbol,"Runtime9instance"))return &runtime_ptr;
 if(std::strstr(symbol,"thread_list_lock"))return &lock_ptr;
 if(std::strstr(symbol,"ExclusiveLock"))return reinterpret_cast<void*>(Lock);
 if(std::strstr(symbol,"ExclusiveUnlock"))return reinterpret_cast<void*>(Unlock);
 if(std::strstr(symbol,"ThreadList7ForEach"))return reinterpret_cast<void*>(ForEach);
 if(std::strstr(symbol,"InstrumentThreadStack"))return reinterpret_cast<void*>(Instrument);
 if(std::strstr(symbol,"Context6Create"))return reinterpret_cast<void*>(MakeContext);
 if(std::strstr(symbol,"GetReturnPcAddr"))return reinterpret_cast<void*>(ReturnPc);
 if(std::strstr(symbol,"GetShouldDeoptimizeFlagAddr"))return reinterpret_cast<void*>(FlagAddr);
 return nullptr;}
 void* Install(void* target,void* replacement,void** original,std::string*)override{assert(target==reinterpret_cast<void*>(Original));*original=target;proxy=reinterpret_cast<bool (*)(const void*,uintptr_t)>(replacement);return this;}
 bool Remove(void*,std::string*)override{return true;}};
int main(){Hooks hooks;std::string error;void* self=dlopen(nullptr,RTLD_NOW);assert(InstallAsyncDeoptHook(hooks,self,&error));assert(ConfigureSelectiveDeopt(hooks,self,&error));
 void* low=mmap(reinterpret_cast<void*>(0x20000000),4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON|MAP_FIXED,-1,0);assert(low==reinterpret_cast<void*>(0x20000000));
 auto* cache=new(low)Cache{};obsolete_cache=reinterpret_cast<Cache*>(static_cast<unsigned char*>(low)+64);new(obsolete_cache)Cache{};
 Class app{},boot{};Method method{},native{},runtime_method{},boot_method{};method.dex_method_index=1;native=method;native.access_flags=0x100;runtime_method.dex_method_index=UINT32_MAX;boot_method=method;
 declaring[&method]=declaring[&native]=declaring[&runtime_method]=&app;declaring[&boot_method]=&boot;loaders[&app]=reinterpret_cast<void*>(1);loaders[&boot]=nullptr;
 alignas(8) uint32_t machine_code[8]{};void* code=machine_code+1;const void* header=machine_code;uintptr_t pc=reinterpret_cast<uintptr_t>(code)+4;
 stack_abi::DexPrefix dex{};stack_abi::OatDexPrefix oat{reinterpret_cast<void*>(0x5550)};dex.oat_dex=&oat;cache->dex_file=reinterpret_cast<uint64_t>(&dex);obsolete_cache->dex_file=cache->dex_file;app.dex_cache=uint32_t(reinterpret_cast<uintptr_t>(cache));codes[&method]=code;
 current=thread1;stacks[thread1]={{&method,pc,header}};
 size_t before=walks;original_result=true;assert(proxy(&runtime,pc)&&walks==before&&get_hits()==0);original_result=false;assert(!proxy(&runtime,pc)&&walks==before);
 set_probe(true);current=nullptr;assert(!proxy(&runtime,pc)&&walks==before);current=thread1;
 assert(proxy(&runtime,pc)&&get_hits()==1);before=walks;assert(!proxy(&runtime,pc+8)&&walks==before+1&&get_hits()==1);
 stacks[thread1][0].quick=false;assert(!proxy(&runtime,pc));stacks[thread1][0].quick=true;
 for(void* bad:{static_cast<void*>(&native),static_cast<void*>(&runtime_method),static_cast<void*>(&boot_method),static_cast<void*>(nullptr)}){stacks[thread1][0].method=bad;assert(!proxy(&runtime,pc));}stacks[thread1][0].method=&method;
 stacks[thread1][0].inlined=true;assert(!proxy(&runtime,pc));stacks[thread1][0].inlined=false;
 void* saved_class=declaring[&method];declaring[&method]=nullptr;assert(!proxy(&runtime,pc));declaring[&method]=saved_class;
 oat_debug=false;assert(!proxy(&runtime,pc));oat_debug=true;
 stacks[thread1][0].header=machine_code+2;assert(!proxy(&runtime,pc));stacks[thread1][0].header=header;
 codes[&method]=nullptr;assert(!proxy(&runtime,pc));codes[&method]=code;
 stacks[thread1][0].pc=reinterpret_cast<uintptr_t>(code)-1;assert(!proxy(&runtime,reinterpret_cast<uintptr_t>(code)-1));stacks[thread1][0].pc=pc;
 method.access_flags=0x40000;app.dex_cache=0;assert(proxy(&runtime,pc)&&cache_calls==1&&get_hits()==2); Cache* saved_obsolete=obsolete_cache;obsolete_cache=nullptr;assert(!proxy(&runtime,pc));obsolete_cache=saved_obsolete;
 method.access_flags=0;app.dex_cache=uint32_t(reinterpret_cast<uintptr_t>(cache));
 dex.oat_dex=nullptr;assert(!proxy(&runtime,pc));dex.oat_dex=&oat;oat.oat=nullptr;assert(!proxy(&runtime,pc));oat.oat=reinterpret_cast<void*>(0x5550);
 // First matching native frame terminates the AOT safety search; an app frame at the same PC cannot override it.
 stacks[thread1]={{&native,pc,header},{&method,pc,header}};assert(!proxy(&runtime,pc));
 set_probe(false);required=true;stacks[thread1]={{&method,pc,header}};assert(proxy(&runtime,pc)&&get_hits()==3);required=false;
 // A physical boot frame may contain an inlined app method, which selective deopt must include.
 uint32_t flagged=selective_abi::kShouldDeoptimizeMask,plain=0;
 stacks[thread1]={{&boot_method,1,&flagged},{&method,2,&flagged,true,true,0x41},{&native,3,&flagged},{&runtime_method,4,&flagged},{&method,5,&flagged,false}};
 stacks[thread2]={{&method,6,&plain,true,false,1,77},{&method,7,nullptr,true,false,1,80}};
 selective_abi::MapNode node{};node.key=77;node.value.force_deopt_id=123;selective_abi::MapNode right{};right.key=80;right.value.force_deopt_id=456;node.base.right=&right.base;
 selective_abi::Tree tree{&node,&node.base,2};void* tree_ptr=&tree;
 std::memcpy(thread2+selective_abi::kThreadInstrumentationStack,&tree_ptr,sizeof(tree_ptr));std::memcpy(thread1+selective_abi::kThreadInstrumentationStack,&tree_ptr,sizeof(tree_ptr));runtime.services.thread_list=&list_marker;
 events.clear();DeoptimizeApplicationFrames();assert(!locked&&contexts_deleted==2);assert(stacks[thread1][1].debug==(0x41|selective_abi::kDebugFlag));assert(stacks[thread1][0].debug==1&&stacks[thread1][2].debug==1&&stacks[thread1][3].debug==1&&stacks[thread1][4].debug==1);assert(node.value.force_deopt_id==UINT64_MAX&&right.value.force_deopt_id==UINT64_MAX);
 std::vector<std::string> expected{"lock","foreach","instrument1","context","construct","walk","delete","instrument2","context","construct","walk","delete","unlock"};assert(events==expected);
 assert(original_calls>15&&code_calls==6);munmap(low,4096);
 puts("compiled-frame deopt: real ctor/vtable walk, async original priority and AOT/obsolete boundaries, inlined app flags, frame-map sentinel, lock/context lifetime passed");}
