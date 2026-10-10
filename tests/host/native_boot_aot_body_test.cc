#include "platform/common/native_boot_aot_unaffected.h"
#include "platform/common/native_receiver_code_item.h"
#include <array>
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <cstdlib>
#include <new>
static bool no_alloc=false;
void* operator new(size_t n){assert(!no_alloc);if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete(void*p,size_t)noexcept{std::free(p);}
namespace b=artdcevm::native_boot_aot_unaffected;
namespace r=artdcevm::receiver_guard;
namespace c=artdcevm::code_item_metadata;
namespace {
struct Memory {const void*p;size_t size;bool live=true,denied=false;};
bool Extent(void*owner,const void*p,size_t*out){auto&m=*static_cast<Memory*>(owner);auto x=uintptr_t(p),base=uintptr_t(m.p);if(!m.live||m.denied||x<base||x-base>=m.size)return false;*out=m.size-(x-base);return true;}
}
int main(int argc,char**argv){
 assert(argc==3);std::ifstream f(argv[1],std::ios::binary);std::vector<unsigned char>dex{std::istreambuf_iterator<char>(f),{}};const auto code_offset=std::stoul(argv[2]);assert(code_offset+20<=dex.size());
 artdcevm::evolution::MappedDexMetadata map{dex.data(),dex.size(),dex.data(),dex.size(),0};
 std::array<unsigned char,32> method{};uint32_t index=2,flags=9;std::memcpy(method.data()+4,&flags,4);std::memcpy(method.data()+8,&index,4);uint64_t code=uintptr_t(dex.data()+code_offset);std::memcpy(method.data()+16,&code,8);
 r::CodeView source;assert(r::ReadCodeView(c::kStandard,method.data(),method.size(),map,&source)&&source.source_dex.begin==dex.data());
 artdcevm::native_boot_method_catalog::Method entry;entry.native_method=method.data();entry.modifiers=9;entry.max_locals=1;entry.name="staticKeep";entry.descriptor="()I";entry.declaring_signature="Lcom/example/Fixture;";entry.dex.assign(reinterpret_cast<const unsigned char*>(source.instructions),reinterpret_cast<const unsigned char*>(source.instructions)+source.units*2);entry.body=artdcevm::native_boot_method_catalog::Classify(entry.dex.data(),entry.dex.size(),entry.descriptor.c_str());
 artdcevm::decoded_frame_recovery::RegisterRange range{0,1};Memory memory{dex.data(),dex.size()};artdcevm::modern_field_metadata::ReadContext context{&memory,Extent};
 no_alloc=true;assert(b::BodyMatches(entry,source,2,0,range,context));no_alloc=false;assert(b::BodyMatches(entry,source,2,1,range,context));
 entry.name="keep";assert(!b::BodyMatches(entry,source,2,0,range,context));entry.name="staticKeep";
 entry.declaring_signature="Ljava/lang/Object;";assert(!b::BodyMatches(entry,source,2,0,range,context));entry.declaring_signature="Lcom/example/Fixture;";
 entry.descriptor="()J";assert(!b::BodyMatches(entry,source,2,0,range,context));entry.descriptor="()I";
 entry.modifiers=1;assert(!b::BodyMatches(entry,source,2,0,range,context));entry.modifiers=9;
 entry.dex[0]^=1;assert(!b::BodyMatches(entry,source,2,0,range,context));entry.dex[0]^=1;
 range.count=0;assert(!b::BodyMatches(entry,source,2,0,range,context));range.count=1;
 assert(!b::BodyMatches(entry,source,2,2,range,context));memory.denied=true;assert(!b::BodyMatches(entry,source,2,0,range,context));memory.denied=false;
 // Corrupt ID ranges and string offsets must fail as values under the stop
 // allocation prohibition, without constructing parser exceptions.
 const auto saved_offset=source.source_dex.begin;auto invalid=source;invalid.source_dex.size=95;
 no_alloc=true;assert(!b::BodyMatches(entry,invalid,2,0,range,context));no_alloc=false;
 uint32_t saved=0;std::memcpy(&saved,dex.data()+92,4);uint32_t outside=UINT32_MAX;std::memcpy(dex.data()+92,&outside,4);
 no_alloc=true;assert(!b::BodyMatches(entry,source,2,0,range,context));no_alloc=false;std::memcpy(dex.data()+92,&saved,4);assert(source.source_dex.begin==saved_offset);
 const unsigned char wide[]{0x13,0,0x0f,0,0x0f,0};
 assert(artdcevm::native_boot_method_catalog::Classify(wide,sizeof(wide),"()I",0)==artdcevm::native_boot_method_catalog::Body::kPrimitiveLocal);
 assert(artdcevm::native_boot_method_catalog::Classify(wide,sizeof(wide),"()I",1)==artdcevm::native_boot_method_catalog::Body::kUnknown);
 assert(artdcevm::native_boot_method_catalog::Classify(wide,sizeof(wide),"()I",2)==artdcevm::native_boot_method_catalog::Body::kPrimitiveLocal);
 // Fixed native decoded StackMap: each cell stores logical value+1. Empty
 // root mask indices are zero encoded (kNoValue), not inferred from body labels.
 std::array<uint32_t,8>row{};artdcevm::code_info_view::View view;
 auto&t=view.tables[0];t.region={row.data(),0,sizeof(row)*8};t.rows=1;t.row_bits=256;t.column_count=8;for(size_t i=0;i<=8;++i)t.columns[i]=i*32;
 Memory masks{row.data(),sizeof(row)};artdcevm::modern_field_metadata::ReadContext mask_context{&masks,Extent};
 assert(b::TablesMapped(view,mask_context)&&b::EmptyRootMasks(view,0));
 row[3]=1;assert(!b::EmptyRootMasks(view,0));row[3]=0;row[4]=1;assert(!b::EmptyRootMasks(view,0));row[4]=0;
 t.region.bit_size++;assert(!b::TablesMapped(view,mask_context));t.region.bit_size--;
 masks.live=false;assert(!b::TablesMapped(view,mask_context));masks.live=true;assert(!b::EmptyRootMasks(view,1));
 std::cout<<"Actual bounded CodeItem/public immutable bytes+native MethodId/type/proto identity, complete empty GC root masks and mapped table extent positive/negative PASS (body predicate only, no stop authority)\n";
}
namespace artdcevm::runnable_executor {runnable_jni::Result With(JNIEnv*,jobject,runnable_jni::Operation,void*){std::abort();}}
