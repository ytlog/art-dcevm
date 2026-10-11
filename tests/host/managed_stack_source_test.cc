#include "platform/common/managed_stack_source.h"
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <new>
static bool noalloc=false;
void*operator new(size_t n){assert(!noalloc);if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete(void*p,size_t)noexcept{std::free(p);}
using namespace artdcevm;
using Fragment=abi::arm64::android16::ManagedStack;
struct Memory {Fragment*first;size_t count;bool live=true;};
bool Extent(void*raw,const void*p,size_t*out){auto&m=*static_cast<Memory*>(raw);auto address=uintptr_t(p),base=uintptr_t(m.first);if(!m.live||address<base||address-base>=m.count*sizeof(Fragment))return false;*out=m.count*sizeof(Fragment)-(address-base);return true;}
int main(){
 std::array<Fragment,3> fragments{};Memory memory{fragments.data(),fragments.size()};modern_field_metadata::ReadContext read{&memory,Extent};
 fragments[0].link_=&fragments[1];fragments[1].link_=&fragments[2];
 fragments[1].tagged_top_quick_frame_.tagged_sp_=0x1001;fragments[2].top_shadow_frame_=reinterpret_cast<void*>(0x2000);
 noalloc=true;auto census=managed_stack_source::Count(read,fragments.data());assert(census.complete&&census.fragments==3&&census.terminal==&fragments[2]);
 managed_stack_source::Snapshot snapshot;assert(managed_stack_source::Read(read,&fragments[1],&snapshot)&&snapshot.tagged_quick==0x1001);
 fragments[1].tagged_top_quick_frame_.tagged_sp_=0x1002;assert(managed_stack_source::Read(read,&fragments[1],&snapshot));
 fragments[1].tagged_top_quick_frame_.tagged_sp_=0x1003;assert(!managed_stack_source::Count(read,fragments.data()).complete);
 fragments[1].tagged_top_quick_frame_.tagged_sp_=1;assert(!managed_stack_source::Count(read,fragments.data()).complete);
 fragments[1].tagged_top_quick_frame_.tagged_sp_=0x1008;assert(!managed_stack_source::Count(read,fragments.data()).complete);
 fragments[1].tagged_top_quick_frame_.tagged_sp_=0x1000;fragments[1].top_shadow_frame_=reinterpret_cast<void*>(0x2000);assert(!managed_stack_source::Count(read,fragments.data()).complete);fragments[1].top_shadow_frame_=nullptr;
 fragments[2].link_=&fragments[0];assert(!managed_stack_source::Count(read,fragments.data()).complete);
 fragments[2].link_=&fragments[2];assert(!managed_stack_source::Count(read,fragments.data()).complete);
 fragments[2].link_=reinterpret_cast<Fragment*>(uintptr_t(fragments.data())+1);assert(!managed_stack_source::Count(read,fragments.data()).complete);
 fragments[2].link_=reinterpret_cast<Fragment*>(UINTPTR_MAX-3);assert(!managed_stack_source::Count(read,fragments.data()).complete);
 fragments[2].link_=nullptr;memory.count=2;assert(!managed_stack_source::Count(read,fragments.data()).complete);memory.count=3;
 memory.live=false;assert(!managed_stack_source::Count(read,fragments.data()).complete);memory.live=true;
 assert(!managed_stack_source::Count(read,nullptr).complete);
 noalloc=false;puts("Fixed ManagedStack full empty/quick/generic-JNI/JIT-JNI/shadow chain census; cycles/dual-tags/null-tag/unaligned/truncated/expired refusal noalloc PASS; no native permission granted");
}
