#include "platform/common/native_exception_upcall_boundary.h"
#include "platform/common/native_exception_entry_ownership.h"
#include <cassert>
#include <type_traits>
using namespace artdcevm;
static_assert(!std::is_default_constructible_v<native_exception_upcall_boundary::Proof>);
static_assert(!std::is_copy_constructible_v<native_exception_upcall_boundary::Proof>);
static_assert(!std::is_move_constructible_v<native_exception_upcall_boundary::Proof>);
static_assert(!std::is_copy_constructible_v<native_exception_upcall_boundary::Service::CaptureStorage>);
static_assert(!std::is_default_constructible_v<native_exception_entry_ownership::Proof>);
static_assert(!std::is_copy_constructible_v<native_exception_entry_ownership::Proof>);
static_assert(!std::is_move_constructible_v<native_exception_entry_ownership::Proof>);
static_assert(!std::is_default_constructible_v<native_exception_entry_ownership::Service>);
namespace {
struct Region {uintptr_t begin;size_t size;bool valid=true;};
bool Extent(void*raw,const void*p,size_t*out){auto&r=*static_cast<Region*>(raw);auto a=uintptr_t(p);if(!r.valid||a<r.begin||a-r.begin>=r.size)return false;*out=r.size-(a-r.begin);return true;}
}
int main(){
 using Cursor=native_managed_stack_stream::Cursor;using Frame=native_call_graph_stack::Frame;
 alignas(16) uintptr_t words[10]{};words[0]=0x3450;words[4]=0x1004;
 Region region{uintptr_t(words),sizeof(words)};modern_field_metadata::ReadContext read{&region,Extent};
 managed_stack_source::Snapshot head;head.tagged_quick=uintptr_t(words);
 Frame frame;frame.kind=native_call_graph_stack::FrameKind::Managed;
 frame.method=frame.carrier_method=reinterpret_cast<void*>(words[0]);frame.quick=words;
 frame.physical_boundary=true;frame.geometry.size=64;frame.return_slot=words+4;frame.return_pc=words[4];
 Frame terminal;terminal.quick=words+8;terminal.pc=words[4];
 // The actual named ReturnPcAddr supplies the slot. The observer must not
 // substitute an inferred frame_size-minus-word offset for that source fact.
 Cursor cursor;assert(cursor.Begin(head)&&cursor.Consume(frame,read)&&!cursor.Complete());
 assert(cursor.Consume(terminal,read)&&cursor.Complete());
 words[4]^=16;assert(cursor.Begin(head)&&!cursor.Consume(frame,read));words[4]^=16;
 frame.return_slot=words;assert(cursor.Begin(head)&&!cursor.Consume(frame,read));frame.return_slot=words+4;
 frame.return_slot=words+8;assert(cursor.Begin(head)&&!cursor.Consume(frame,read));frame.return_slot=words+4;
 region.size=32;assert(cursor.Begin(head)&&!cursor.Consume(frame,read));region.size=sizeof(words);
 region.valid=false;assert(cursor.Begin(head)&&!cursor.Consume(frame,read));region.valid=true;
 frame.inline_depth=1;assert(cursor.Begin(head)&&!cursor.Consume(frame,read));frame.inline_depth=0;
 head.tagged_quick+=8;assert(!cursor.Begin(head));
 native_exception_upcall_boundary::Service::CaptureStorage storage;
 (void)storage;
}
