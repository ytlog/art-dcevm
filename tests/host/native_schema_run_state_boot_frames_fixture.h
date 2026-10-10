#include <pthread.h>
// Actual whole scanner leaf composition. No stop, root or dependency authority
// is replaced here: the surrounding RunState owns those production services.
namespace {
void* boot_compiled_method=nullptr;size_t boot_frame_walks=0;
int boot_worker,boot_empty,boot_list_mutex;
void* boot_list_mutex_slot=&boot_list_mutex;
void* boot_runtime_slot=&runtime;
bool boot_frame_binding=false;
bool boot_list_locked=false,boot_bad_pc=false,boot_bad_value=false,boot_bad_mask=false,boot_bad_cursor=false;
alignas(8) uint32_t boot_header[2]{};
struct alignas(16) BootQuick {void* method=nullptr;uint32_t value=0,padding=0;std::array<unsigned char,48> tail{};} boot_quick;
static_assert(sizeof(BootQuick)==64);
std::array<uint32_t,8> boot_stack_map{{0,0,2,0,0,1,1,1}};
uint32_t boot_dex_mask=1,boot_dex_map=1;
std::array<uint32_t,2> boot_location{{1,3}};
void* boot_visit_method=nullptr;
struct BootContext:abi::arm64::context::Prefix {
 void Reset()override{}~BootContext()override=default;
 void FillCalleeSaves(uint8_t*,const abi::arm64::context::FrameInfo&)override{}
 void SetSP(uintptr_t)override{}void SetPC(uintptr_t)override{}void SetArg0(uintptr_t)override{}
 bool IsAccessibleGPR(uint32_t)override{return false;}
 uintptr_t* GetGPRAddress(uint32_t)override{return nullptr;}
 uintptr_t GetGPR(uint32_t)override{std::abort();}void SetGPR(uint32_t,uintptr_t)override{}
 uintptr_t GetFPR(uint32_t)override{std::abort();}
 bool IsAccessibleFPR(uint32_t)override{return false;}
};
abi::arm64::context::Prefix* BootCreateContext(){assert(!stopped);return new BootContext;}
void BootListLock(void* mutex,void*self){assert(mutex==&boot_list_mutex&&self==thread.data()&&gc&&stopped==1&&!boot_list_locked);boot_list_locked=true;}
void BootListUnlock(void* mutex,void*self){assert(mutex==&boot_list_mutex&&self==thread.data()&&gc&&stopped==1&&locked);boot_list_locked=false;}
void BootForEach(void*list,void(*visit)(void*,void*),void*data){assert(list==&thread_list&&locked);visit(thread.data(),data);visit(foreign_thread.data(),data);visit(&boot_empty,data);}
void BootConstructVisitor(void*raw,void*self,void*context,abi::arm64::stack::WalkKind kind,bool check){
 assert(gc&&stopped==1&&locked&&check&&kind==abi::arm64::stack::WalkKind::kIncludeInlined);
 auto*v=static_cast<abi::arm64::stack::Visitor13*>(raw);std::memset(static_cast<unsigned char*>(raw)+8,0,sizeof(*v)-8);v->thread=self;v->context=context;v->kind=kind;
}
void BootWalkVisitor(void*raw,bool transitions){
 assert(gc&&stopped==1&&locked&&transitions);auto*v=static_cast<abi::arm64::stack::Visitor13*>(raw);
 if(v->thread==thread.data()||v->thread==&boot_empty)return;
 assert(v->thread==foreign_thread.data()&&boot_compiled_method);boot_visit_method=boot_compiled_method;boot_quick.method=boot_compiled_method;
 v->quick_frame=reinterpret_cast<void**>(&boot_quick);v->header=boot_header;v->pc=uintptr_t(boot_header)+36;++boot_frame_walks;v->VisitFrame();
}
void* BootGetMethod(const void*){return boot_visit_method;}
uint32_t BootDexPc(const void*,bool crash){assert(!crash);return boot_bad_pc?0:1;}
bool BootFlag(const void*header){assert(header==boot_header);return false;}
bool BootContains(const void*cache,const void*){assert(cache==&jit_cache);return false;}
bool BootNterp(const void*header){assert(header==boot_header);return false;}
const void* BootMethodHeader(void*method,uintptr_t pc){return method==boot_compiled_method&&pc==uintptr_t(boot_header)+36?boot_header:nullptr;}
bool BootOriginalAsync(const void*r,void*method,uintptr_t pc){assert(r==&runtime&&method==boot_compiled_method&&pc==uintptr_t(boot_header)+36&&locked);return false;}
size_t BootOffset(const void*){return 32;}
template<size_t N>void BootTable(abi::arm64::android13::BitTable<N>&table,void*bits,size_t size,uint32_t rows){table.region={bits,0,size};table.rows=rows;for(size_t i=0;i<=N;++i)table.columns[i]=uint16_t(i*32);}
void BootCodeInfo(abi::arm64::android13::CodeInfo*info,const void*header){
 assert(header==boot_header);*info={};info->flags=1;info->frame_size=4;info->code_size=64;info->dex_regs=1;
 boot_stack_map[3]=boot_bad_mask?1:0;
 BootTable(info->stack_maps,boot_stack_map.data(),sizeof(boot_stack_map)*8,1);
 info->dex_masks.region={&boot_dex_mask,0,1};info->dex_masks.rows=1;info->dex_masks.columns[0]=0;info->dex_masks.columns[1]=1;
 BootTable(info->dex_maps,&boot_dex_map,32,1);BootTable(info->dex_catalog,boot_location.data(),64,1);
}
abi::arm64::android11::TableIterator BootFindMap(const abi::arm64::android13::CodeInfo*info,uintptr_t pc,int32_t isa){assert(pc==32&&isa==2);return {const_cast<void*>(static_cast<const void*>(&info->stack_maps)),0};}
void BootDecodeMap(const void*,uint32_t row,uint32_t first,abi::arm64::dex_register_map::Map*map){assert(row==0&&!first&&map->count==1);map->data()[0]={0,8};}
bool BootNativeValue(const void*raw,abi::arm64::android12::frame_registers::Location location,uint32_t*out){assert(location.kind==0&&location.value==8);*out=boot_quick.value+(boot_bad_value?1:0);if(boot_bad_cursor)++static_cast<abi::arm64::stack::Visitor13*>(const_cast<void*>(raw))->pc;return true;}
}
void CoverBootRootMaps(RunStateMappings&);
bool BootRefreshMaps(void*raw,std::string*error){if(!RunStateMappings::Refresh(raw,error))return false;auto&s=*static_cast<RunStateMappings*>(raw);s.Cover(boot_header);s.Cover(boot_stack_map.data());s.Cover(&boot_dex_mask);s.Cover(&boot_dex_map);s.Cover(boot_location.data());s.Cover(&boot_quick);CoverBootRootMaps(s);auto*cache=reinterpret_cast<void*>(uintptr_t(Read32(BootCreatedClass(),offsetof(class_metadata::Class17,dex_cache))));void*file=nullptr;std::memcpy(&file,static_cast<char*>(cache)+16,8);s.Cover(static_cast<dex_mapping_abi::Prefix*>(file)->begin);return true;}
void* BootResolve(void*,void*,uint32_t,void*,uint32_t){std::abort();}
bool BootSameSignature(void*a,void*b){
 if(a!=BootCreatedMethod()&&b!=BootCreatedMethod())return SameIndex(a,b);
 if(a==b)return true;void*other=a==BootCreatedMethod()?b:a;void*klass=Root(other);assert(klass);
 auto*cache=reinterpret_cast<void*>(uintptr_t(Read32(klass,offsetof(class_metadata::Class17,dex_cache))));void*file=nullptr;std::memcpy(&file,static_cast<char*>(cache)+16,8);assert(file);
 const auto&p=*static_cast<dex_mapping_abi::Prefix*>(file);evolution::MappedDexMetadata map{p.begin,p.size,p.data_begin,p.data_size,0};
 native_boot_aot_unaffected::DexIdentity identity(map);return identity.Matches(Read32(other,8),Public(klass).descriptor,"staticKeep","()I");
}
void* BootGuardMethod(void*p,jmethodID id){return BootIsMethod(id)?BootDecodeMethod(&linker,id):GuardMethod(p,id);}
bool BootGuardSame(void*p,void*a,void*b){return a==BootCreatedMethod()||b==BootCreatedMethod()?BootSameSignature(a,b):GuardSameMethod(p,a,b);}
#include "native_schema_run_state_frame_roots_fixture.h"
void CoverBootRootMaps(RunStateMappings&s){
#if defined(__APPLE__)
 const uintptr_t high=uintptr_t(pthread_get_stackaddr_np(pthread_self()));const size_t bytes=pthread_get_stacksize_np(pthread_self());assert(high>bytes);uintptr_t low=high-bytes;size_t readable=0;if(!method_identity::ReadableMappingExtent(reinterpret_cast<void*>(low),&readable))low+=uintptr_t(sysconf(_SC_PAGESIZE));s.Cover(reinterpret_cast<void*>(low));
#else
 pthread_attr_t attr;assert(!pthread_getattr_np(pthread_self(),&attr));void*low=nullptr;size_t bytes=0;assert(!pthread_attr_getstack(&attr,&low,&bytes)&&bytes);pthread_attr_destroy(&attr);s.Cover(low);
#endif
s.Cover(&boot_trace_slot);s.Cover(&boot_class_slot);s.Cover(&boot_trace_lock);s.Cover(&boot_class_lock);s.Cover(&boot_original_cursor);s.Cover(&boot_original_context);}
class BootHooks:public BootPublicHooks {
 public:void* Install(void*t,void*proxy,void**original,std::string*error)override{if(t==reinterpret_cast<void*>(BootFinish)){assert(stopped&&Read32(thread.data())==92u<<24);*original=t;owner_finish_proxy=reinterpret_cast<s::FinishGC>(proxy);return t;}if(t==reinterpret_cast<void*>(BootStart)){assert(stopped&&Read32(thread.data())==92u<<24);*original=t;owner_start_proxy=reinterpret_cast<s::StartGC>(proxy);return t;}return BootPublicHooks::Install(t,proxy,original,error);}
 public:void* FindSymbol(const char*lib,const char*name)override{
  if(auto*p=BootRootSymbol(name))return p;
#define BOOT_FN(symbol,fn) if(!std::strcmp(name,symbol))return reinterpret_cast<void*>(fn)
  if(std::strstr(name,"HasSameNameAndSignature"))return reinterpret_cast<void*>(BootSameSignature);
  if(!boot_frame_binding)return BootPublicHooks::FindSymbol(lib,name);
  BOOT_FN("_ZN3art11ClassLinker13ResolveMethodILNS0_11ResolveModeE1EEEPNS_9ArtMethodEPNS_6ThreadEjS4_NS_10InvokeTypeE",BootResolve);
  if(!std::strcmp(name,"_ZN3art5Locks17thread_list_lock_E"))return &boot_list_mutex_slot;
  if(!std::strcmp(name,native_frame_async_recovery::Service::kRuntime))return &boot_runtime_slot;
  BOOT_FN(native_frame_async_recovery::Service::kMethod,BootOriginalAsync);
  BOOT_FN(native_frame_code_owner::Service::kMethodHeader,BootMethodHeader);
  BOOT_FN("_ZN3art5Mutex13ExclusiveLockEPNS_6ThreadE",BootListLock);
  BOOT_FN("_ZN3art5Mutex15ExclusiveUnlockEPNS_6ThreadE",BootListUnlock);
  BOOT_FN("_ZN3art10ThreadList7ForEachEPFvPNS_6ThreadEPvES3_",BootForEach);
  BOOT_FN(abi::arm64::context::kCreate,BootCreateContext);
  BOOT_FN(abi::arm64::stack::kConstruct,BootConstructVisitor);
  BOOT_FN(abi::arm64::stack::kWalk,BootWalkVisitor);
  BOOT_FN(abi::arm64::stack::kMethod,BootGetMethod);
  BOOT_FN(abi::arm64::android13::kConstructCodeInfo,BootCodeInfo);
  BOOT_FN(abi::arm64::android13::kFindStackMap,BootFindMap);
  BOOT_FN(abi::arm64::dex_register_map::kDecode,BootDecodeMap);
  BOOT_FN(abi::arm64::android12::frame_registers::kFast,BootNativeValue);
  BOOT_FN(abi::arm64::android14::frame_registers::kNativePcOffset,BootOffset);
  BOOT_FN("_ZNK3art12StackVisitor8GetDexPcEb",BootDexPc);
  BOOT_FN("_ZNK3art20OatQuickMethodHeader23HasShouldDeoptimizeFlagEv",BootFlag);
  BOOT_FN("_ZNK3art3jit12JitCodeCache10ContainsPcEPKv",BootContains);
  BOOT_FN("_ZNK3art20OatQuickMethodHeader19IsNterpMethodHeaderEv",BootNterp);
#undef BOOT_FN
  return BootPublicHooks::FindSymbol(lib,name);
 }
};
