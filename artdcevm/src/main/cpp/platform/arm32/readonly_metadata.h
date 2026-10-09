#pragma once
#include "platform/common/readonly_method_table.h"
#include "platform/common/readonly_field_table.h"
#include "platform/common/borrowed_jni_callback.h"
#include "platform/common/arm32_method_metadata.h"
#include "platform/abi/arm32/android11/class.h"
#include "platform/abi/arm32/android16/class.h"
#include "platform/abi/arm32/android17/class.h"
#include <atomic>
namespace artdcevm::arm32_readonly_metadata {
// Release ObjPtr uses uintptr_t; AAPCS returns this single 32-bit carrier in r0.
// Debug poisoning is refused by independent Class/method/table/DEX agreement.
struct ObjectPointer {uintptr_t reference;};
using Decode=ObjectPointer (*)(const void*,jobject);
inline constexpr const char* kDecode="_ZNK3art6Thread13DecodeJObjectEP8_jobject";
using FieldLeaf=void* (*)(void*);
inline constexpr const char* kFieldLeaf="_ZN3art6mirror5Field11GetArtFieldEv";
using Extent=bool (*)(const void*,size_t*);
struct Request {
  Decode decode;Extent extent;jclass owner;FieldLeaf field_leaf=nullptr;
  std::vector<jobject> fields;std::vector<uint32_t> field_indices,field_flags;
  uint32_t static_fields=0;bool fields_proven=false;
  std::vector<jobject> methods;std::vector<uint32_t> indices,flags;
  uint32_t direct,declared,class_def,type_index,class_flags;
  readonly_method_table::Proof proof{};size_t class_index_offset=0;bool class_proven=false;
  std::string error;
};
inline bool Observe(const borrowed_jni_callback::Context& context,void* opaque) {
  auto& request=*static_cast<Request*>(opaque);
  auto fail=[&](const char* text){request.error=text;return false;};
  if(sizeof(void*)!=4||!request.decode||!request.extent||!request.owner||
      !context.original_leaf||!context.original_method||request.methods.size()<3||
      request.indices.size()!=request.methods.size()||request.flags.size()!=request.methods.size())
    return fail("ARM32 metadata lacks independent complete JNI/DEX oracle");
  // Every object is decoded and consumed before this borrowed Runnable callback
  // returns; no raw managed/native address escapes in the resulting Proof.
  void* klass=reinterpret_cast<void*>(request.decode(context.self,request.owner).reference);
  size_t class_extent=0;
  if(context.env->ExceptionCheck()||!klass||uintptr_t(klass)%4||
      !request.extent(klass,&class_extent)||class_extent<88)
    return fail("Independently decoded Class prefix is unavailable");
  auto scalar=[&](size_t at){uint32_t value;std::memcpy(&value,static_cast<unsigned char*>(klass)+at,4);return value;};
  size_t candidates=0,chosen=0;
  constexpr size_t legacy_index=offsetof(abi::arm32::android11::Class11,dex_class_def_index);
  constexpr size_t merged_index=offsetof(abi::arm32::android17::Class17,dex_class_def_index);
  static_assert(legacy_index==80&&merged_index==72);
  for(size_t at:{legacy_index,merged_index}) {
    const size_t access=at==legacy_index?offsetof(abi::arm32::android11::Class11,access_flags):offsetof(abi::arm32::android17::Class17,access_flags);
    if(scalar(at)==request.class_def&&scalar(at+4)==request.type_index&&
        (__atomic_load_n(reinterpret_cast<const uint32_t*>(static_cast<unsigned char*>(klass)+access),__ATOMIC_RELAXED)&0x611u)==(request.class_flags&0x611u)) {
      ++candidates;chosen=at;
    }
  }
  if(candidates!=1)return fail("Class source prefix does not uniquely match APK class_def/type/reflection");
  uint64_t table_bits=0;std::memcpy(&table_bits,static_cast<unsigned char*>(klass)+offsetof(abi::arm32::android11::Class11,methods),8);
  if(table_bits>UINTPTR_MAX)return fail("ARM32 Class method pointer contains upper bits");
  void* table=reinterpret_cast<void*>(uintptr_t(table_bits));size_t table_extent=0;
  if(!request.extent(table,&table_extent))return fail("Class method allocation is not readable");
  abi::common::RunnableJniScope scope{context.self,context.env,context.vm};
  std::vector<readonly_method_table::Oracle> oracles;
  for(size_t i=0;i<request.methods.size();++i) {
    void* method=context.original_leaf(scope,request.methods[i]);size_t extent=0;
    if(context.env->ExceptionCheck()||!method||!request.extent(method,&extent)||extent<8)
      return fail("Original JNI reflected leaf failed to provide mapped Method");
    uint32_t owner;std::memcpy(&owner,method,4);
    if(owner!=uintptr_t(klass))return fail("Original Method declaring Class disagrees with independently decoded owner");
    oracles.push_back({method,extent,request.indices[i],request.flags[i]});
  }
  size_t accepted=0;readonly_method_table::Proof proof;
  for(const auto& source:{readonly_method_table::Declaration{28,12,4},readonly_method_table::Declaration{24,8,4}}) {
    readonly_method_table::Proof candidate;std::string ignored;
    if(readonly_method_table::Prove(source,table,table_extent,oracles,request.declared,request.direct,&candidate,&ignored)) {++accepted;proof=candidate;}
  }
  if(accepted!=1)return fail("ARM32 complete table does not uniquely match a fixed Method source declaration");
  request.proof=proof;request.class_index_offset=chosen;request.class_proven=true;
  if(!request.field_leaf||request.fields.size()!=request.field_indices.size()||
      request.fields.size()!=request.field_flags.size())return fail("Named Field leaf or complete independent field oracle unavailable");
  std::vector<readonly_field_table::View> field_tables;
  constexpr size_t instance_at=offsetof(abi::arm32::android11::Class11,ifields);
  constexpr size_t static_at=offsetof(abi::arm32::android11::Class11,sfields);
  static_assert(instance_at==offsetof(abi::arm32::android17::Class17,fields));
  for(size_t at:chosen==legacy_index?std::vector<size_t>{instance_at,static_at}:std::vector<size_t>{instance_at}) {
    uint64_t bits=0;std::memcpy(&bits,static_cast<unsigned char*>(klass)+at,8);
    if(bits>UINTPTR_MAX)return fail("ARM32 Class field pointer contains upper bits");
    void* table=reinterpret_cast<void*>(uintptr_t(bits));size_t extent=0;
    if(table&&!request.extent(table,&extent))return fail("Class Field table mapping unavailable");
    uint32_t expected=chosen==legacy_index?(at==instance_at?uint32_t(request.fields.size())-request.static_fields:request.static_fields):uint32_t(request.fields.size());
    field_tables.push_back({table,extent,expected,chosen==legacy_index?(at==instance_at?0:1):-1});
  }
  std::vector<readonly_field_table::Oracle> field_oracles;
  for(size_t i=0;i<request.fields.size();++i) {
    void* reflected=reinterpret_cast<void*>(request.decode(context.self,request.fields[i]).reference);
    if(context.env->ExceptionCheck()||!reflected)return fail("Independent reflected Field decode failed");
    void* field=request.field_leaf(reflected);size_t extent=0;
    if(context.env->ExceptionCheck()||!field||!request.extent(field,&extent))return fail("Named reflected Field leaf failed");
    field_oracles.push_back({field,extent,request.field_indices[i],request.field_flags[i]});
  }
  if(!readonly_field_table::Prove(field_tables,field_oracles,uint32_t(request.fields.size()),request.static_fields,uint32_t(uintptr_t(klass)),&request.error))return false;
  request.fields_proven=true;
  request.error.clear();return true;
}
}
