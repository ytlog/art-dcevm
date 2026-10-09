#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <vector>
namespace artdcevm::readonly_field_table {
// ArtField source shape is invariant for ARM32/ARM64: compressed owner, atomic
// flags, DEX field index, managed object offset. Only identity is read here.
struct Field {uint32_t owner,flags,index,offset;};
static_assert(sizeof(Field)==16&&alignof(Field)==4);
struct Oracle {const void* field;size_t extent;uint32_t index,flags;};
struct View {const void* array;size_t extent;uint32_t count;int static_group=-1;};
inline bool Prove(const std::vector<View>& tables,const std::vector<Oracle>& oracles,
 uint32_t declared,uint32_t statics,uint32_t owner,std::string* error) {
 auto fail=[&](const char* text){if(error)*error=text;return false;};
 if(oracles.size()!=declared||statics>declared||tables.empty())return fail("Incomplete independent field oracle");
 uint64_t total=0;std::vector<uint32_t> counts;
 for(const auto& table:tables) {
  uint32_t count=0;
  if(table.array) {
   if(uintptr_t(table.array)%4||uintptr_t(table.array)>UINTPTR_MAX-4||table.extent<4)return fail("Field table prefix not mapped");
   std::memcpy(&count,table.array,4);
   if(count>(table.extent-4)/16||count>(UINTPTR_MAX-uintptr_t(table.array)-4)/16)return fail("Field table exceeds mapped extent");
  } else if(table.extent)return fail("Null field table has nonzero extent");
  if(count!=table.count)return fail("Field table count disagrees with independent class_data");
  counts.push_back(count);total+=count;
 }
 if(total!=declared)return fail("Field storage omits/duplicates declared fields");
 std::set<uint32_t> indices;std::set<uintptr_t> addresses;uint32_t actual_statics=0;
 for(const auto& oracle:oracles) {
  if(!oracle.field||oracle.extent<16||uintptr_t(oracle.field)%4||
      oracle.index==UINT32_MAX||!indices.insert(oracle.index).second||!addresses.insert(uintptr_t(oracle.field)).second)
    return fail("Field leaf is not a distinct mapped identity");
  bool found=false;int group=-1;
  for(size_t t=0;t<tables.size();++t) {
   uintptr_t begin=uintptr_t(tables[t].array)+4,address=uintptr_t(oracle.field);
   if(tables[t].array&&address>=begin&&(address-begin)%16==0&&(address-begin)/16<counts[t]){found=true;group=tables[t].static_group;}
  }
  if(!found)return fail("Original reflected Field is outside complete owned tables");
  uint32_t identity[3];std::memcpy(&identity[0],oracle.field,4);
  std::memcpy(&identity[2],static_cast<const unsigned char*>(oracle.field)+8,4);
  identity[1]=__atomic_load_n(reinterpret_cast<const uint32_t*>(static_cast<const unsigned char*>(oracle.field)+4),__ATOMIC_RELAXED);
  if((group!=-1&&((identity[1]&8u)!=0)!=(group==1))||identity[0]!=owner||identity[2]!=oracle.index||(identity[1]&0xdfu)!=(oracle.flags&0xdfu))
    return fail("Field identity disagrees with independently decoded Class/APK/reflection");
  actual_statics+=(identity[1]&8u)!=0;
 }
 if(actual_statics!=statics)return fail("Field static/instance flags disagree with class_data");
 if(error)error->clear();return true;
}
}
