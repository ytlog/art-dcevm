#include "platform/common/native_method_table.h"
#include "platform/common/dex_mapping_abi.h"
#include "platform/abi/arm64/android11/dex_cache.h"
#include "platform/abi/arm64/android14/dex_cache.h"
#include <cstring>
#include <unordered_map>
namespace artdcevm::native_method_table {
namespace {
bool Mapped(const void* p,size_t need,const modern_field_metadata::ReadContext* context){size_t extent=0;return p&&modern_field_metadata::ReadMappingExtent(context,p,&extent)&&extent>=need;}
template<class T>T Scalar(const void* p,size_t at){T value;std::memcpy(&value,static_cast<const unsigned char*>(p)+at,sizeof(value));return value;}
}
bool Read(const modern_field_metadata::FieldMetadata& fields,
    const method_identity::Calibration& calibration,void* klass,View* output,std::string* error,const modern_field_metadata::ReadContext* context){
  const auto fail=[&](const char* text){if(error)*error=text;return false;};
  if(!output||!fields.Calibrated()||calibration.identities.size()<3||
     calibration.identities.size()!=calibration.readable_extents.size()||
     (calibration.model!=method_identity::Model::kMethod11&&calibration.model!=method_identity::Model::kMethod12Onward))
    return fail("Native method table lacks complete independent source calibration");
  const auto& source=fields.StorageModel()==modern_field_metadata::Model::kSplit?
      class_metadata::kLegacy:class_metadata::k17;
  if(!Mapped(klass,source.size,context)||uintptr_t(klass)%4)return fail("Native Class scalar source prefix is not mapped");
  static_assert(offsetof(class_metadata::Class11,dex_cache)==offsetof(class_metadata::Class17,dex_cache));
  static_assert(offsetof(abi::arm64::android11::LegacyCache,dex_file)==offsetof(abi::arm64::android14::DualCache,dex_file));
  void* cache=fields.Reference(klass,offsetof(class_metadata::Class11,dex_cache));
  constexpr size_t file_offset=offsetof(abi::arm64::android11::LegacyCache,dex_file);
  if(!Mapped(cache,file_offset+8,context))return fail("Native DexCache immutable DEX link is not mapped");
  void* file=reinterpret_cast<void*>(uintptr_t(Scalar<uint64_t>(cache,file_offset)));
  if(!Mapped(file,sizeof(dex_mapping_abi::Prefix),context))return fail("Native DexFile shared source prefix is not mapped");
  const auto prefix=Scalar<dex_mapping_abi::Prefix>(file,0);
  if(!Mapped(prefix.begin,0x70,context)||std::memcmp(prefix.begin,"dex\n",4)||prefix.begin[7])
    return fail("Native method table requires mapped StandardDex header");
  // file_size is the immutable standard header extent, including 041's selected
  // file. The modern unused_size slot is never accepted as a mapping bound.
  const uint32_t file_size=Scalar<uint32_t>(prefix.begin,32);
  if(file_size<0x70||!Mapped(prefix.begin,file_size,context)||!prefix.data_begin||!prefix.data_size||
     !Mapped(prefix.data_begin,prefix.data_size,context))return fail("Native DEX header/data exceed actual mappings");
  View result;
  result.dex={prefix.begin,file_size,prefix.data_begin,prefix.data_size,
      Scalar<uint32_t>(klass,source.dex_class_def_index)};
  if(!evolution::ReadMappedDexDeclarations(result.dex,&result.declarations,error))return false;
  const auto named=fields.Class(klass,false,context);
  if(!named.descriptor||result.declarations.descriptor!=named.descriptor)
    return fail("DEX class_def identity disagrees with independently calibrated named Class descriptor");
  evolution::MappedDexMemberCounts counts;
  if(!evolution::ReadMappedDexMemberCounts(result.dex,&counts,error)||
     uint64_t(counts.direct_methods)+counts.virtual_methods!=result.declarations.methods.size())
    return fail("Complete DEX method declarations disagree with class_data counts");
  const auto& layout=calibration.model==method_identity::Model::kMethod11?
      method_metadata::kAndroid11:method_metadata::kAndroid12Onward;
  void* array=reinterpret_cast<void*>(uintptr_t(Scalar<uint64_t>(klass,48)));size_t extent=0;
  if(array&&!modern_field_metadata::ReadMappingExtent(context,array,&extent))return fail("Native method array lacks actual mapped extent");
  const method_array::Boundaries boundaries{counts.direct_methods,
      counts.direct_methods+counts.virtual_methods,method_array::Order::ByFlags};
  if(!method_array::Read(layout,array,extent,boundaries,&result.methods))
    return fail("Native method array exceeds actual mapping or DEX declarations");
  std::unordered_map<uint32_t,const evolution::Member*> expected;
  for(const auto& member:result.declarations.methods)
    if(member.dex_index==UINT32_MAX||!expected.emplace(member.dex_index,&member).second)
      return fail("Complete DEX method oracle contains duplicate/missing identity");
  uint32_t direct=0;
  for(uint32_t n=0;n<boundaries.declared_count;++n){
    method_metadata::Identity native;
    if(!method_metadata::ReadIdentity(layout,result.methods.At(n),layout.size,&native))
      return fail("Native declared method identity is not readable");
    const auto found=expected.find(native.dex_method_index);
    if(found==expected.end()||(native.access_flags&0x11fu)!=(found->second->access&0x11fu))
      return fail("Native declared method disagrees with complete DEX index/flags oracle");
    expected.erase(found);direct+=result.methods.IsDirect(n);
  }
  if(!expected.empty()||direct!=counts.direct_methods)
    return fail("Native table omitted DEX methods or changed direct/virtual classification");
  *output=std::move(result);if(error)error->clear();return true;
}
}
