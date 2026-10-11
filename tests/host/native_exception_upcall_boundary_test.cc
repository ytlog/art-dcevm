#include "platform/common/native_exception_upcall_boundary.h"
#include "platform/common/native_exception_entry_ownership.h"
#include <cassert>
#include <type_traits>
using namespace artdcevm;
static_assert(!std::is_default_constructible_v<native_exception_upcall_boundary::Proof>);
static_assert(!std::is_copy_constructible_v<native_exception_upcall_boundary::Proof>);
static_assert(!std::is_default_constructible_v<native_exception_entry_ownership::Proof>);
static_assert(!std::is_copy_constructible_v<native_exception_entry_ownership::Proof>);
static_assert(!std::is_move_constructible_v<native_exception_entry_ownership::Proof>);
static_assert(!std::is_default_constructible_v<native_exception_entry_ownership::Service>);
namespace {
struct Region {uintptr_t begin;size_t size;bool valid=true;};
bool Extent(void*raw,const void*p,size_t*out){auto&r=*static_cast<Region*>(raw);auto a=uintptr_t(p);if(!r.valid||a<r.begin||a-r.begin>=r.size)return false;*out=r.size-(a-r.begin);return true;}
}
int main(){
 alignas(16) uintptr_t frame[8]{};frame[7]=0x1004;Region region{uintptr_t(frame),sizeof(frame)};
 modern_field_metadata::ReadContext read{&region,Extent};uintptr_t pc=42;
 auto check=[&](uintptr_t base,size_t size,size_t offset){return native_exception_upcall_boundary::detail::SavedReturn(read,base,size,offset,&pc);};
 assert(check(uintptr_t(frame),64,56)&&pc==0x1004);
 pc=42;assert(!check(uintptr_t(frame),64,48)&&pc==42);
 assert(!check(uintptr_t(frame)+8,64,56));assert(!check(uintptr_t(frame),63,55));
 assert(!check(UINTPTR_MAX-15,32,24));assert(!check(uintptr_t(frame),80,72));
 frame[7]=0;assert(!check(uintptr_t(frame),64,56));frame[7]=0x1004;
 region.valid=false;assert(!check(uintptr_t(frame),64,56));
}
