#include "platform/common/native_call_graph_super.h"
#include <cassert>
#include <iostream>
#include <optional>
#include <sys/mman.h>
#include <unistd.h>
#include "platform/common/readable_mapping_scope.h"
using namespace artdcevm;
namespace {
int referenced,declaring,selected,interface_calls;
void* InterfaceOriginal(void*klass,void*method,abi::arm64::android17::MethodPointerSize width){
 assert(klass==&referenced&&klass!=&declaring&&method==&declaring&&width==abi::arm64::android17::MethodPointerSize::k64);
 ++interface_calls;return &selected;
}
template<class Method>void Source(const method_metadata::Layout&layout){
 Method method{};method.access_flags=1;method.dex_method_index=19;method.method_index=2;
 method_metadata::Identity identity{};uint16_t slot=0;
 assert(native_call_graph_super::detail::Invokable(layout,&method,sizeof(method),true,&identity));
 assert(native_call_graph_dispatch::detail::MethodSlot(layout,&method,sizeof(method),&slot)&&slot==2);
 // Distinct actual superclass and receiver tables. Super dispatch must use the
 // caller's superclass at the resolved method index, never virtual receiver.
 alignas(8) uint64_t parent[]{0,uintptr_t(&referenced),uintptr_t(&declaring),uintptr_t(&selected)};
 alignas(8) uint64_t receiver[]{0,uintptr_t(&selected),uintptr_t(&selected),uintptr_t(&referenced)};
 void*target=nullptr;
 assert(native_call_graph_dispatch::detail::Slot(parent,sizeof(parent),8,3,slot,&target)&&target==&selected);
 assert(native_call_graph_dispatch::detail::Slot(receiver,sizeof(receiver),8,3,slot,&target)&&target==&referenced);
 assert(!native_call_graph_dispatch::detail::Slot(parent,sizeof(parent)-1,8,3,slot,&target));
 assert(!native_call_graph_dispatch::detail::Slot(parent,sizeof(parent),8,3,3,&target));
 assert(!native_call_graph_dispatch::detail::MethodSlot(layout,&method,sizeof(method)-1,&slot));
 method.access_flags=0x401;assert(native_call_graph_super::detail::Invokable(layout,&method,sizeof(method),true,&identity));
 assert(!native_call_graph_super::detail::Invokable(layout,&method,sizeof(method),false,&identity));
 for(uint32_t flags:{0x9u,0x3u,0x10001u,0x40001u}){method.access_flags=flags;assert(!native_call_graph_super::detail::Invokable(layout,&method,sizeof(method),true,&identity));}
 method.access_flags=0x101;assert(native_call_graph_super::detail::Invokable(layout,&method,sizeof(method),false,&identity)); // Native target is a graph boundary.
 method.dex_method_index=UINT32_MAX;assert(!native_call_graph_super::detail::Invokable(layout,&method,sizeof(method),true,&identity));
}
}
int main(){
 // Actual OS mappings can change across the original resolver's safepoint.
 // A pre-call view is not reusable; only a newly captured immediate window
 // rejects the now-inaccessible source span (without dereferencing it).
 const size_t page=static_cast<size_t>(sysconf(_SC_PAGESIZE));
 void*region=mmap(nullptr,page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
 assert(region!=MAP_FAILED);
 std::optional<method_identity::ScopedReadableMappings> window;
 window.emplace();size_t extent=0;
 assert(window->Ready()&&window->Extent(region,&extent)&&extent>=page);
 window.reset();
 assert(mprotect(region,page,PROT_NONE)==0);
 window.emplace();assert(window->Ready()&&!window->Extent(region,&extent));
 window.reset();assert(mprotect(region,page,PROT_READ|PROT_WRITE)==0);
 window.emplace();assert(window->Ready()&&window->Extent(region,&extent));
 window.reset();assert(munmap(region,page)==0);

 Source<method_metadata::Method11>(method_metadata::kAndroid11);
 Source<method_metadata::Method12>(method_metadata::kAndroid12Onward);
 assert(native_call_graph_super::detail::InterfaceTarget(InterfaceOriginal,&referenced,&declaring)==&selected&&interface_calls==1);
 assert(!native_call_graph_super::detail::InterfaceTarget(nullptr,&referenced,&declaring));
 assert(!native_call_graph_super::detail::InterfaceTarget(InterfaceOriginal,nullptr,&declaring)&&interface_calls==1);
 std::cout<<"Super source mechanics: Method11/12 slot, parent rather than receiver, referenced-interface rather than declaring Class, original 64-bit PointerSize ABI, abstract/obsolete/private/static/runtime rejection; no census/owner permission simulated\n";
}
