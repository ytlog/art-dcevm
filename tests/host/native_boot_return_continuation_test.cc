#include "platform/common/native_boot_return_continuation.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <array>
static bool noalloc=false;
void* operator new(size_t n){assert(!noalloc);if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete(void*p,size_t)noexcept{std::free(p);}
using namespace artdcevm::native_boot_return_continuation;
struct Memory {const unsigned char*data;size_t size;bool live=true;};
bool Extent(void*owner,const void*pointer,size_t*out){const auto&m=*static_cast<Memory*>(owner);const auto p=uintptr_t(pointer),base=uintptr_t(m.data);if(!m.live||p<base||p-base>=m.size)return false;*out=m.size-(p-base);return true;}
template<class T>void Put(unsigned char*memory,size_t offset,T value){std::memcpy(memory+offset,&value,sizeof(value));}
int main(){
 unsigned char boundaries[1024]{},seen[1024]{};uint32_t queue[1024]{};Scratch scratch{boundaries,seen,queue,1024};
 const uint16_t simple[]{0x0071,0,0,0x000e};
 noalloc=true;auto r=Inspect({simple,4,nullptr,0},0,scratch);assert(r.complete&&r.primitive_return&&r.exception_escape&&r.reachable==1&&!r.backedge);
 const uint16_t caught[]{0x0071,0,0,0x000e,0x000d,0x000e};
 const Catch typed[]{ {0,4} },all[]{ {UINT32_MAX,4} };
 const Try tt[]{ {0,3,typed,1,false} },ta[]{ {0,3,all,1,true} };
 r=Inspect({caught,6,tt,1},0,scratch);assert(!r.complete&&r.exception_escape);
 r=Inspect({caught,6,ta,1},0,scratch);assert(!r.complete&&!r.exception_escape);
 const Try invalidall[]{ {0,3,typed,1,true} };
 r=Inspect({caught,6,invalidall,1},0,scratch);assert(!r.complete);
 const uint16_t swallowed[]{0x0071,0,0,0x000e,0x000e};
 r=Inspect({swallowed,5,ta,1},0,scratch);assert(r.complete&&r.primitive_return&&!r.exception_escape);
 const uint16_t loop[]{0x0071,0,0,0xfd28};
 r=Inspect({loop,4,nullptr,0},0,scratch);assert(!r.complete&&r.backedge);
 const uint16_t unknown[]{0x0071,0,0,0x00e3};assert(!Inspect({unknown,4,nullptr,0},0,scratch).complete);
 const uint16_t operand_branch[]{0x0071,0,0,0xff28};assert(!Inspect({operand_branch,4,nullptr,0},0,scratch).complete);
 assert(!Inspect({simple,4,nullptr,0},1,scratch).complete);
 assert(!Inspect({simple,4,nullptr,0},0,{boundaries,seen,queue,3}).complete);

 r=Inspect({simple,4,nullptr,0},0,scratch,Entry::ExceptionalOnly);assert(r.complete&&r.exception_escape&&r.primitive_return&&r.reachable==0);
 r=Inspect({swallowed,5,ta,1},0,scratch,Entry::ExceptionalOnly);assert(r.complete&&!r.exception_escape&&r.primitive_return&&r.reachable==1);
 const uint16_t monitor[]{0x001d,0x0071,0,0,0x000e};assert(!Inspect({monitor,5,nullptr,0},1,scratch).complete);
 noalloc=false;
 // Exact bounded Standard CodeItem/DEX proto reader, not a synthetic body
 // label or invocation name. Every read stays in this source data owner.
 std::array<unsigned char,512>dex{};std::memcpy(dex.data(),"dex\n039\0",8);
 Put<uint32_t>(dex.data(),36,112);Put<uint32_t>(dex.data(),40,0x12345678);
 Put<uint32_t>(dex.data(),56,1);Put<uint32_t>(dex.data(),60,112);
 Put<uint32_t>(dex.data(),64,1);Put<uint32_t>(dex.data(),68,148);
 Put<uint32_t>(dex.data(),72,1);Put<uint32_t>(dex.data(),76,136);
 Put<uint32_t>(dex.data(),88,1);Put<uint32_t>(dex.data(),92,128);
 Put<uint32_t>(dex.data(),112,164);dex[164]=1;dex[165]='V';
 Put<uint32_t>(dex.data(),140,0);Put<uint32_t>(dex.data(),148,0);
 std::memcpy(dex.data()+208,simple,sizeof(simple));
 artdcevm::receiver_guard::CodeView source{reinterpret_cast<const uint16_t*>(dex.data()+208),4,1,1,0,{dex.data(),dex.size(),dex.data(),dex.size(),0},dex.data()+192,0,0,false};
 Memory memory{dex.data(),dex.size()};artdcevm::modern_field_metadata::ReadContext context{&memory,Extent};
 Try parsed[4]{};Catch parsed_handlers[8]{};HandlerStorage hs{parsed,4,parsed_handlers,8};
 noalloc=true;r=Read(source,0,context,hs,scratch);assert(r.complete&&r.exception_escape);
 auto capacity=CountStorage(source,context);assert(capacity.complete&&capacity.units==4&&capacity.tries==0&&capacity.handlers==0);
 dex[165]='I';assert(!Read(source,0,context,hs,scratch).complete);dex[165]='V';
 memory.live=false;assert(!Read(source,0,context,hs,scratch).complete);memory.live=true;
 source.tries_size=1;source.units=6;std::memcpy(dex.data()+208,caught,sizeof(caught));
 Put<uint32_t>(dex.data(),220,0);Put<uint16_t>(dex.data(),224,3);Put<uint16_t>(dex.data(),226,1);
 dex[228]=1;dex[229]=1;dex[230]=0;dex[231]=4;
 r=Read(source,0,context,hs,scratch);assert(!r.complete&&r.exception_escape);
 capacity=CountStorage(source,context);assert(capacity.complete&&capacity.tries==1&&capacity.handlers==1);
 Put<uint16_t>(dex.data(),226,2);assert(!Read(source,0,context,hs,scratch).complete);
 Put<uint16_t>(dex.data(),226,1);dex[229]=0;dex[230]=4;
 r=Read(source,0,context,hs,scratch);assert(!r.complete&&!r.exception_escape);
 assert(!Read(source,0,context,{parsed,0,parsed_handlers,8},scratch).complete);
 assert(!Read(source,0,context,{parsed,4,parsed_handlers,0},scratch).complete);
 // Complete source handler census includes unused encoded entries; the count
 // can exceed instruction units, so no units-based capacity is assumed safe.
 dex[228]=8;for(unsigned i=0;i<8;++i){dex[229+i*3]=1;dex[230+i*3]=0;dex[231+i*3]=4;}
 capacity=CountStorage(source,context);assert(capacity.complete&&capacity.handlers==8&&capacity.units==6);
 assert(!Read(source,0,context,{parsed,4,parsed_handlers,6},scratch).complete);
 memory.live=false;assert(!CountStorage(source,context).complete);memory.live=true;
 // Compact CodeItems and data owners may begin at two-byte alignment. The
 // TryItems pointer aligns the absolute instruction end, not a base-relative
 // instruction offset. Both census and decoding use the exact same geometry.
 source.compact=true;source.code_item=dex.data()+194;source.instructions=reinterpret_cast<const uint16_t*>(dex.data()+198);source.source_dex.data_begin=dex.data()+2;source.source_dex.data_size=dex.size()-2;
 std::memcpy(dex.data(),"cdex001\0",8);Put<uint32_t>(dex.data(),36,136);
 Put<uint32_t>(dex.data(),60,160);Put<uint32_t>(dex.data(),68,156);Put<uint32_t>(dex.data(),76,144);Put<uint32_t>(dex.data(),92,136);
 Put<uint64_t>(dex.data(),136,0);Put<uint32_t>(dex.data(),148,0);Put<uint32_t>(dex.data(),156,0);Put<uint32_t>(dex.data(),160,162);
 std::memcpy(dex.data()+198,swallowed,sizeof(swallowed));source.units=5;
 Put<uint32_t>(dex.data(),208,0);Put<uint16_t>(dex.data(),212,3);Put<uint16_t>(dex.data(),214,1);dex[216]=1;dex[217]=0;dex[218]=4;
 capacity=CountStorage(source,context);assert(capacity.complete&&capacity.handlers==1);
 r=Read(source,0,context,hs,scratch);assert(r.complete&&!r.exception_escape);
 noalloc=false;puts("Noalloc bounded continuation shape: normal return, typed/catchall/escape, prefix loopback, missing capacity, unknown/operand PC, whole-body monitor PASS; no native permission granted");
}
