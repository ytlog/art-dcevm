#pragma once
#include "platform/common/method_array_view.h"
#include "platform/common/method_identity_calibration.h"
#include <set>

namespace artdcevm::owned_method_table {
static_assert(offsetof(class_metadata::Class11,methods)==48);
static_assert(offsetof(class_metadata::Class16,methods)==48);
static_assert(offsetof(class_metadata::Class17,methods)==48);
// Calibrate ordinary classes only, using every declared Method/Constructor from
// an independent APK class_data oracle. All roots stay alive in this Runnable
// scope. The common source methods-pointer prefix and native identity select
// storage; no version-specific class tail, SDK, Build ID or guessed extent.
// Read another strongly rooted Class using the declaration already proven by
// the runtime bootstrap. Each target entry still needs an independent complete
// DEX/reflection oracle; bootstrap calibration vectors are not target evidence.
inline bool ReadProvenDeclaration(const runnable_jni::Context& context,
    abi::arm64::android_common::FromReflectedMethod leaf,
    method_identity::Model proven_model,const void* klass,
    const evolution::MappedDexMemberCounts& dex,const method_identity::Oracle* methods,
    size_t count,method_array::View* output,std::string* error) {
  auto fail=[&](const char* text){if(error)*error=text;return false;};
  if(!context.self||!context.env||!context.vm||!leaf||!klass||!output||!methods||!count||
      uint64_t(dex.direct_methods)+dex.virtual_methods!=count||count>UINT32_MAX||context.env->ExceptionCheck())
    return fail("Incomplete owned method-table oracle");
  if(proven_model!=method_identity::Model::kMethod11&&proven_model!=method_identity::Model::kMethod12Onward)return fail("Unproven method declaration");
  const auto& declaration=proven_model==method_identity::Model::kMethod11?
      method_metadata::kAndroid11:method_metadata::kAndroid12Onward;
  size_t extent=0;
  if(!method_identity::ReadableMappingExtent(klass,&extent)||extent<56||uintptr_t(klass)%4)
    return fail("Owned Class methods pointer prefix is not readable");
  uint64_t pointer;
  std::memcpy(&pointer,static_cast<const unsigned char*>(klass)+48,sizeof(pointer));
  void* array=reinterpret_cast<void*>(uintptr_t(pointer));
  size_t array_extent=0;
  if(!array||!method_identity::ReadableMappingExtent(array,&array_extent))
    return fail("Owned method array is not readable");
  method_array::View table;
  const method_array::Boundaries boundaries{dex.direct_methods,static_cast<uint32_t>(count),method_array::Order::ByFlags};
  if(!method_array::Read(declaration,array,array_extent,boundaries,&table))
    return fail("Owned method table exceeds independent mapped extent");
  const abi::arm64::android_common::RunnableJniScope scope{context.self,context.env,context.vm};
  std::set<uint32_t> physical,indices;
  uint32_t direct=0;
  for(size_t i=0;i<count;++i) {
    const auto& oracle=methods[i];
    if(!oracle.strong_global_method||oracle.dex_method_index==UINT32_MAX||
        !indices.insert(oracle.dex_method_index).second||
        context.env->GetObjectRefType(oracle.strong_global_method)!=JNIGlobalRefType||context.env->ExceptionCheck())
      return fail("Method table oracle is not distinct owned globals");
    void* method=leaf(scope,oracle.strong_global_method);
    if(context.env->ExceptionCheck()||!method)return fail("Named reflected method table leaf failed");
    const auto begin=uintptr_t(array)+8,address=uintptr_t(method);
    if(address<begin||(address-begin)%table.stride||(address-begin)/table.stride>=count)
      return fail("Owned method identity is outside declared table");
    const auto index=static_cast<uint32_t>((address-begin)/table.stride);
    method_metadata::Identity native;
    if(!physical.insert(index).second||!method_metadata::ReadIdentity(declaration,method,table.stride,&native)||
        native.dex_method_index!=oracle.dex_method_index||(native.access_flags&0x11fu)!=(oracle.java_modifiers&0x11fu))
      return fail("Method table native/DEX/reflection identity mismatch");
    direct+=table.IsDirect(index);
  }
  if(direct!=dex.direct_methods)return fail("Method table direct flags disagree with APK class_data");
  *output=table;if(error)error->clear();return true;
}
inline bool Calibrate(const runnable_jni::Context& context,
    abi::arm64::android_common::FromReflectedMethod leaf,
    const method_identity::Calibration& identity_model,const void* klass,
    const evolution::MappedDexMemberCounts& dex,const method_identity::Oracle* methods,
    size_t count,method_array::View* output,std::string* error) {
  if(identity_model.identities.size()!=count||identity_model.readable_extents.size()!=count){
    if(error)*error="Incomplete owned method-table bootstrap calibration";return false;
  }
  return ReadProvenDeclaration(context,leaf,identity_model.model,klass,dex,methods,count,output,error);
}
}  // namespace artdcevm::owned_method_table
