#pragma once
#include "platform/common/code_info_view.h"
#include <algorithm>

namespace artdcevm::decoded_frame_recovery {
using code_info_view::View;
constexpr uint32_t kNoValue=UINT32_MAX;
constexpr uint32_t kMaxSearchDistance=32; // fixed AOSP 11--17 stack_map.cc.
struct RegisterRange { uint32_t first=0, count=0; };
struct Location { int32_t kind=-2; uint32_t value=0; }; // logical value, no ART ABI.
// Borrowed decoded memory, exact source declaration and a stable mutator-locked
// frame are caller prerequisites. Nothing in this layer establishes capability.
inline bool Range(const View& view,uint32_t row,uint32_t begin,uint32_t end,
                  RegisterRange* output) {
  if(!output || begin>end)return false;
  uint32_t map;
  if(!view.StackMaps().Read(row,7,&map) || map==kNoValue)return false;
  RegisterRange range{0,view.dex_regs};
  if(begin!=end) {
    uint32_t start;
    if(!view.StackMaps().Read(row,5,&start) || start==kNoValue || begin<start ||
       end>view.InlineInfos().rows)return false;
    // The visitor's end selects its current inlinee; begin may describe the
    // complete prefix. Validate each cumulative entry and IsLast boundary.
    uint32_t previous=view.dex_regs;
    for(uint32_t at=start;at<end;++at) {
      uint32_t last,terminal;
      if(!view.InlineInfos().Read(at,5,&last) || last==kNoValue || last<previous ||
         !view.InlineInfos().Read(at,0,&terminal) ||
         (terminal!=0 && terminal!=kNoValue) || (at+1<end && terminal==kNoValue))return false;
      if(at+1==end)range={previous,last-previous};
      previous=last;
    }
  }
  if(range.count>UINT32_MAX-range.first)return false;
  *output=range;return true;
}
inline bool MaskBit(const code_info_view::Table& mask,uint32_t row,uint32_t bit,bool* out) {
  if(!out || mask.column_count!=1 || bit>=mask.row_bits)return false;
  uint32_t encoded;
  if(!bit_table_read::ReadCell(mask.region,mask.rows,row,mask.row_bits,bit,1,&encoded))return false;
  *out=encoded==0;return true; // BitTable scalar encoding subtracts one; raw mask doesn't.
}
inline bool Catalog(const View& view,uint32_t index,Location* out) {
  if(!out)return false;
  uint32_t kind,value;
  if(!view.tables[7].Read(index,0,&kind) || !view.tables[7].Read(index,1,&value) ||
     (kind!=kNoValue && kind>5))return false;
  if(kind==0) { // kInStack, packed in four-byte frame slots.
    if(value>UINT32_MAX/4)return false;
    value*=4;
  }
  *out={kind==kNoValue?-1:static_cast<int32_t>(kind),value};return true;
}
// Caller supplies storage to keep stack walking allocation-free. Contents are
// unspecified on failure; no frame/register/root mutation is performed.
inline bool Decode(const View& view,uint32_t row,uint32_t begin,uint32_t end,
                   Location* storage,size_t capacity,RegisterRange* output) {
  RegisterRange range;
  if(!output || !Range(view,row,begin,end,&range) || range.count>capacity ||
     (range.count && !storage))return false;
  for(uint32_t i=0;i<range.count;++i)storage[i]={-2,0};
  uint32_t remaining=range.count;
  for(uint32_t distance=0;remaining && distance<=row;++distance) {
    if(distance>kMaxSearchDistance)return false;
    uint32_t mask_index,map_index;
    if(!view.StackMaps().Read(row-distance,6,&mask_index))return false;
    if(mask_index==kNoValue)continue;
    const auto& masks=view.tables[5];
    if(mask_index>=masks.rows || masks.column_count!=1)return false;
    if(masks.row_bits<=range.first)continue;
    if(!view.StackMaps().Read(row-distance,7,&map_index) || map_index==kNoValue)return false;
    uint32_t limit=std::min(masks.row_bits,range.first+range.count);
    for(uint32_t bit=0;bit<limit;++bit) {
      bool set;if(!MaskBit(masks,mask_index,bit,&set))return false;
      if(!set)continue;
      uint32_t catalog;
      // Also validate skipped prefix indices: malformed carrier data must not
      // be accepted simply because this inlinee happens not to use that slot.
      if(!view.tables[6].Read(map_index,0,&catalog))return false;
      Location location;if(!Catalog(view,catalog,&location))return false;
      if(bit>=range.first && storage[bit-range.first].kind==-2) {
        storage[bit-range.first]=location;--remaining;
      }
      if(map_index==UINT32_MAX)return false;
      ++map_index;
    }
  }
  // The first map's unassigned registers mean None in the upstream decoder.
  for(uint32_t i=0;i<range.count;++i)if(storage[i].kind==-2)storage[i]={-1,0};
  *output=range;return true;
}
// Object ABI is an explicit caller-owned source contract, never chosen by API,
// BuildID or named-constructor availability. The extent and borrowed backing
// allocations must be independently established before this entry point.
inline bool DecodeSourceFrame(code_info_view::SourceFamily family,const void* object,size_t extent,
                              uint32_t row,uint32_t begin,uint32_t end,Location* storage,
                              size_t capacity,RegisterRange* output) {
  View view;
  return code_info_view::ReadSourceObject(family,object,extent,&view) &&
         Decode(view,row,begin,end,storage,capacity,output);
}
// Preserve the existing source13 availability policy while sharing its count
// service. This deliberately does not authorize active-frame reconstruction.
inline bool LegacyCount(const View& view,uint32_t row,uint32_t begin,uint32_t end,uint32_t* count) {
  return code_info_view::RegisterCount(view,row,begin,end,count);
}
}
