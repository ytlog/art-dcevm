#include "device_profile_fixture.h"
#include "platform/android13/frame_recovery.h"
#include "platform/abi/arm64/android13/art_method_abi.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <vector>
#include <sys/mman.h>
using namespace artdcevm::android13;
namespace {
stack_abi::CodeInfo decoded{};
uint32_t map_bits[8]{},inline_bits[12]{};
uint32_t header[16]{0x40000000};
bool is_nterp=false,valid_map=true,jit_owned=true,aot_owned=false,aot_debug=true;int thread;
validation_abi::ClassPrefix klass{};validation_abi::DexMappingPrefix dex{};
validation_abi::DexCachePrefix* cache;
std::vector<unsigned char> bytes(512);
art_method_abi::ArtMethod outer{},middle{},inner{};
struct Frame {void* method;uint32_t inline_begin,inline_end;void** quick;uintptr_t pc;};
void* anchor;std::vector<Frame> frames;
uintptr_t Pc(){return reinterpret_cast<uintptr_t>(header)+4+32;}
void Put(size_t at,uint16_t n){std::memcpy(bytes.data()+at,&n,2);}
void Decode(stack_abi::CodeInfo* info,const void* h){assert(h==header);*info=decoded;}
stack_abi::TableIterator Map(const stack_abi::CodeInfo* info,uintptr_t pc,int isa){assert(pc==32&&isa==2);return {const_cast<stack_abi::BitTable<8>*>(&info->stack_maps),valid_map?0u:99u};}
bool Nterp(const void* h){assert(h==header);return is_nterp;}
bool Contains(const void*,const void*){return jit_owned;}
const void* OatCode(void*,size_t){return aot_owned?reinterpret_cast<unsigned char*>(header)+4:nullptr;}
bool Debuggable(const void*){return aot_debug;}
void* Root(void*){return &klass;}void* Obsolete(void*){return cache;}
void Construct(void* raw,void* t,void* c,stack_abi::WalkKind kind,bool check){assert(check&&!c);auto* v=static_cast<stack_abi::Visitor*>(raw);std::memset(static_cast<unsigned char*>(raw)+8,0,sizeof(*v)-8);v->thread=t;v->kind=kind;}
void Walk(void* raw,bool transitions){assert(!transitions);auto* v=static_cast<stack_abi::Visitor*>(raw);assert(v->thread==&thread&&v->kind==stack_abi::WalkKind::kIncludeInlined);for(size_t i=0;i<frames.size();++i){auto& f=frames[i];v->depth=i;v->quick_frame=f.quick;v->header=header;v->pc=f.pc;v->inline_frames.begin.row=f.inline_begin;v->inline_frames.end.row=f.inline_end;if(!v->VisitFrame())break;}}
void* Method(const void* v){return frames.at(static_cast<const stack_abi::Visitor*>(v)->depth).method;}
class Hooks final:public artdcevm::HookEngine {public:bool missing=false;bool Initialize(std::string*)override{return true;}void* FindSymbol(const char*,const char* name)override{if(missing)return nullptr;
#define FN(part,fn) if(std::strstr(name,part))return reinterpret_cast<void*>(fn)
 FN("ContainsPc",Contains);FN("GetOatMethodQuickCode",OatCode);FN("IsDebuggable",Debuggable);FN("CodeInfoC2",Decode);FN("GetStackMapForNativePcOffset",Map);FN("IsNterpMethodHeader",Nterp);FN("artReadBarrierForRootSlow",Root);FN("GetObsoleteDexCache",Obsolete);FN("StackVisitorC2",Construct);FN("WalkStack",Walk);FN("GetMethodEv",Method);
#undef FN
return nullptr;}void* Install(void*,void*,void**,std::string*)override{return nullptr;}bool Remove(void*,std::string*)override{return true;}};
void Reset(){anchor=&outer;jit_owned=true;aot_owned=false;aot_debug=true;is_nterp=false;valid_map=true;header[0]=0x9ca233cc;decoded={};decoded.dex_regs=4;decoded.stack_maps.region={map_bits,0,sizeof(map_bits)*8};decoded.stack_maps.rows=1;for(unsigned i=0;i<=8;++i)decoded.stack_maps.columns[i]=i*32;std::memset(map_bits,0,sizeof(map_bits));map_bits[5]=1;map_bits[7]=1;
 decoded.inline_infos.region={inline_bits,0,sizeof(inline_bits)*8};decoded.inline_infos.rows=2;for(unsigned i=0;i<=6;++i)decoded.inline_infos.columns[i]=i*32;std::memset(inline_bits,0,sizeof(inline_bits));inline_bits[5]=8;inline_bits[11]=10;
 std::memcpy(bytes.data(),"dex\n035",8);dex={nullptr,bytes.data(),bytes.size(),bytes.data()+128,bytes.size()-128};cache->dex_file=reinterpret_cast<uint64_t>(&dex);klass.dex_cache=uint32_t(reinterpret_cast<uintptr_t>(cache));
 outer={};middle={};inner={};outer.dex_method_index=middle.dex_method_index=inner.dex_method_index=1;outer.data=reinterpret_cast<uintptr_t>(bytes.data()+160);middle.data=reinterpret_cast<uintptr_t>(bytes.data()+192);inner.data=reinterpret_cast<uintptr_t>(bytes.data()+224);Put(160,4);Put(192,3);Put(224,2);
 frames={{&inner,0,2,&anchor,Pc()},{&middle,0,1,&anchor,Pc()},{&outer,0,0,&anchor,Pc()}};
}
}
namespace artdcevm::android13 {void* RuntimeJitCodeCache(){return &thread;}}
int main(){Hooks hooks;std::string error;assert(ConfigureStackAccess(hooks,MatchedProfileForTest(),&error));assert(!ConfigureFrameRecovery(hooks,DeviceProfile{},&error));hooks.missing=true;assert(!ConfigureFrameRecovery(hooks,MatchedProfileForTest(),&error));hooks.missing=false;assert(ConfigureFrameRecovery(hooks,MatchedProfileForTest(),&error));
 void* low=mmap(reinterpret_cast<void*>(0x24000000),4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON|MAP_FIXED,-1,0);assert(low==reinterpret_cast<void*>(0x24000000));cache=new(low)validation_abi::DexCachePrefix{};
 Reset();assert(IsEvolutionPcRecoverable(&thread,Pc()));jit_owned=false;assert(!IsEvolutionPcRecoverable(&thread,Pc()));Reset();assert(IsEvolutionPcRecoverable(&thread,Pc()));assert(!IsEvolutionPcRecoverable(nullptr,Pc()));assert(!IsEvolutionPcRecoverable(&thread,Pc()+4));
 // AOT is recognized only by exact named quick-code/header identity, and
 // remains subject to its immutable OatFile debuggable policy.
 stack_abi::DexPrefix aot_dex{};aot_dex.mapping=dex;stack_abi::OatDexPrefix oat_link{&thread};aot_dex.oat_dex=&oat_link;
 cache->dex_file=reinterpret_cast<uint64_t>(&aot_dex);jit_owned=false;aot_owned=true;
 assert(IsEvolutionPcRecoverable(&thread,Pc()));aot_debug=false;assert(!IsEvolutionPcRecoverable(&thread,Pc()));aot_debug=true;
 aot_owned=false;assert(!IsEvolutionPcRecoverable(&thread,Pc()));Reset();
 map_bits[7]=0;assert(!IsEvolutionPcRecoverable(&thread,Pc()));map_bits[7]=1;
 inline_bits[11]=9;assert(!IsEvolutionPcRecoverable(&thread,Pc()));inline_bits[11]=10;
 inline_bits[5]=7;assert(!IsEvolutionPcRecoverable(&thread,Pc()));inline_bits[5]=8;
 decoded.dex_regs=5;assert(!IsEvolutionPcRecoverable(&thread,Pc()));decoded.dex_regs=4;
 valid_map=false;assert(!IsEvolutionPcRecoverable(&thread,Pc()));valid_map=true;
 decoded.stack_maps.region.bit_size=1;assert(!IsEvolutionPcRecoverable(&thread,Pc()));Reset();
 middle.data=reinterpret_cast<uintptr_t>(bytes.data()+bytes.size()-2);assert(!IsEvolutionPcRecoverable(&thread,Pc()));Reset();
 // A JVMTI standard code item can retain a CompactDex owning file.
 std::memcpy(bytes.data(),"cdex001",8);assert(IsEvolutionPcRecoverable(&thread,Pc()));Reset();
 // Separate/shared CDEX data and backwards preheader: locals + incoming registers.
 outer.data|=1;middle.data|=1;inner.data|=1;
 std::memcpy(bytes.data(),"cdex001",8);Put(160,0x4000);Put(162,0);Put(192,0x2100);Put(194,0);Put(224,0x2000);Put(226,0);
 assert(IsEvolutionPcRecoverable(&thread,Pc()));
 frames={{&middle,0,1,&anchor,Pc()},{&outer,0,0,&anchor,Pc()}};Put(192,0x0100);Put(194,19);Put(190,0);Put(188,0);Put(186,18);Put(184,5);inline_bits[5]=29;assert(IsEvolutionPcRecoverable(&thread,Pc()));inline_bits[5]=28;assert(!IsEvolutionPcRecoverable(&thread,Pc()));
 Reset();middle.access_flags=0x40000;klass.dex_cache=0;assert(!IsEvolutionPcRecoverable(&thread,Pc())); // other frames still need their current cache.
 Reset();outer.access_flags=middle.access_flags=inner.access_flags=0x40000;klass.dex_cache=0;assert(IsEvolutionPcRecoverable(&thread,Pc()));
 Reset();map_bits[7]=0;is_nterp=true;assert(IsEvolutionPcRecoverable(&thread,Pc()));is_nterp=false;assert(!IsEvolutionPcRecoverable(&thread,Pc()));
 munmap(low,4096);std::puts("frame recovery: actual PC, all inline/outer counts, missing maps, shared standard/CDEX and Nterp passed");}
