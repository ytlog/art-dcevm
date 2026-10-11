#pragma once
#include "platform/common/compiled_frame_dex_oracle.h"
#include "platform/common/native_receiver_bytecode.h"
#include "platform/common/native_boot_dex_identity.h"
#include "platform/common/modern_field_metadata.h"
#include <cstddef>
#include <cstdint>
namespace artdcevm::native_boot_return_continuation {
// Readonly structural evidence only. The actual source CodeItem owner must
// provide bounded immutable instructions and complete handlers. This helper
// grants no stop, callee, GC-root, dependency, or reconstruction authority.
struct Catch {uint32_t type,pc;};
struct Try {uint32_t first,count;const Catch*handlers;size_t handler_count;bool catch_all;};
struct Body {const uint16_t*units;uint32_t count;const Try*tries;size_t try_count;uint32_t registers=0;};
struct Scratch {unsigned char*boundaries;unsigned char*seen;uint32_t*queue;size_t capacity;};
struct Shape {bool complete=false,primitive_return=false,exception_escape=false,backedge=false;size_t reachable=0,caught_entries=0;const char*failure="not-read";};
enum class Entry {NormalAndExceptional,ExceptionalOnly};
inline int32_t S16(uint16_t x){return static_cast<int16_t>(x);}
inline int32_t S32(uint16_t lo,uint16_t hi){return static_cast<int32_t>(uint32_t(lo)|(uint32_t(hi)<<16));}
inline bool OrdinaryInvoke(unsigned op){return (op>=0x6e&&op<=0x72)||(op>=0x74&&op<=0x78);}
inline Shape Inspect(const Body&b,uint32_t invoke,Scratch s,Entry entry=Entry::NormalAndExceptional){
 Shape r;auto deny=[&](const char*reason){r.failure=reason;return r;};
 if(!b.units||!b.count||!s.boundaries||!s.seen||!s.queue||s.capacity<b.count||(b.try_count&&!b.tries))return deny("storage");
 for(uint32_t i=0;i<b.count;++i)s.boundaries[i]=s.seen[i]=0;
 for(uint32_t pc=0;pc<b.count;){
  const auto word=b.units[pc];const auto op=word&255u;uint64_t w=artdcevm::compiled_frame_dex_oracle::detail::Width(op);
  if(op==0&&word){
   if(pc+2>b.count)return deny("payload-header");const auto kind=word>>8;
   if(kind==1)w=4ull+uint64_t(b.units[pc+1])*2;
   else if(kind==2)w=2ull+uint64_t(b.units[pc+1])*4;
   else if(kind==3){if(pc+4>b.count)return deny("array-payload-header");w=4+(uint64_t(b.units[pc+1])*(uint32_t(b.units[pc+2])|(uint32_t(b.units[pc+3])<<16))+1)/2;}
   else return deny("payload-kind");
  }else {s.boundaries[pc]=1;if(op==0x1d||op==0x1e)return deny("whole-body-monitor-state-unproved");}
  if(!w||w>b.count-pc)return deny("instruction-width");pc+=static_cast<uint32_t>(w);
 }
 if(invoke>=b.count||!s.boundaries[invoke])return deny("invoke-boundary");
 const auto current=b.units[invoke]&255u;
 if(entry==Entry::NormalAndExceptional&&!OrdinaryInvoke(current))return deny("ordinary-invoke-required");
 const auto width=artdcevm::compiled_frame_dex_oracle::detail::Width(current);
 for(size_t i=0;i<b.try_count;++i){const auto&t=b.tries[i];
  if(!t.count||t.first>=b.count||t.count>b.count-t.first||!s.boundaries[t.first]||(t.first+t.count<b.count&&!s.boundaries[t.first+t.count])||!t.handler_count||!t.handlers)return deny("try-range");
  if(i&&b.tries[i-1].first+b.tries[i-1].count>t.first)return deny("overlapping-try");
  for(size_t j=0;j<t.handler_count;++j){const auto&h=t.handlers[j];if(h.pc>=b.count||!s.boundaries[h.pc])return deny("catch-target");
   if(h.type==UINT32_MAX&&(!t.catch_all||j+1!=t.handler_count))return deny("catch-all-order");
  }
  if(t.catch_all&&t.handlers[t.handler_count-1].type!=UINT32_MAX)return deny("catch-all-missing");
 }
 // A move-exception definition anywhere in the immutable body must identify
 // a real encoded handler start and a real CodeItem register, even when this
 // particular paused invoke does not select it.
 for(uint32_t pc=0;pc<b.count;++pc)if(s.boundaries[pc]&&(b.units[pc]&255u)==0x0d){
  if((b.units[pc]>>8)>=b.registers)return deny("continuation-move-exception-register-outside-CodeItem");
  bool handler=false;for(size_t i=0;i<b.try_count;++i)for(size_t j=0;j<b.tries[i].handler_count;++j)handler|=b.tries[i].handlers[j].pc==pc;
  if(!handler)return deny("continuation-move-exception-not-handler-entry");
 }
 // A caught reference is issued only by a selected, fully validated encoded
 // handler entry. This is a structural reference lifetime, not a Throwable
 // identity/root permission: the enclosing scanner still needs its current
 // Source/Plan/frame-root and closed JNI/native-entry owners.
 struct CaughtReference {uint32_t entry,vreg;const Catch*handler;};
 bool returned=false;
 auto walk=[&](uint32_t first,const CaughtReference* caught)->const char* {
  for(uint32_t i=0;i<b.count;++i)s.seen[i]=0;
  size_t head=0,tail=0;
  auto enqueue=[&](int64_t pc){if(pc<0||uint64_t(pc)>=b.count||!s.boundaries[pc])return false;if(!s.seen[pc]){s.seen[pc]=1;s.queue[tail++]=uint32_t(pc);}return true;};
  if(!enqueue(first))return "continuation-entry-outside-body";
  while(head<tail){const auto pc=s.queue[head++];++r.reachable;if(pc<=invoke)r.backedge=true;
   const auto word=b.units[pc];const auto op=word&255u;const auto w=artdcevm::compiled_frame_dex_oracle::detail::Width(op);
   // No normal edge or later exceptional edge can reissue a pending exception.
   // A back edge to even this owner's entry is a repeated reference definition.
   if(op==0x0d)return "continuation-move-exception-not-owned-entry";
   if(op==0x0e){returned=true;continue;}
   if(op==0&&word==0){if(!enqueue(uint64_t(pc)+w))return "falloff";continue;}
   if(op==0x28){if(!enqueue(int64_t(pc)+static_cast<int8_t>(word>>8)))return "branch-target";continue;}
   if(op==0x29){if(!enqueue(int64_t(pc)+S16(b.units[pc+1])))return "branch-target";continue;}
   if(op==0x2a){if(!enqueue(int64_t(pc)+S32(b.units[pc+1],b.units[pc+2])))return "branch-target";continue;}
   // This closed remainder never reads OR overwrites any vreg. Thus a bound
   // caught reference remains an unused local until return; iget/invoke/throw,
   // object moves, scalar overwrites and every other operand/type operation
   // still require an independent owner and are not treated as nop.
   if(caught&&(!caught->handler||caught->vreg>=b.registers||caught->entry>=b.count))return "continuation-caught-reference-owner-invalid";
   return "continuation-dependency-owner-required";
  }
  return nullptr;
 };
 if(entry==Entry::NormalAndExceptional){
  if(uint64_t(invoke)+width>=b.count)return deny("normal-successor");
  if(const char*failure=walk(invoke+width,nullptr))return deny(failure);
 }
 // Validate each active entry independently. Shared catch PCs do not share a
 // reusable receipt or visitation state, and cannot hide a later unsafe path.
 bool covered=false;
 for(size_t i=0;i<b.try_count;++i){const auto&t=b.tries[i];if(invoke<t.first||invoke-t.first>=t.count)continue;covered=true;
  if(!t.catch_all)r.exception_escape=true;
  for(size_t j=0;j<t.handler_count;++j){const auto&handler=t.handlers[j];uint32_t first=handler.pc;
   if((b.units[first]&255u)==0x0d){
    const uint32_t vreg=b.units[first]>>8;if(vreg>=b.registers)return deny("continuation-move-exception-register-outside-CodeItem");
    const CaughtReference caught{first,vreg,&handler};++r.caught_entries;++r.reachable;
    if(first+1>=b.count)return deny("exception-successor");
    if(const char*failure=walk(first+1,&caught))return deny(failure);
   }else if(const char*failure=walk(first,nullptr))return deny(failure);
  }
 }
 if(!covered)r.exception_escape=true;
 r.complete=true;r.primitive_return=returned||(entry==Entry::ExceptionalOnly&&r.exception_escape);r.failure="";return r;
}
// Ordinary caller-owned storage. The stopped reader never grows it.
struct HandlerStorage {Try*tries;size_t try_capacity;Catch*handlers;size_t handler_capacity;};
namespace detail {
inline bool AlignTryTail(const unsigned char*base,size_t tail,size_t*out)noexcept {
 const auto address=uintptr_t(base);if(!base||!out||tail>UINTPTR_MAX-address||address+tail>UINTPTR_MAX-3)return false;
 *out=((address+tail+3)&~uintptr_t{3})-address;return true;
}
struct Bytes {
 const unsigned char*begin=nullptr;size_t size=0;
 template<class T>bool Read(size_t at,T*out)const noexcept{if(!out||!begin||at>size||sizeof(T)>size-at)return false;std::memcpy(out,begin+at,sizeof(T));return true;}
 bool Leb(size_t*cursor,uint32_t*out,bool sign=false)const noexcept{
  if(!cursor||!out)return false;uint32_t value=0;
  for(unsigned i=0;i<5;++i){unsigned char byte=0;if(!Read((*cursor)++,&byte))return false;
   if(i==4&&(!sign?(byte&0xf0)!=0:((byte&0x78)!=0&&(byte&0x78)!=0x78)))return false;
   value|=uint32_t(byte&127)<<(7*i);
   if(!(byte&128)){if(sign&&(byte&64)&&i<4)value|=UINT32_MAX<<(7*(i+1));*out=value;return true;}}
  return false;
 }
};
}
struct StorageRequirement {size_t units=0,tries=0,handlers=0;bool complete=false;};
// Complete bounded encoded-list census. This counts unused handler entries as
// well: Read validates the whole source list, not only the current try range.
// It grants no continuation or stop capability and allocates no storage.
inline StorageRequirement CountStorage(const receiver_guard::CodeView&source,
 const modern_field_metadata::ReadContext&context)noexcept {
 StorageRequirement r;
 if(!context.owner||!context.extent||!source.code_item||!source.instructions||!source.units||source.units>UINT32_MAX)return r;
 const size_t header=source.compact?4:16;const auto code=uintptr_t(source.code_item);
 if(code>UINTPTR_MAX-header||uintptr_t(source.instructions)!=code+header||source.units>(SIZE_MAX-header)/2)return r;
 size_t extent=0;if(!context.extent(context.owner,source.code_item,&extent)||extent<header+source.units*2)return r;
 const auto&dex=source.source_dex;const unsigned char*base=dex.data_begin;size_t bytes=dex.data_size;
 auto contains=[&](const unsigned char*b,size_t n){const auto start=uintptr_t(b);return b&&n<=UINTPTR_MAX-start&&code>=start&&code-start<=n&&header+source.units*2<=n-(code-start);};
 if(!contains(base,bytes)){base=dex.begin;bytes=dex.size;}
 if(!contains(base,bytes)||!context.extent(context.owner,base,&extent)||extent<bytes)return r;
 r.units=source.units;r.tries=source.tries_size;
 if(!source.tries_size){r.complete=true;return r;}
 size_t tail=code-uintptr_t(base)+header+source.units*2;
 if(!detail::AlignTryTail(base,tail,&tail))return r;
 if(tail>bytes||size_t(source.tries_size)>(bytes-tail)/8)return r;
 size_t cursor=tail+size_t(source.tries_size)*8;detail::Bytes memory{base,bytes};uint32_t lists=0,types=0;
 if(!dex.begin||dex.size<112||!context.extent(context.owner,dex.begin,&extent)||extent<dex.size||!detail::Bytes{dex.begin,dex.size}.Read(64,&types)||!memory.Leb(&cursor,&lists)||!lists||lists>bytes-cursor)return r;
 for(uint32_t list=0;list<lists;++list){uint32_t encoded=0;if(!memory.Leb(&cursor,&encoded,true))return r;
  const auto count=static_cast<int32_t>(encoded);if(count==INT32_MIN)return r;
  const uint32_t typed=count<0?uint32_t(-count):uint32_t(count);const size_t needed=size_t(typed)+(count<=0);
  if(needed>SIZE_MAX-r.handlers)return r;r.handlers+=needed;
  for(uint32_t item=0;item<typed;++item){uint32_t type=0,pc=0;if(!memory.Leb(&cursor,&type)||type>=types||!memory.Leb(&cursor,&pc)||pc>=source.units)return r;}
  if(count<=0){uint32_t pc=0;if(!memory.Leb(&cursor,&pc)||pc>=source.units)return r;}
 }
 r.complete=true;return r;
}
// Source CodeItem view must already be calibrated and the source/public body
// identity checked by its owner. This returns structural evidence only.
inline Shape Read(const receiver_guard::CodeView&source,uint32_t pc,
 const modern_field_metadata::ReadContext&context,HandlerStorage storage,Scratch scratch,Entry entry=Entry::NormalAndExceptional)noexcept{
 Shape failure;auto deny=[&](const char*reason){failure.failure=reason;return failure;};
 if(!context.owner||!context.extent||!source.code_item||!source.instructions||!source.units||source.units>UINT32_MAX||source.ins_size>source.registers||source.tries_size>storage.try_capacity||(source.tries_size&&!storage.tries))return deny("source-CodeItem-or-storage");
 const auto code=uintptr_t(source.code_item),instructions=uintptr_t(source.instructions);
 const size_t header=source.compact?4:16;
 if(code>UINTPTR_MAX-header||instructions!=code+header||source.units>(UINTPTR_MAX-instructions)/2)return deny("source-CodeItem-instruction-geometry");
 size_t extent=0;
 if(!context.extent(context.owner,source.code_item,&extent)||extent<header+source.units*2)return deny("source-CodeItem-current-extent");
 // Prefer the precise source data allocation; IDs may live in another section.
 const auto&dex=source.source_dex;const auto*base=dex.data_begin;size_t bytes=dex.data_size;
 auto contains=[&](const unsigned char*b,size_t n){const auto start=uintptr_t(b);return b&&n<=UINTPTR_MAX-start&&code>=start&&code-start<=n&&header+source.units*2<=n-(code-start);};
 if(!contains(base,bytes)){base=dex.begin;bytes=dex.size;}if(!contains(base,bytes)||!context.extent(context.owner,base,&extent)||extent<bytes)return deny("source-CodeItem-data-owner");
 if(!dex.begin||dex.size<112||!context.extent(context.owner,dex.begin,&extent)||extent<dex.size)return deny("source-DEX-ID-owner");
 if(pc>=source.units)return deny("source-current-PC-outside-body");
 if(entry==Entry::NormalAndExceptional&&(source.units-pc<3||!OrdinaryInvoke(source.instructions[pc]&255u)||!native_boot_aot_unaffected::DexIdentity(dex).ReturnsVoid(source.instructions[pc+1])))return deny("source-ordinary-void-invoke-required");
 detail::Bytes memory{base,bytes};const size_t code_offset=code-uintptr_t(base);
 size_t tail=code_offset+header+source.units*2;
 // Fixed Standard/Compact GetTryItems uses 4-byte alignment, not merely an
 // odd-instruction count: Compact CodeItem itself is only 2-byte aligned.
 if(source.tries_size&&!detail::AlignTryTail(base,tail,&tail))return deny("source-try-alignment");
 if(tail>bytes||uint64_t(source.tries_size)*8>bytes-tail)return deny("source-try-table-bounds");
 const size_t handlers_base=tail+size_t(source.tries_size)*8;
 for(uint32_t t=0;t<source.tries_size;++t)storage.tries[t]={};
 size_t handlers_used=0;
 if(source.tries_size){
  size_t list_cursor=handlers_base;uint32_t list_count=0,type_count=0;
  if(!memory.Leb(&list_cursor,&list_count)||!list_count||list_count>bytes-list_cursor||!detail::Bytes{dex.begin,dex.size}.Read(64,&type_count))return deny("source-handler-list-header");
  // Validate the entire encoded list, including unused entries. Offsets must
  // select an actual encoded handler start, never a byte inside its ULEB.
  for(uint32_t item=0;item<list_count;++item){
   const size_t start=list_cursor-handlers_base;uint32_t encoded=0;
   if(!memory.Leb(&list_cursor,&encoded,true))return deny("source-handler-size");
   const int32_t signed_count=static_cast<int32_t>(encoded);
   if(signed_count==INT32_MIN)return deny("source-handler-size-overflow");
   const uint32_t count=signed_count<0?uint32_t(-signed_count):uint32_t(signed_count);
   const size_t needed=size_t(count)+(signed_count<=0?1:0);
   if(needed>storage.handler_capacity-handlers_used||(needed&&!storage.handlers))return deny("source-handler-storage");
   auto*begin=storage.handlers+handlers_used;
   for(uint32_t j=0;j<count;++j){uint32_t type=0,target=0;if(!memory.Leb(&list_cursor,&type)||type>=type_count||!memory.Leb(&list_cursor,&target)||target>=source.units)return deny("source-typed-catch");begin[j]={type,target};}
   if(signed_count<=0){uint32_t target=0;if(!memory.Leb(&list_cursor,&target)||target>=source.units)return deny("source-catch-all");begin[count]={UINT32_MAX,target};}
   for(uint32_t t=0;t<source.tries_size;++t){uint32_t first=0;uint16_t length=0,offset=0;
    if(!memory.Read(tail+size_t(t)*8,&first)||!memory.Read(tail+size_t(t)*8+4,&length)||!memory.Read(tail+size_t(t)*8+6,&offset))return deny("source-try-item");
    if(offset==start)storage.tries[t]={first,length,begin,needed,signed_count<=0};
   }
   handlers_used+=needed;
  }
 }
 return Inspect({source.instructions,uint32_t(source.units),storage.tries,source.tries_size,source.registers},pc,scratch,entry);
}
}
