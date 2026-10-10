#include "platform/common/native_callee_edge.h"
#include "platform/common/native_interface_dispatch.h"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
using namespace artdcevm;
int main(){
 alignas(8) std::array<unsigned char,48> array{};
 int32_t count=3;std::memcpy(array.data()+8,&count,4);
 for(uint32_t i=0;i<3;++i){uint64_t value=0x1000+8*i;std::memcpy(array.data()+16+8*i,&value,8);}
 void*out=reinterpret_cast<void*>(0xdead);
 for(uint32_t i=0;i<3;++i){assert(native_interface_dispatch::ReadPointerSlot(array.data(),40,i,&out));assert(uintptr_t(out)==0x1000+8*i);}
 out=reinterpret_cast<void*>(0xdead);
 assert(!native_interface_dispatch::ReadPointerSlot(array.data(),39,0,&out)&&uintptr_t(out)==0xdead);
 assert(!native_interface_dispatch::ReadPointerSlot(array.data(),40,3,&out));
 assert(!native_interface_dispatch::ReadPointerSlot(array.data(),15,0,&out));
 assert(!native_interface_dispatch::ReadPointerSlot(array.data()+1,39,0,&out));
 assert(!native_interface_dispatch::ReadPointerSlot(nullptr,40,0,&out));
 assert(!native_interface_dispatch::ReadPointerSlot(array.data(),SIZE_MAX,0,&out));
 count=-1;std::memcpy(array.data()+8,&count,4);assert(!native_interface_dispatch::ReadPointerSlot(array.data(),40,0,&out));
 count=INT32_MAX;std::memcpy(array.data()+8,&count,4);assert(!native_interface_dispatch::ReadPointerSlot(array.data(),40,0,&out));
 count=0;std::memcpy(array.data()+8,&count,4);assert(!native_interface_dispatch::ReadPointerSlot(array.data(),40,0,&out));
 count=3;std::memcpy(array.data()+8,&count,4);uint64_t zero=0;std::memcpy(array.data()+24,&zero,8);assert(!native_interface_dispatch::ReadPointerSlot(array.data(),40,1,&out));
 using namespace native_callee_edge;
 Adjacent walk;int thread=0,caller_method=0,callee_method=0;void*stack[16]{};
 Frame caller{&thread,&caller_method,nullptr,stack+8,0x2000,Kind::ManagedPhysical};
 Frame callee{&thread,&callee_method,reinterpret_cast<void*>(0x4000),stack,0x3000,Kind::ManagedPhysical},edge{};
 assert(!walk.Younger(caller,&edge));walk.Observe(callee);assert(walk.Younger(caller,&edge)&&edge.method==&callee_method&&edge.receiver==callee.receiver);
 for(auto kind:{Kind::Inline,Kind::Runtime,Kind::Transition,Kind::Unknown}){auto boundary=callee;boundary.kind=kind;walk.Observe(boundary);assert(!walk.Younger(caller,&edge));}
 walk.Observe(callee);auto other=caller;other.thread=&callee_method;assert(!walk.Younger(other,&edge));
 other=caller;other.quick=stack;assert(!walk.Younger(other,&edge));other=caller;other.quick=nullptr;assert(!walk.Younger(other,&edge));
 other=caller;other.kind=Kind::NativePhysical;assert(!walk.Younger(other,&edge));
 auto native=callee;native.kind=Kind::NativePhysical;walk.Observe(native);assert(walk.Younger(caller,&edge));
 walk.Clear();assert(!walk.Younger(caller,&edge));
 std::cout<<"PointerArray exact extent/index/null/overflow refusal and original consecutive physical-frame adjacency with runtime/transition/inline refusal passed\n";
}
