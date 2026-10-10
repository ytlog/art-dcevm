#pragma once
#include "api/hook_engine.h"
#include "platform/common/native_reflective_value_abi.h"
#include "platform/common/modern_field_metadata.h"
#include "platform/common/modern_field_storage.h"
#include "platform/common/type_cache_update.h"
#include <string>
namespace artdcevm::native_cache_semantic {
// Fixed ART DexCache::VisitReflectiveTargets declaration. The original callee
// owns traversal, pair invalidation and barriers; no DexCache object layout is
// described here. Type storage is accessed through two independently proved
// instance-long ArtFields and their already calibrated general field semantics.
class Service;
class TypeReader {
 public:
  virtual ~TypeReader()=default;
  virtual bool Read(const Service&,void*,uint32_t,type_cache::View*,const modern_field_metadata::ReadContext*)const noexcept=0;
};
class Service {
 public:
  using Visit=void (*)(void*,art::ReflectiveValueVisitor*);
  using Entry=void* (*)(void*,uint32_t);
  struct Leaves {Visit visit=nullptr;Entry type=nullptr,method=nullptr;};
  bool Bind(HookEngine&,std::string*);
  bool PrepareStorage(const modern_field_metadata::FieldMetadata&,
      const modern_field_storage::Service&,void* cache,void* owning_class,
      void* types_field,void* types_array_field,uint64_t public_types,
      uint64_t public_types_array,const modern_field_metadata::ReadContext*,std::string*);
  bool Bound()const noexcept{return leaves_.visit&&leaves_.type&&leaves_.method;}
  bool StoragePrepared()const noexcept{return metadata_&&storage_&&reader_;}
  // Every borrowed managed object belongs to the supplied actual source root
  // window. A final mapping context is mandatory at final/exclusive callers.
  bool Types(void* cache,uint32_t type_count,type_cache::View*,
      const modern_field_metadata::ReadContext*)const noexcept;
  bool Calibrate(void* cache,uint32_t types,uint32_t fields,uint32_t methods,
      uint32_t type_index,void* expected_class,uint32_t field_index,void* expected_field,
      uint32_t method_index,void* expected_method,bool original_resolution_proved,
      const modern_field_metadata::ReadContext*,std::string*);
  bool Calibrated()const noexcept{return calibrated_;}
  bool CheckWitness(void* cache,uint32_t fields,uint32_t methods,uint32_t field_index,
      void* expected_field,uint32_t method_index,void* expected_method)const noexcept;
  using MemberRole=bool(*)(void*,bool field,uint32_t index,void* member) noexcept;
  // Validate every original visitor entry against independent immutable public
  // roles. Empty/evicted witnesses are permitted; unknown occupied roles refuse.
  bool CheckRoles(void* cache,uint32_t fields,uint32_t methods,void* owner,MemberRole)const noexcept;
  bool CheckTypeEntry(void* cache,uint32_t index,void* expected)const noexcept;
  bool Readonly(void* cache,uint32_t fields,uint32_t methods)const noexcept;
  bool Clear(void* cache,uint32_t fields,uint32_t methods)const noexcept;
  bool Cleared(void* cache,uint32_t fields,uint32_t methods)const noexcept;
 private:
  Leaves leaves_{};
  const TypeReader* reader_=nullptr;
  const modern_field_metadata::FieldMetadata* metadata_=nullptr;
  const modern_field_storage::Service* storage_=nullptr;
  void* fields_[2]{};
  bool calibrated_=false;
  friend struct HostFixture;
  friend struct StorageReader;
};
}
