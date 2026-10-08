#include "device_profile_fixture.h"
#include "platform/android13/evolution_transaction.h"
#include "platform/android13/evolution_transaction_abi.h"
#include "platform/android13/device_dex_input.h"
#include "platform/android13/thread_suspend_scope.h"
#include "platform/android13/native_object_access.h"
#include "platform/android13/handle_root_scope.h"
#include "platform/android13/stack_access.h"
#include "platform/android13/suspend_deopt_hooks.h"
#include "platform/android13/selective_deopt_abi.h"
#include "platform/android13/art_method_abi.h"
#include "platform/android13/validation_hooks.h"
#include "platform/android13/platform_string_abi.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <dlfcn.h>
#include <unordered_map>
#include <vector>
#include <sys/mman.h>
using namespace artdcevm::android13;
using Class=validation_abi::ClassPrefix;using Method=art_method_abi::ArtMethod;
using Map=validation_abi::DexMappingPrefix;
alignas(8) unsigned char self[384]{},worker[384]{};
runtime_access_abi::RuntimePrefix runtime{};void* runtime_ptr=&runtime;int list,mutex;void* mutex_ptr=&mutex;
bool locked=false,suspended=false,enabled=false,required=false,mode_ok=true,narrow=false,engine_narrow=false,debug=true,private_pc=true,busy=false,timeout=false,no_progress=false;
SuspendDeoptState request_state=SuspendDeoptState::kIdle;bool executing_timeout=false,rejected=false,recovery=true;
uint32_t hierarchy=9;int starts=0,ends=0,constructs=0,destroys=0,opens=0,strings=0,dex_destroys=0,engine_calls=0,marks=0,cancels=0,lookups=0;
jvmtiError engine_result=JVMTI_ERROR_NONE;void* pending=nullptr;void* marked_method=nullptr;
std::vector<std::string> events;
selective_abi::Tree empty_instrumentation_map{};
struct Frame {void* method;const void* header;uintptr_t pc=0x9000,ret=0;int physical=1;bool quick=true;};
std::vector<Frame> frames;std::unordered_map<void*,void*> declaring,loaders,parents;std::unordered_map<std::string,void*> classes;std::unordered_map<void*,const void*> codes;
std::unordered_map<void*,std::vector<void*>> roots;
void Native(void* t){assert(t==self);assert(*reinterpret_cast<uint32_t*>(t)==0);*reinterpret_cast<uint32_t*>(t)=92u<<24;++starts;events.push_back("native");}
void Runnable(void* t){assert(t==self);assert(*reinterpret_cast<uint32_t*>(t)==92u<<24);*reinterpret_cast<uint32_t*>(t)=0;++ends;events.push_back("runnable");}
void Suspend(void*,const char* c,bool b){assert(c&&!b&&!suspended&&(*reinterpret_cast<uint32_t*>(self)>>24)==92);suspended=true;++constructs;events.push_back("suspend");}
void Resume(void*){assert(suspended&&!locked);suspended=false;++destroys;events.push_back("resume");}
void Lock(void* p,void* t){assert(p==mutex_ptr&&t==self&&suspended&&!locked);locked=true;events.push_back("lock");}
void Unlock(void* p,void* t){assert(p==mutex_ptr&&t==self&&locked);locked=false;events.push_back("unlock");}
void ForEach(void* p,void(*fn)(void*,void*),void* x){assert(p==&list&&locked);events.push_back("foreach");fn(self,x);fn(worker,x);}
void Construct(void* raw,void* t,void* context,stack_abi::WalkKind kind,bool check){assert(check);auto* v=static_cast<stack_abi::Visitor*>(raw);std::memset(static_cast<unsigned char*>(raw)+8,0,sizeof(*v)-8);v->thread=t;v->context=context;v->kind=kind;void* foreign=reinterpret_cast<void*>(0x1234);std::memcpy(raw,&foreign,8);}
Frame& Active(const void* v){return frames.at(static_cast<const stack_abi::Visitor*>(v)->depth);}
void Walk(void* raw,bool transitions){auto* v=static_cast<stack_abi::Visitor*>(raw);assert(!transitions);if(v->thread!=worker)return;assert(locked&&suspended&&v->kind==stack_abi::WalkKind::kIncludeInlined);for(size_t i=0;i<frames.size();++i){auto& f=frames[i];v->depth=i;v->header=f.header;v->pc=f.pc;static void* quick_methods[16];quick_methods[f.physical]=f.method;v->quick_frame=f.quick?&quick_methods[f.physical]:nullptr;if(!v->VisitFrame())break;}}
void* GetMethod(const void* v){return Active(v).method;}
uintptr_t ReturnPc(const void* v){return Active(v).ret;}
void* Root(void* m){return declaring.at(m);}
void* Obsolete(void*){return nullptr;}
const void* Code(void* m,size_t s){assert(s==8);return codes[m];}
bool Debug(const void*){return debug;}
bool Contains(const void*,const void*){return true;}
bool Private(const void*,const void*){return private_pc;}
void* Lookup(void*,void* t,const char* name,void* loader){assert(t==self&&loader==reinterpret_cast<void*>(1));++lookups;return classes[name];}
jvmtiError Engine(jvmtiEnv*,jint,const jvmtiClassDefinition*){assert(!locked&&!suspended);++engine_calls;narrow=engine_narrow;return engine_result;}
struct Dex {virtual ~Dex(){++dex_destroys;}const unsigned char* begin;size_t size;const unsigned char* data;size_t data_size;Dex(const unsigned char* b,size_t n):begin(b),size(n),data(b),data_size(n){if(n>=512 && std::memcmp(b,"dex\n041\0",8)==0){begin=b+256;size=n-256;}}};
device_dex_abi::OwnedPointer Open(const void*,const unsigned char* b,size_t n,const BorrowedPlatformString*,uint32_t checksum,const void* oat,bool verify,bool sum,device_dex_abi::PlatformError* error,device_dex_abi::OwnedPointer container){assert(checksum==0&&!oat&&verify&&sum&&!container.object);++opens;assert(error->bytes[0]==0);if(n<112)return {};return device_dex_abi::OwnedPointer(new Dex(b,n));}
void DestroyString(void* raw){assert(raw);++strings;}
namespace artdcevm::android13 {
bool ReadEvolutionCodeInfo(void*,const void* header,uintptr_t,stack_abi::CodeInfo* info){*info={};info->inline_infos.rows=static_cast<const uint32_t*>(header)[-3]?1:0;return true;}
bool IsEvolutionFrameRecoverable(const stack_abi::Visitor&){return recovery;}bool IsEvolutionPcRecoverable(void*,uintptr_t){return recovery;}
void* CurrentHierarchyThread(){return self;}void* RuntimeClassLinker(){return &list;}void* RuntimeJitCodeCache(){return &mutex;}
bool ReceiverGuardEnabled(){return enabled;}bool ReceiverGuardRequired(){return required;}void SetReceiverGuardRequired(bool b){required=b;}
bool SetReceiverGuardMode(bool b,bool reject){assert(!reject);if(b&&!mode_ok)return false;if(b&&!enabled){NativeSuspendScope native_scope(self);assert(native_scope.Supported());OwnedSuspendAll suspended_scope("guard recovery preflight");if(!CheckEvolutionCompiledFramesRecoverable())return false;}enabled=b;return true;}
uint32_t BeginHierarchyTransaction(){auto previous=hierarchy;hierarchy=0;narrow=false;return previous;}void EndHierarchyTransaction(uint32_t previous){hierarchy=previous;}bool HasHierarchyNarrowing(){return narrow;}
bool IsValidationSubclassOf(void* source,void* target){return source==target||parents[source]==target;}
bool IsValidationAssignableFrom(void* target,void* source){return target==source||parents[source]==target;}
void* ReadHeapReference(void* p,size_t off,bool){if(off==offsetof(Class,class_loader))return loaders[p];if(off==offsetof(Class,super_class))return parents[p];uint32_t value;std::memcpy(&value,static_cast<unsigned char*>(p)+off,4);return reinterpret_cast<void*>(uintptr_t(value));}
NativeObjectAccess::NativeObjectAccess(JNIEnv*):self_(self),supported_(true),transitioned_(false){}NativeObjectAccess::~NativeObjectAccess(){assert(!transitioned_);}void* NativeObjectAccess::Decode(jobject o)const{return o;}
HandleRootScope::HandleRootScope(void* t,uint32_t n):thread_(t),count_(n){assert(t==self);roots[this].resize(n);}HandleRootScope::~HandleRootScope(){roots.erase(this);}void HandleRootScope::Set(size_t i,void* p){roots[this].at(i)=p;}void* HandleRootScope::Get(size_t i){return roots[this].at(i);}
JNIEnv* ValidationHooks::TransactionJni(){return nullptr;}
bool MarkEvolutionSuspendDeopt(void* t,uint64_t* token){if(token)*token=256;assert(locked&&suspended&&t==worker);if(request_state==SuspendDeoptState::kCompleted||request_state==SuspendDeoptState::kRejected)pending=nullptr;if(busy||pending)return false;pending=t;request_state=SuspendDeoptState::kQueued;++marks;for(auto& f:frames){if(loaders[declaring[f.method]]){marked_method=f.method;break;}}return true;}
bool EvolutionSuspendDeoptPending(void* t,uint64_t token){assert(token==256);assert(!locked&&!suspended);if(pending==t&&!timeout){request_state=rejected?SuspendDeoptState::kRejected:SuspendDeoptState::kCompleted;if(!no_progress&&!rejected)frames.clear();}if(executing_timeout)request_state=SuspendDeoptState::kExecuting;return pending==t&&(request_state==SuspendDeoptState::kQueued||request_state==SuspendDeoptState::kExecuting);}
void CancelEvolutionSuspendDeopt(void* t,uint64_t token){assert(token==256);assert(pending==t);if(request_state==SuspendDeoptState::kQueued){request_state=SuspendDeoptState::kRejected;++cancels;}}
SuspendDeoptState EvolutionSuspendDeoptState(void* t,uint64_t){return pending==t?request_state:SuspendDeoptState::kIdle;}
bool EvolutionSuspendDeoptInFlight(){return request_state==SuspendDeoptState::kClaimed||request_state==SuspendDeoptState::kExecuting;}
bool ReleaseEvolutionSuspendDeopt(void* t,uint64_t){if(t!=pending||(request_state!=SuspendDeoptState::kCompleted&&request_state!=SuspendDeoptState::kRejected))return false;pending=nullptr;request_state=SuspendDeoptState::kIdle;return true;}
}
class Hooks:public artdcevm::HookEngine{public:bool Initialize(std::string*)override{return true;}void* FindSymbol(const char*,const char* n)override{
#define FN(part,f) if(std::strstr(n,part))return reinterpret_cast<void*>(f)
FN("StackVisitorC2",Construct);FN("WalkStack",Walk);FN("GetMethodEv",GetMethod);FN("GetReturnPcAddr",ReturnPc);FN("artJniMethodStart",Native);FN("artJniMethodEnd",Runnable);FN("ScopedSuspendAllC2",Suspend);FN("ScopedSuspendAllD2",Resume);FN("LookupClass",Lookup);FN("ExclusiveLock",Lock);FN("ExclusiveUnlock",Unlock);FN("ThreadList7ForEach",ForEach);FN("PrivateRegionContainsPc",Private);FN("ContainsPc",Contains);FN("artReadBarrierForRootSlow",Root);FN("GetObsoleteDexCache",Obsolete);FN("GetOatMethodQuickCode",Code);FN("IsDebuggable",Debug);FN("StructurallyRedefineClasses",Engine);FN("DexFileLoader4Open",Open);FN("basic_string",DestroyString);
#undef FN
if(std::strstr(n,"Runtime9instance"))return &runtime_ptr;if(std::strstr(n,"thread_list_lock"))return &mutex_ptr;return nullptr;}
void* Install(void*,void*,void**,std::string*)override{return nullptr;}bool Remove(void*,std::string*)override{return true;}};
void Reset(){assert(!pending&&!locked&&!suspended);frames.clear();enabled=required=false;mode_ok=debug=private_pc=recovery=true;busy=timeout=no_progress=engine_narrow=executing_timeout=rejected=false;request_state=SuspendDeoptState::kIdle;engine_result=JVMTI_ERROR_NONE;events.clear();empty_instrumentation_map={};void* map=&empty_instrumentation_map;std::memcpy(worker+transaction_abi::kThreadInstrumentationStack,&map,sizeof(map));}
void Put(std::vector<unsigned char>& b,size_t off,uint32_t value){std::memcpy(b.data()+off,&value,4);}
int main(){ auto profile=MatchedProfileForTest();runtime.thread_list=&list;Hooks hooks;std::string error;assert(!ConfigureThreadSuspendScopes(hooks,artdcevm::android13::DeviceProfile{},&error));assert(ConfigureThreadSuspendScopes(hooks,profile,&error));assert(starts==0&&ends==0&&constructs==0&&destroys==0);assert(!ConfigureStackAccess(hooks,artdcevm::android13::DeviceProfile{},&error));assert(ConfigureStackAccess(hooks,profile,&error));if(!ConfigureEvolutionTransactions(hooks,profile,&error)){std::fprintf(stderr,"%s\n",error.c_str());assert(false);}assert(dex_destroys==0&&opens==0&&strings==0);
// Native/Runnable nesting and unsupported states are exercised directly.
{ NativeSuspendScope outer(self);assert(outer.Supported());
  { NativeSuspendScope nested(self);assert(nested.Supported()); }
  { OwnedSuspendAll suspend("owned scope host test");assert(suspended); }
}
assert(starts==1&&ends==1&&constructs==1&&destroys==1);
Native(self);{NativeSuspendScope already_native(self);assert(already_native.Supported());}Runnable(self);
*reinterpret_cast<uint32_t*>(self)=96u<<24;{NativeSuspendScope unsupported(self);assert(!unsupported.Supported());}
assert(*reinterpret_cast<uint32_t*>(self)==96u<<24);*reinterpret_cast<uint32_t*>(self)=0;
Class app{},boot{},old{},parent{},child{},iface{};loaders[&app]=reinterpret_cast<void*>(1);loaders[&old]=reinterpret_cast<void*>(1);parents[&old]=&parent;parents[&child]=&parent;classes["LParent;"]=&parent;classes["LChild;"]=&child;classes["LIface;"]=&iface;
Method method{},native{},runtime_method{},boot_method{};method.dex_method_index=1;native=method;native.access_flags=0x100;runtime_method.dex_method_index=UINT32_MAX;boot_method=method;declaring[&method]=declaring[&native]=declaring[&runtime_method]=&app;declaring[&boot_method]=&boot;
alignas(4) uint32_t code[8]{};const void* header=code+4;code[4]=transaction_abi::kCodeInfo|16;code[1]=1;
Reset();assert(DrainEvolutionCompiledFrames());assert((events==std::vector<std::string>{"native","suspend","lock","foreach","unlock","resume","runnable"}));
Reset();frames={{&native,header},{&runtime_method,header},{&boot_method,nullptr},{&method,header,0x9000,0,1,false}};assert(DrainEvolutionCompiledFrames());
Reset();frames={{&method,header}};int before=marks;assert(DrainEvolutionCompiledFrames()&&marks==before+1);
Reset();frames={{&boot_method,header}};assert(DrainEvolutionCompiledFrames());assert(EvolutionCompiledPrecommitDiagnostics(5)>0);
Reset();code[1]=0;private_pc=false;frames={{&boot_method,header}};assert(DrainEvolutionCompiledFrames());code[1]=1;
Reset();private_pc=false;frames={{&runtime_method,header,0x9000,0,1},{&boot_method,header,0x9000,0,1}};assert(DrainEvolutionCompiledFrames());
Reset();frames={{&boot_method,header}};private_pc=false;assert(!DrainEvolutionCompiledFrames());
Reset();frames={{&method,header}};busy=true;assert(!DrainEvolutionCompiledFrames());
Reset();frames={{&method,header}};timeout=true;before=cancels;assert(!DrainEvolutionCompiledFrames()&&cancels==before+1);
Reset();frames={{&method,header}};recovery=false;before=engine_calls;auto unsupported_before=EvolutionCompiledPrecommitDiagnostics(4);assert(RedefineEvolutionTransaction(nullptr,0,nullptr,true)==JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED&&engine_calls==before&&!enabled&&!required&&!pending);assert(EvolutionCompiledPrecommitDiagnostics(4)==unsupported_before+1);
 Reset();frames={{&method,header}};enabled=required=true;recovery=false;unsupported_before=EvolutionCompiledPrecommitDiagnostics(4);before=engine_calls;assert(RedefineEvolutionTransaction(nullptr,0,nullptr,true)==JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED&&engine_calls==before&&enabled&&required&&!pending);assert(EvolutionCompiledPrecommitDiagnostics(4)==unsupported_before+1);
Reset();frames={{&method,header}};rejected=true;before=engine_calls;assert(RedefineEvolutionTransaction(nullptr,0,nullptr,true)==JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED&&engine_calls==before&&!enabled&&!required&&!pending);
Reset();frames={{&method,header}};timeout=executing_timeout=true;before=engine_calls;assert(RedefineEvolutionTransaction(nullptr,0,nullptr,true)==JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED&&engine_calls==before&&enabled&&required&&pending);assert(!DrainEvolutionCompiledFrames());request_state=SuspendDeoptState::kCompleted;assert(ReleaseEvolutionSuspendDeopt(worker));
Reset();frames={{&method,header}};no_progress=true;before=marks;assert(!DrainEvolutionCompiledFrames()&&marks==before+64);
// Real fixed AOT metadata graph and exit-barrier tree. A current frame's own barrier is insufficient.
void* low=mmap(reinterpret_cast<void*>(0x24000000),4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON|MAP_FIXED,-1,0);assert(low==reinterpret_cast<void*>(0x24000000));auto* cache=new(low)validation_abi::DexCachePrefix{};stack_abi::DexPrefix dex{};stack_abi::OatDexPrefix oat{&list};dex.oat_dex=&oat;cache->dex_file=reinterpret_cast<uint64_t>(&dex);app.dex_cache=uint32_t(reinterpret_cast<uintptr_t>(cache));codes[&method]=code+5;
Reset();debug=false;frames={{&method,header}};assert(!DrainEvolutionCompiledFrames());Reset();frames={{&method,header}};assert(DrainEvolutionCompiledFrames());
selective_abi::MapNode node{};node.key=77;selective_abi::Tree tree{&node,&node.base,1};Reset();debug=false;void* map=&tree;std::memcpy(worker+transaction_abi::kThreadInstrumentationStack,&map,sizeof(map));frames={{&native,header,0x9000,77,1},{&method,header,0x9000,0,2}};before=marks;assert(DrainEvolutionCompiledFrames()&&marks==before);Reset();debug=false;std::memcpy(worker+transaction_abi::kThreadInstrumentationStack,&map,sizeof(map));frames={{&method,header,0x9000,77,1}};assert(!DrainEvolutionCompiledFrames());
Reset();before=engine_calls;assert(RedefineEvolutionTransaction(nullptr,0,nullptr,true)==JVMTI_ERROR_NONE&&engine_calls==before+1&&!enabled&&!required&&hierarchy==9);
Reset();engine_narrow=true;assert(RedefineEvolutionTransaction(nullptr,0,nullptr,true)==JVMTI_ERROR_NONE&&enabled&&required);
Reset();engine_result=JVMTI_ERROR_FAILS_VERIFICATION;assert(RedefineEvolutionTransaction(nullptr,0,nullptr,true)==engine_result&&!enabled&&!required);
Reset();mode_ok=false;before=engine_calls;assert(RedefineEvolutionTransaction(nullptr,0,nullptr,true)==JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED&&engine_calls==before&&hierarchy==9);
Reset();frames={{&method,header}};debug=false;before=engine_calls;assert(RedefineEvolutionTransaction(nullptr,0,nullptr,true)==JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED&&engine_calls==before&&!enabled&&!required);
Reset();enabled=required=true;engine_result=JVMTI_ERROR_FAILS_VERIFICATION;assert(RedefineEvolutionTransaction(nullptr,0,nullptr,true)==engine_result&&enabled&&required);
// Genuine DEX table format, verifier result remains owned by the original opener.
std::vector<unsigned char> bytes(256);std::memcpy(bytes.data(),"dex\n039\0",8);Put(bytes,32,256);Put(bytes,36,112);Put(bytes,40,0x12345678);Put(bytes,56,2);Put(bytes,60,112);Put(bytes,64,2);Put(bytes,68,120);Put(bytes,96,1);Put(bytes,100,128);Put(bytes,112,180);Put(bytes,116,200);Put(bytes,120,0);Put(bytes,124,1);Put(bytes,136,0);bytes[180]=8;std::memcpy(bytes.data()+181,"LParent;",9);bytes[200]=7;std::memcpy(bytes.data()+201,"LIface;",8);jvmtiClassDefinition def{reinterpret_cast<jclass>(&old),int(bytes.size()),bytes.data()};Reset();lookups=0;assert(!NeedsEvolutionReceiverGuard(1,&def)&&lookups==1);classes["LParent;"]=nullptr;assert(NeedsEvolutionReceiverGuard(1,&def));classes["LParent;"]=&parent;
// Full-width superclass indices: NO_INDEX requests guarding of a removed
// superclass, and an invalid high index stays for the original verifier.
Put(bytes,136,UINT32_MAX);lookups=0;assert(NeedsEvolutionReceiverGuard(1,&def)&&lookups==0);
Put(bytes,136,0x10000);lookups=0;assert(!NeedsEvolutionReceiverGuard(1,&def)&&lookups==0);
Put(bytes,136,0);
// With no old interfaces, even an unresolved added interface is not looked up.
Put(bytes,140,224);Put(bytes,224,1);bytes[228]=1;classes["LIface;"]=nullptr;lookups=0;assert(!NeedsEvolutionReceiverGuard(1,&def)&&lookups==1);
// v041 later-header ID offsets are based at DataBegin, not the selected header.
std::vector<unsigned char> container(512);
std::memcpy(container.data(),"dex\n041\0",8);
std::memcpy(container.data()+256,bytes.data(),112);
std::memcpy(container.data()+256,"dex\n041\0",8);
std::memcpy(container.data()+120,bytes.data()+112,128);
Put(container,256+36,120);Put(container,256+60,120);Put(container,256+68,128);Put(container,256+100,136);
Put(container,256+112,512);Put(container,256+116,256);
Put(container,120,188);Put(container,124,208);Put(container,148,232);
jvmtiClassDefinition later{reinterpret_cast<jclass>(&old),int(container.size()),container.data()};
classes["LParent;"]=nullptr;lookups=0;
assert(NeedsEvolutionReceiverGuard(1,&later)&&lookups==1);
classes["LParent;"]=&parent;
assert(!NeedsEvolutionReceiverGuard(1,&later));
// Once an old interface exists, unresolved replacements force guarding even if superclass retains it.
auto* table=static_cast<unsigned char*>(low)+128;Put(bytes,136,0);int32_t length=2;std::memcpy(table+8,&length,4);uint32_t ref=uint32_t(reinterpret_cast<uintptr_t>(low)+256);std::memcpy(table+12,&ref,4);old.iftable=uint32_t(reinterpret_cast<uintptr_t>(table));parents[&parent]=reinterpret_cast<void*>(uintptr_t(ref));assert(NeedsEvolutionReceiverGuard(1,&def));classes["LIface;"]=reinterpret_cast<void*>(uintptr_t(ref));assert(!NeedsEvolutionReceiverGuard(1,&def));
assert(roots.empty()&&starts==ends&&constructs==destroys);
assert(strings==opens&&dex_destroys==opens); // each transaction open destroys its platform error and accepted DEX
assert(EvolutionGuardTransactionDiagnostics(-1)==0&&EvolutionCompiledPrecommitDiagnostics(6)==0);munmap(low,4096);std::puts("evolution transaction ownership, drain, barriers, verifier and rollback: passed");}
