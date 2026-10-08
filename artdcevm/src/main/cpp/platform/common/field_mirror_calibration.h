#pragma once
#include "platform/common/modern_field_metadata.h"
#include "platform/common/reflection_metadata.h"
#include "platform/common/runnable_executor.h"
#include "platform/common/field_storage.h"
#include <cstring>

namespace artdcevm::field_mirror {
using NamedField = void* (*)(void*);
// Field scalar prefix is identical in fixed release 11--17 field.h declarations.
// Its use is authorized by the complete owned public-field oracle below, not SDK.
inline constexpr reflection_metadata::Layout kDeclaration=reflection_metadata::k12;
struct Calibration { size_t checked=0; bool matched=false; };
inline bool Calibrate(const runnable_jni::Context& context,
                      const modern_field_metadata::FieldMetadata& metadata,
                      NamedField named_field,jobject strong_class,
                      const modern_field_metadata::FieldOracle* oracles,size_t count,
                      Calibration* output,std::string* error) {
  auto fail=[&](const char* text){if(error)*error=text;return false;};
  if(!output||!metadata.Calibrated()||!named_field||!strong_class||!oracles||!count)
    return fail("Field mirror calibration requires complete owned metadata");
  void* klass=nullptr;
  if(!runnable_executor::DecodeStrongGlobal(context,strong_class,&klass))
    return fail("Field mirror declaring Class is not an owned strong root");
  field_array::View instances,statics;
  if(!metadata.Fields(klass,false,&instances,error)||!metadata.Fields(klass,true,&statics,error))return false;
  if(size_t(instances.selected_count)+statics.selected_count!=count)
    return fail("Field mirror oracle is incomplete");
  Calibration result;
  for(size_t i=0;i<count;++i) {
    void* mirror=nullptr;
    if(!runnable_executor::DecodeStrongGlobal(context,oracles[i].strong_global_field,&mirror))
      return fail("Field mirror oracle is not an owned strong root");
    // Native identity is obtained before touching mirror scalar metadata. All
    // native fields/arrays were independently verified by FieldMetadata.
    void* native=named_field(mirror);
    const auto& array=(oracles[i].modifiers&8u)?statics:instances;
    uint32_t physical;
    if(!native||!array.IndexOf(native,&physical))return fail("Field mirror native identity is outside source array");
    field_storage::Field field;
    std::memcpy(&field,native,sizeof(field));
    reflection_metadata::FieldIdentity identity;
    if(!reflection_metadata::ReadField(kDeclaration,mirror,kDeclaration.field_size,&identity)||
       identity.array_index!=physical||identity.access_flags!=field.access_flags||
       identity.offset!=field.offset||(identity.access_flags&0xdfu)!=(oracles[i].modifiers&0xdfu))
      return fail("Field mirror scalar prefix disagrees with native/public identity");
    // Checked physical ownership includes static filtering of merged arrays.
    // Duplicate oracles must not conceal a missing field.
    for(size_t prior=0;prior<i;++prior) {
      void* earlier=nullptr;
      if(!runnable_executor::DecodeStrongGlobal(context,oracles[prior].strong_global_field,&earlier)||
         named_field(earlier)==native)return fail("Duplicate field mirror oracle identity");
    }
    ++result.checked;
  }
  result.matched=true;*output=result;return true;
}
}  // namespace artdcevm::field_mirror
