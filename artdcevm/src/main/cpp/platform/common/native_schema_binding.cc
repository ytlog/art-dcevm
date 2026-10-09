#include "platform/common/native_schema_binding.h"
#include <algorithm>
namespace artdcevm::native_schema_policy {
bool ReadNativeDeclarations(const modern_field_metadata::FieldMetadata& metadata,const method_identity::Calibration& model,void* klass,evolution::ClassSchema* output,std::string* error,native_method_table::View* table){
 std::string local_error;if(!error)error=&local_error;
 native_method_table::View view;if(!native_method_table::Read(metadata,model,klass,&view,error))return false;
 auto schema=view.declarations;
 const auto& layout=metadata.StorageModel()==modern_field_metadata::Model::kSplit?class_metadata::kLegacy:class_metadata::k17;
 size_t extent=0;class_metadata::State state;
 if(!method_identity::ReadableMappingExtent(klass,&extent)||!class_metadata::ReadState(layout,klass,extent,&state)){if(error)*error="Schema Class flags have no live source provenance";return false;}
 schema.access=state.access_flags&0xffffu;
 std::vector<bool> fields_seen(schema.fields.size()),methods_seen(schema.methods.size());
 for(bool statik:{false,true}){const auto owner=metadata.Class(klass,statik);
  for(size_t i=0;i<owner.field_count;++i){const auto native=metadata.Field(klass,statik,i);
   auto member=std::find_if(schema.fields.begin(),schema.fields.end(),[&](const auto& f){return f.IsStatic()==statik&&native.name&&native.descriptor&&f.name==native.name&&f.descriptor==native.descriptor;});
   if(member==schema.fields.end()||!native.field||metadata.FieldDeclaringClass(native.field)!=klass){if(error)*error="Schema field does not match complete native/DEX owner identity";return false;}
   const size_t index=size_t(member-schema.fields.begin());if(fields_seen[index]){if(error)*error="Duplicate native schema field";return false;}fields_seen[index]=true;
   const auto* art_field=static_cast<const abi::arm64::Field*>(native.field);
   if(art_field->dex_field_index!=member->dex_index||(art_field->access_flags&0xffffu)!=(member->access&0xffffu)){if(error)*error="Native schema field flags/index differ from immutable DEX declaration";return false;}
   member->access=art_field->access_flags&0xffffu;
  }
 }
 const auto& method_layout=model.model==method_identity::Model::kMethod11?method_metadata::kAndroid11:method_metadata::kAndroid12Onward;
 for(size_t i=0;i<view.methods.boundaries.declared_count;++i){void* native=view.methods.At(uint32_t(i));
  // ArtField and ArtMethod both begin with the fixed GcRoot<Class> declaration;
  // this existing service calls the named generic root-slow barrier.
  if(metadata.FieldDeclaringClass(native)!=klass){if(error)*error="Declared native schema method has another Class root";return false;}
  method_metadata::Identity identity;if(!method_metadata::ReadIdentity(method_layout,native,method_layout.size,&identity)){if(error)*error="Unreadable native schema method";return false;}
  auto member=std::find_if(schema.methods.begin(),schema.methods.end(),[&](const auto& m){return m.dex_index==identity.dex_method_index;});
  if(member==schema.methods.end()){if(error)*error="Native schema method is outside complete DEX declarations";return false;}
  const size_t index=size_t(member-schema.methods.begin());if(methods_seen[index]){if(error)*error="Duplicate native schema method";return false;}methods_seen[index]=true;
  if(JavaFlags(identity.access_flags)!=JavaFlags(member->access)){if(error)*error="Native schema method flags differ from immutable DEX declaration";return false;}
  member->access=JavaFlags(identity.access_flags);
 }
 if(std::find(fields_seen.begin(),fields_seen.end(),false)!=fields_seen.end()||std::find(methods_seen.begin(),methods_seen.end(),false)!=methods_seen.end()){if(error)*error="Native schema misses declared members";return false;}
 if(!output){if(error)*error="Missing native schema declarations output";return false;}
 *output=std::move(schema);if(table)*table=std::move(view);if(error)error->clear();return true;
}
bool ValidateNative(const source_commit::Plan& transaction,const modern_field_metadata::FieldMetadata& metadata,const method_identity::Calibration& model,const std::vector<const modern_methods::Service*>& methods,const std::vector<Plan>& policies,const GuardVerifier* guards,std::string* error){
 std::string local_error;if(!error)error=&local_error;
 auto fail=[&](const char* text){if(error)*error=text;return false;};
 if(policies.size()!=transaction.old_classes.size()||methods.size()!=policies.size()||transaction.new_classes.size()!=policies.size()||policies.empty())return fail("Native schema policy lacks the complete rooted Class-pair set");
 for(size_t i=0;i<policies.size();++i){void* old=transaction.old_classes[i];void* next=transaction.new_classes[i];
  if(!methods[i]||old==next)return fail("Native schema pair lacks actual distinct Class tables");
  field_copy::CopyClassView before,after;
  if(!metadata.ClassIdentity(old,&before,error)||!metadata.ClassIdentity(next,&after,error)||before.loader!=after.loader)return fail("Native schema Class loader identity changed");
  if(before.parent!=after.parent){bool paired=false;for(size_t p=0;p<policies.size();++p)paired|=transaction.old_classes[p]==before.parent&&transaction.new_classes[p]==after.parent;
   const bool hierarchy=policies[i].Requires(GuardKind::HierarchyGraph);
   if(!paired&&!hierarchy)return fail("Native schema superclass identity differs outside the rooted transaction");}
  evolution::ClassSchema old_schema,next_schema;native_method_table::View old_table,next_table;
  if(!ReadNativeDeclarations(metadata,model,old,&old_schema,error,&old_table)||!ReadNativeDeclarations(metadata,model,next,&next_schema,error,&next_table))return false;
  for(const auto& pair:{std::make_pair(old,&old_table),std::make_pair(next,&next_table)}){
   if(methods[i]->Count(pair.first)!=pair.second->methods.count)return fail("Prepared method table count changed during schema validation");
   for(uint32_t n=0;n<pair.second->methods.count;++n)if(methods[i]->At(pair.first,n)!=pair.second->methods.At(n))return fail("Prepared native method identities changed during schema validation");
  }
  if(!policies[i].Matches(old_schema,next_schema))return fail("Prepared schema policy differs from actual complete native declarations");
  for(const auto& required:policies[i].Requirements()){
   if(!guards)return fail("Native schema modifier requires independent loaded-Class/final-field proof");
   if(!guards->Verify(transaction,i,before.loader,required,error)){
    if(error->empty())*error="Native schema modifier requires independent loaded-Class/final-field proof";
    return false;
   }
  }
 }
 if(error)error->clear();return true;
}
}
