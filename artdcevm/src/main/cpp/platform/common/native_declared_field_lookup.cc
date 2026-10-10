#include "platform/common/native_declared_field_lookup.h"
#include "platform/common/readable_mapping_scope.h"
namespace artdcevm::native_declared_field_lookup {
void* Read(const modern_field_metadata::FieldMetadata& metadata,void* self,
           void* (*current)(),void* owner,
           const original_field_declaration::Declaration& declaration,
           std::string* error,Observation* observation){
 if(observation)*observation={};
 if(!self||!current||self!=current()||!metadata.Calibrated()){
  if(error)*error="Declared Field native metadata/current Runnable context unavailable";
  return nullptr;
 }
 // Only this immediate traversal owns the bounded VMA cache. The original
 // factory can safepoint, so its after-factory lookup constructs a fresh reader.
 // Every cache miss queries the actual current VMA; no prior snapshot or JNI
 // object address supplies mapping permission to this traversal.
 method_identity::ImmediateReadableRegions mappings;
 struct Record final {Observation* out;method_identity::ImmediateReadableRegions& mappings;
  ~Record(){if(out)out->mapping_os_queries=mappings.OsQueries();}
 } record{observation,mappings};
 const modern_field_metadata::ReadContext read{&mappings,[](void* raw,const void* address,size_t* extent){
  return static_cast<method_identity::ImmediateReadableRegions*>(raw)->Extent(address,extent);
 }};
 field_array::View table;
 if(!metadata.Fields(owner,(declaration.flags&8)!=0,&table,error,&read))return nullptr;
 const char* failure=nullptr;
 void* matched=original_field_declaration::Match(table,owner,declaration,[&](void* field,field_copy::CopyFieldView* text,void** declaring){
  if(observation)++observation->inspected_fields;
  if(!metadata.FieldIdentity(field,text,error,&read))return false;
  *declaring=metadata.FieldDeclaringClass(field);return self==current();
 },&failure);
 if(!matched&&failure&&error&&error->empty())*error=failure;
 return matched;
}
}
