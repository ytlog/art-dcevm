#pragma once
#include "platform/abi/arm64/android11/code_info.h"
#include "platform/abi/arm64/android12/code_info.h"
#include "platform/abi/arm64/android13/code_info.h"
#include "platform/abi/arm64/android17/code_info.h"
#include "platform/abi/arm64/android14/code_info.h"
#include "platform/abi/arm64/android16/code_info.h"
#include "platform/common/bit_table_read.h"
#include <array>
#include <cstring>
#include <limits>
#include <optional>
#include <type_traits>
namespace artdcevm::code_info_view {
struct Table {
  bit_table_read::Region region{};
  uint32_t rows=0, row_bits=0, column_count=0;
  std::array<uint32_t,9> columns{};
  bool Read(uint32_t row,size_t column,uint32_t* result) const {
    if(column>=column_count)return false;
    return bit_table_read::ReadCell(region,rows,row,row_bits,columns[column],
                                   columns[column+1]-columns[column],result);
  }
};
struct View {
  uint32_t flags=0, dex_regs=0;
  std::optional<uint32_t> code_size;
  std::array<Table,8> tables{};
  const Table& StackMaps() const {return tables[0];}
  const Table& InlineInfos() const {return tables[3];}
};
namespace detail {
inline bool Validate(Table* table) {
  if(table->columns[0]!=0)return false;
  for(size_t i=0;i<table->column_count;++i)
    if(table->columns[i]>table->columns[i+1])return false;
  if(table->columns[table->column_count]!=table->row_bits)return false;
  constexpr size_t max=std::numeric_limits<size_t>::max();
  if(table->rows && table->row_bits>max/table->rows)return false;
  size_t bits=size_t{table->rows}*table->row_bits;
  if(bits>table->region.bit_size || (bits && !table->region.data))return false;
  if(bits && bits-1>max-table->region.bit_start)return false;
  if(bits && (table->region.bit_start+bits-1)/8>
      std::numeric_limits<uintptr_t>::max()-reinterpret_cast<uintptr_t>(table->region.data))return false;
  return true;
}
template<size_t N> bool Normalize(const abi::arm64::android11::BitTable<N>& source,Table* out) {
  Table value{{source.region.data,source.region.bit_start,source.region.bit_size},source.rows,source.columns[N],N,{}};
  for(size_t i=0;i<=N;++i)value.columns[i]=source.columns[i];
  if(!Validate(&value))return false;
  *out=value;return true;
}
template<size_t N> bool Normalize(const abi::arm64::android17::BitTable<N>& source,Table* out) {
  Table value{{source.region.data,source.region.bit_start,source.region.bit_size},source.rows,source.row_bits,N,{}};
  for(size_t i=0;i<N;++i)value.columns[i]=source.columns[i];
  value.columns[N]=source.row_bits;
  if(!Validate(&value))return false;
  *out=value;return true;
}
template<class T> bool NormalizeObject(const T& info,View* output) {
  if(!output)return false;
  View view;view.flags=info.flags;view.dex_regs=info.dex_regs;
  if constexpr(!std::is_same_v<T,abi::arm64::android11::CodeInfo>)view.code_size=info.code_size;
  if(!Normalize(info.stack_maps,&view.tables[0]) || !Normalize(info.register_masks,&view.tables[1]) ||
     !Normalize(info.stack_masks,&view.tables[2]) || !Normalize(info.inline_infos,&view.tables[3]) ||
     !Normalize(info.method_infos,&view.tables[4]) || !Normalize(info.dex_masks,&view.tables[5]) ||
     !Normalize(info.dex_maps,&view.tables[6]) || !Normalize(info.dex_catalog,&view.tables[7]))return false;
  *output=view;return true;
}
} // namespace detail
// Native decoder ownership, immutable table storage and exact matched declaration
// are caller prerequisites. Bounds below are arithmetic checks, not mapped-memory
// discovery or serialized-header decoding. No allocation or managed root reads.
template<class T> bool ReadDecoded(const T& info,View* output) {
  return detail::NormalizeObject(info,output);
}
enum class SourceFamily {kAndroid11,kAndroid12,kAndroid13,kAndroid17,kAndroid14,kAndroid16};
template<class T> bool ReadSourceObject(const void* object,size_t extent,View* output) {
  if(!object || !output || reinterpret_cast<uintptr_t>(object)%alignof(T) || extent<sizeof(T) ||
     sizeof(T)-1>std::numeric_limits<uintptr_t>::max()-reinterpret_cast<uintptr_t>(object))return false;
  T copy;std::memcpy(&copy,object,sizeof(copy));return ReadDecoded(copy,output);
}
inline bool ReadSourceObject(SourceFamily family,const void* object,size_t extent,View* output) {
  switch(family) {
    case SourceFamily::kAndroid11:return ReadSourceObject<abi::arm64::android11::CodeInfo>(object,extent,output);
    case SourceFamily::kAndroid12:return ReadSourceObject<abi::arm64::android12::CodeInfo>(object,extent,output);
    case SourceFamily::kAndroid13:return ReadSourceObject<abi::arm64::android13::CodeInfo>(object,extent,output);
    case SourceFamily::kAndroid14:return ReadSourceObject<abi::arm64::android14::CodeInfo>(object,extent,output);
    case SourceFamily::kAndroid16:return ReadSourceObject<abi::arm64::android16::CodeInfo>(object,extent,output);
    case SourceFamily::kAndroid17:return ReadSourceObject<abi::arm64::android17::CodeInfo>(object,extent,output);
  }
  return false;
}
inline bool RegisterCount(const View& view,uint32_t stack_row,uint32_t inline_begin,
                          uint32_t inline_end,uint32_t* output) {
  if(!output)return false;
  uint32_t map;if(!view.StackMaps().Read(stack_row,7,&map))return false;
  uint32_t available=0;
  if(map!=UINT32_MAX) {
    available=view.dex_regs;
    if(inline_begin!=inline_end) {
      uint32_t start;if(!view.StackMaps().Read(stack_row,5,&start) || inline_end==0)return false;
      uint32_t row=inline_end-1;
      if(start==UINT32_MAX || row<start || row>=view.InlineInfos().rows)return false;
      uint32_t first=view.dex_regs,last;
      if(row!=start && !view.InlineInfos().Read(row-1,5,&first))return false;
      if(!view.InlineInfos().Read(row,5,&last) || last<first)return false;
      available=last-first;
    }
  }
  *output=available;return true;
}
}
