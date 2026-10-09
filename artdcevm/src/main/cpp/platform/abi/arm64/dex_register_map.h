#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>
#include "platform/abi/arm64/android11/frame_registers.h"
namespace artdcevm::abi::arm64::dex_register_map {
using Location=android11::frame_registers::Location;
// Fixed runtime/[oat/]stack_map.h on 11–17 and libartbase/base/dchecked_vector.h:
// count, sixteen inline locations, then a vector subclass with no extra members.
// ART's DecodeDexRegisterMap only writes caller-owned locations. It never grows
// or destroys this container; allocation/destruction stay in our C++ runtime.
class CheckedLocations:private std::vector<Location> {
 public:using std::vector<Location>::data;using std::vector<Location>::resize;
};
struct Map {
  explicit Map(size_t count):count(count),small{} {
    const Location invalid{-2,0};
    if(count<=small.size())std::fill_n(small.begin(),count,invalid);
    else large.resize(count,invalid);
  }
  Location* data(){return count<=small.size()?small.data():large.data();}
  const Location* data()const{return count<=small.size()?small.data():large.data();}
  size_t count;
  std::array<Location,16> small;
  CheckedLocations large;
};
static_assert(sizeof(Location)==8 && sizeof(CheckedLocations)==24);
static_assert(sizeof(Map)==160 && offsetof(Map,count)==0 && offsetof(Map,small)==8 && offsetof(Map,large)==136);
using Decode=void (*)(const void*,uint32_t,uint32_t,Map*);
inline constexpr char kDecode[]="_ZNK3art8CodeInfo20DecodeDexRegisterMapEjjPNS_14DexRegisterMapE";
}
