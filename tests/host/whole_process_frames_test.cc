#define main stop_fixture_main
#include "native_source_stop_test.cc"
#undef main
#include "platform/common/whole_process_frames.h"
#include "platform/abi/arm64/android-common/closure.h"
#include <array>
namespace {
int thread_list,list_mutex,worker,empty_thread,jit_cache;void* list_mutex_pointer=&list_mutex;
bool list_locked=false,include_self=true,unknown=false,unmarked=false,aot=false,nterp=false,wrong_native_map=false,wrong_dexpc=false,inline_frame=false,wrong_native_value=false,cursor_drift=false;
struct MethodPrefix {uint32_t klass=0,flags=0;} shadow_method,native_method{0,0x100},compiled_method;
void* current_method=nullptr;
alignas(8) uint32_t header[2]{};
struct Quick {void* method=&compiled_method;uint32_t values[14]{123,345};} quick;
std::array<uint32_t,8> stack_row{{0,0,11,0,0,1,1,1}};
std::array<uint32_t,6> inline_row{{0,12,0,0,0,3}};
uint32_t dexmask=3;uint32_t dexmaps[2]{1,2},catalog[4]{1,3,1,4};
struct Closure final:art::Closure {void Run(art::Thread*)override{}} closure;
void ClearCaches(){assert(!exclusive&&!list_locked);}
void ListLock(void* m,void* t){assert(m==&list_mutex&&t==&self&&gc&&exclusive&&!list_locked);list_locked=true;}
void ListUnlock(void* m,void* t){assert(m==&list_mutex&&t==&self&&gc&&exclusive&&list_locked);list_locked=false;}
void ForEach(void* list,void(*visit)(void*,void*),void* state){assert(list==&thread_list&&list_locked&&exclusive&&gc);if(include_self)visit(&self,state);visit(&worker,state);visit(&empty_thread,state);}
struct Context final:abi::arm64::context::Prefix {
 void Reset()override{}~Context()override=default;void FillCalleeSaves(uint8_t*,const abi::arm64::context::FrameInfo&)override{}
 void SetSP(uintptr_t)override{}void SetPC(uintptr_t)override{}void SetArg0(uintptr_t)override{}
 bool IsAccessibleGPR(uint32_t)override{return false;}uintptr_t* GetGPRAddress(uint32_t)override{return nullptr;}
 uintptr_t GetGPR(uint32_t)override{assert(false);return 0;}void SetGPR(uint32_t,uintptr_t)override{}
 bool IsAccessibleFPR(uint32_t)override{return false;}uintptr_t GetFPR(uint32_t)override{assert(false);return 0;}
};
abi::arm64::context::Prefix* CreateContext(){assert(!exclusive);return new Context;}
void ConstructVisitor(void* p,void* t,void* c,abi::arm64::stack::WalkKind kind,bool check){
 assert(gc&&exclusive&&list_locked&&check&&kind==abi::arm64::stack::WalkKind::kIncludeInlined);
 auto* v=static_cast<abi::arm64::stack::Visitor13*>(p);std::memset(static_cast<unsigned char*>(p)+8,0,sizeof(*v)-8);v->thread=t;v->kind=kind;v->context=c;
}
void WalkVisitor(void* p,bool transitions){
 assert(gc&&exclusive&&list_locked&&transitions);auto* v=static_cast<abi::arm64::stack::Visitor13*>(p);
 if(v->thread==&empty_thread)return;
 if(v->thread==&self){current_method=&native_method;quick.method=current_method;v->quick_frame=reinterpret_cast<void**>(&quick);v->header=nullptr;v->VisitFrame();return;}
 assert(v->thread==&worker);
 current_method=&shadow_method;v->shadow_frame=&worker;if(!v->VisitFrame())return;
 current_method=&compiled_method;quick.method=current_method;v->shadow_frame=nullptr;v->quick_frame=reinterpret_cast<void**>(&quick);v->header=header;v->pc=reinterpret_cast<uintptr_t>(header)+36;
 if(unknown)v->header=nullptr;
 if(inline_frame)v->inline_frames.end.row=1;
 v->VisitFrame();
}
void* GetMethod(const void*){return current_method;}
uint32_t DexPc(const void*,bool crash){assert(!crash);return (inline_frame?11:10)+(wrong_dexpc?1:0);}
bool Flag(const void* h){assert(h==header);return !unmarked;}
bool Contains(const void* j,const void*){assert(j==&jit_cache);return !aot;}
bool IsNterp(const void* h){assert(h==header);return nterp;}
size_t Offset(const void*){return 32;}
template<size_t N>void Table(abi::arm64::android13::BitTable<N>& table,void* data,size_t bits,uint32_t rows,unsigned width){table.region={data,0,bits};table.rows=rows;for(size_t i=0;i<=N;++i)table.columns[i]=i*width;}
void CodeInfo(abi::arm64::android13::CodeInfo* storage,const void* h){
 assert(h==header);auto& info=*storage;info={};info.flags=1;info.frame_size=4;info.code_size=64;info.dex_regs=1;
 Table(info.stack_maps,stack_row.data(),256,1,32);Table(info.inline_infos,inline_row.data(),192,1,32);
 Table(info.dex_masks,&dexmask,2,1,2);Table(info.dex_maps,dexmaps,64,2,32);Table(info.dex_catalog,catalog,128,2,32);
}
abi::arm64::android11::TableIterator FindMap(const abi::arm64::android13::CodeInfo* storage,uintptr_t pc,int32_t isa){assert(pc==32&&isa==2);auto* info=static_cast<const abi::arm64::android13::CodeInfo*>(storage);return {const_cast<void*>(static_cast<const void*>(&info->stack_maps)),0};}
bool NativeFast(const void* visitor,abi::arm64::android12::frame_registers::Location location,uint32_t* out){assert(location.kind==0&&(location.value==8||location.value==12));*out=quick.values[location.value/4-2]+(wrong_native_value?1:0);if(cursor_drift)++static_cast<abi::arm64::stack::Visitor13*>(const_cast<void*>(visitor))->pc;return true;}
void NativeMap(const void*,uint32_t row,uint32_t first,abi::arm64::dex_register_map::Map* map){assert(row==0&&first==(inline_frame?1u:0u)&&map->count==1);map->data()[0]={wrong_native_map?1:0,inline_frame?12:8};}
class FrameHooks:public Hooks {
 public:void* FindSymbol(const char* library,const char* name)override {
#define FN(symbol,fn) if(!std::strcmp(name,symbol))return Fn(fn)
 FN("_ZN3art6Thread25ClearAllInterpreterCachesEv",ClearCaches);
 if(!std::strcmp(name,"_ZZN3art6Thread25ClearAllInterpreterCachesEvE7closure"))return &closure;
 if(!std::strcmp(name,"_ZN3art5Locks17thread_list_lock_E"))return &list_mutex_pointer;
 FN("_ZN3art5Mutex13ExclusiveLockEPNS_6ThreadE",ListLock);FN("_ZN3art5Mutex15ExclusiveUnlockEPNS_6ThreadE",ListUnlock);
 FN("_ZN3art10ThreadList7ForEachEPFvPNS_6ThreadEPvES3_",ForEach);
 FN(abi::arm64::android12::frame_registers::kFast,NativeFast);
 FN(abi::arm64::context::kCreate,CreateContext);FN(abi::arm64::stack::kConstruct,ConstructVisitor);FN(abi::arm64::stack::kWalk,WalkVisitor);FN(abi::arm64::stack::kMethod,GetMethod);
 FN(abi::arm64::android13::kConstructCodeInfo,CodeInfo);FN(abi::arm64::android13::kFindStackMap,FindMap);FN(abi::arm64::dex_register_map::kDecode,NativeMap);
 FN("_ZNK3art12StackVisitor8GetDexPcEb",DexPc);FN("_ZNK3art20OatQuickMethodHeader23HasShouldDeoptimizeFlagEv",Flag);FN("_ZNK3art3jit12JitCodeCache10ContainsPcEPKv",Contains);FN("_ZNK3art20OatQuickMethodHeader19IsNterpMethodHeaderEv",IsNterp);FN(abi::arm64::android14::frame_registers::kNativePcOffset,Offset);
#undef FN
 return Hooks::FindSymbol(library,name);
 }
};
}
int main(){
 FrameHooks hooks;std::string error;assert(native_source_stop::Install(hooks,&error));
 native_cache::Service cache;assert(cache.Bind(hooks,&thread_list,&error)&&cache.Prepare(&error));
 native_source_stop::Service stop;auto states=State(hooks);assert(native_source_stop::Service::Bind(hooks,&heap,states,&stop,&error));
 Calibrate(stop);self.flags=92u<<24;
 whole_process_frames::Service scanner;assert(whole_process_frames::Service::Bind(code_info_view::SourceFamily::kAndroid14,[&](const char* name){return hooks.FindSymbol("libart.so",name);},&scanner));
 whole_process_frames::Service::Prepared storage;assert(scanner.Prepare(&storage));self.flags=0;
 {native_source_stop::Session owner(stop);auto r=scanner.Scan(owner,&self,cache,&jit_cache,storage);assert(!r.accepted);}
 auto scan=[&](){native_source_stop::Session owner(stop);assert(owner.Begin(&self,sizeof(self)));return scanner.Scan(owner,&self,cache,&jit_cache,storage);};
 auto report=scan();assert(report.accepted&&report.registered_threads==3&&report.walked_threads==3&&report.empty_threads==1&&report.native_frames==1&&report.shadow_frames==1&&report.compiled_frames==1&&report.map_locations==1&&report.native_value_reads==1&&!list_locked&&!gc);
 inline_frame=true;report=scan();assert(report.accepted&&report.inlined_frames==1&&report.compiled_frames==1);inline_frame=false;
 for(auto* flag:{&unknown,&unmarked,&aot,&nterp,&wrong_native_map,&wrong_dexpc,&wrong_native_value}){*flag=true;report=scan();assert(!report.accepted&&report.registered_threads==3&&report.walked_threads==1&&!list_locked&&!gc&&self.flags==0);*flag=false;}
 cursor_drift=true;report=scan();assert(!report.accepted&&!std::strcmp(report.failure,"whole-frame-source-owner-or-native-cursor-lost-after-map"));cursor_drift=false;
 inline_frame=true;unmarked=true;report=scan();assert(!report.accepted);inline_frame=unmarked=false;
 include_self=false;report=scan();assert(!report.accepted&&report.registered_threads==2&&report.walked_threads==2);include_self=true;
 // A fresh final scan observes every registered thread under the SAME continuous reservation.
 {native_source_stop::Session owner(stop);assert(owner.Begin(&self,sizeof(self)));assert(scanner.Scan(owner,&self,cache,&jit_cache,storage).accepted);assert(owner.ContinueSource()&&gc&&!exclusive&&stops==resumes+1);disable_proxy(&heap,&self);Native(&self);unsigned char carrier;stop_proxy(&carrier,s::kFinalCause,true);assert(scanner.Scan(owner,&self,cache,&jit_cache,storage).accepted);resume_proxy(&carrier);assert(!owner.Completed());Runnable(&self);enable_proxy(&heap,&self);assert(owner.Completed());}
 assert(!gc&&!exclusive&&!list_locked&&stops==resumes);std::cout<<"actual full enumeration/walker, foreign/empty/marked-inline maps, refusal/rollback/continuous final scan PASS\n";
}
