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
 method_identity::ScopedReadableMappings mappings;
 if(observation)observation->mapping_os_queries=mappings.OsQueries();
 if(!mappings.Ready()){
  if(error)*error="Declared Field native mapping snapshot unavailable";
  return nullptr;
 }
 const modern_field_metadata::ReadContext read{&mappings,[](void* raw,const void* address,size_t* extent){
  return static_cast<method_identity::ScopedReadableMappings*>(raw)->Extent(address,extent);
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
