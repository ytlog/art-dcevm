#include "platform/common/native_transformed_definitions.h"
#include "platform/abi/arm64/common/transformed_definition.h"
#include "platform/common/readable_mapping_scope.h"
#include <climits>
#include <cstring>
#include <utility>

namespace artdcevm::native_transformed_definitions {
bool Capture(const void* source,size_t expected_count,
             std::vector<Definition>*output,std::string*error){
 const auto refuse=[&](const char*reason){if(error)*error=reason;return false;};
 if(!source||!output||!expected_count)
  return refuse("Final transformed input is empty or has no destination");
 using namespace abi::arm64::android_common;
 method_identity::ReadableMappingSnapshot mappings;
 const auto readable=[&](const void*address,size_t bytes){
  size_t extent=0;return address&&mappings.Extent(address,&extent)&&bytes<=extent;
 };
 if(!mappings.Ready()||!readable(source,sizeof(TransformedDefinitions)))
  return refuse("Final transformed vector is unreadable");
 TransformedDefinitions vector;
 std::memcpy(&vector,source,sizeof(vector));
 const auto begin=reinterpret_cast<uintptr_t>(vector.begin);
 const auto end=reinterpret_cast<uintptr_t>(vector.end);
 const auto capacity=reinterpret_cast<uintptr_t>(vector.capacity);
 if(!begin||begin%alignof(TransformedDefinition)||end<begin||capacity<end||
    (end-begin)%sizeof(TransformedDefinition)||
    (capacity-begin)%sizeof(TransformedDefinition)||
    (end-begin)/sizeof(TransformedDefinition)!=expected_count||
    !readable(vector.begin,end-begin))
  return refuse("Final transformed vector geometry does not match the batch");
 std::vector<Definition>candidate;
 candidate.reserve(expected_count);
 for(size_t n=0;n<expected_count;++n){
  TransformedDefinition definition;
  std::memcpy(&definition,reinterpret_cast<const unsigned char*>(vector.begin)+n*sizeof(definition),sizeof(definition));
  if(definition.initialized!=1||definition.redefined>1||definition.structural_update>1||
     !definition.klass||!definition.dex_begin||!definition.dex_size||
     definition.dex_size>INT32_MAX||!readable(definition.dex_begin,definition.dex_size))
   return refuse("Final transformed Class or DEX view is invalid");
  candidate.push_back({definition.klass,
    std::vector<unsigned char>(definition.dex_begin,definition.dex_begin+definition.dex_size)});
 }
 *output=std::move(candidate);
 return true;
}
}
