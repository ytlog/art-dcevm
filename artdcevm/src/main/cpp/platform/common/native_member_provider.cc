#include "platform/common/native_member_provider.h"
#include "platform/common/native_schema_binding.h"
#include <algorithm>
namespace artdcevm::native_member_provider {
static_assert(offsetof(class_metadata::Class11,super_class)==offsetof(class_metadata::Class17,super_class));
static_assert(offsetof(class_metadata::Class11,class_loader)==offsetof(class_metadata::Class17,class_loader));
bool Service::ReflectReady(const source_commit::Plan& p,void* raw){auto& s=*static_cast<Service*>(raw);return s.plan_==&p&&s.mapping_&&s.reflection_->Validate(*s.mapping_,nullptr,s.refreshed_)&&([&]{
 if(s.refreshed_){
  if(!s.MatchFacts(p))return false;
  // Final validation cannot allocate. The immutable requests were prepared
  // alongside the schema policy, before any source publication.
  return s.guard_requests_.empty()||(s.schema_guards_&&s.schema_guards_->VerifyBatch(p,s.guard_requests_,nullptr));
 }
 if(s.schemas_.empty())return true;
 return s.schema_metadata_&&s.schema_model_&&native_schema_policy::ValidateNative(p,*s.schema_metadata_,*s.schema_model_,s.views_,s.schemas_,s.schema_guards_,nullptr);
 })();}
void Service::ReflectRetire(const source_commit::Plan&,void* raw){auto& s=*static_cast<Service*>(raw);if(s.schema_guards_&&std::any_of(s.schemas_.begin(),s.schemas_.end(),[](const auto& policy){return !policy.Requirements().empty();}))s.schema_guards_->BeginCommit(*s.plan_);s.reflection_->Retire(*s.mapping_);}
void Service::ReflectUpdate(const source_commit::Plan&,void* raw){auto& s=*static_cast<Service*>(raw);s.reflection_->Update(*s.mapping_,s.refreshed_);}
bool Service::CaptureFacts(const source_commit::Plan& p){
 const auto& metadata=*schema_metadata_;
 const auto& layout=metadata.StorageModel()==modern_field_metadata::Model::kSplit?class_metadata::kLegacy:class_metadata::k17;
 for(size_t i=0;i<tables_.size();++i)for(void* klass:{p.old_classes[i],p.new_classes[i]}){
  field_copy::CopyClassView identity;if(!metadata.ClassIdentity(klass,&identity,nullptr))return false;
  class_metadata::State state;size_t extent=0;
  if(!method_identity::ReadableMappingExtent(klass,&extent)||!class_metadata::ReadState(layout,klass,extent,&state))return false;
  ClassFact c{klass,identity.parent,identity.loader,state.access_flags&0xffffu,0,0,tables_[i]->Count(klass)};
  for(bool stat:{false,true}){auto fields=metadata.Class(klass,stat);(stat?c.static_fields:c.instance_fields)=fields.field_count;
   field_array::View field_array;if(!metadata.Fields(klass,stat,&field_array,nullptr))return false;(stat?c.static_array:c.instance_array)=field_array.array;
   for(size_t n=0;n<fields.field_count;++n){auto field=metadata.Field(klass,stat,n);const auto* native=static_cast<const abi::arm64::Field*>(field.field);
    member_facts_.push_back({field.field,metadata.FieldDeclaringClass(field.field),native->access_flags&0xffffu,native->dex_field_index,false});}}
  for(size_t n=0;n<c.methods;++n){void* method=tables_[i]->At(klass,n);
   member_facts_.push_back({method,tables_[i]->DeclaringClass(method),native_schema_policy::JavaFlags(tables_[i]->Flags(method)),([&]{method_metadata::Identity value;const auto& method_layout=schema_model_->model==method_identity::Model::kMethod11?method_metadata::kAndroid11:method_metadata::kAndroid12Onward;return method_metadata::ReadIdentity(method_layout,method,method_layout.size,&value)?value.dex_method_index:UINT32_MAX;}()),true});}
  class_facts_.push_back(c);
  if(klass==p.old_classes[i]&&i<schemas_.size())for(const auto& required:schemas_[i].Requirements())guard_requests_.push_back({i,c.loader,required});
 }
 return true;
}
bool Service::MatchFacts(const source_commit::Plan& p)const noexcept{
 if(class_facts_.size()!=2*p.old_classes.size())return false;
 const auto& metadata=*schema_metadata_;
 const auto& layout=metadata.StorageModel()==modern_field_metadata::Model::kSplit?class_metadata::kLegacy:class_metadata::k17;
 for(size_t i=0;i<class_facts_.size();++i){const auto& c=class_facts_[i];
  if(c.klass!=(i%2?p.new_classes[i/2]:p.old_classes[i/2]))return false;
  class_metadata::State state;size_t extent=0;
  if(!method_identity::ReadableMappingExtent(c.klass,&extent)||!class_metadata::ReadState(layout,c.klass,extent,&state)||(state.access_flags&0xffffu)!=c.flags||
     metadata.Reference(c.klass,offsetof(class_metadata::Class11,super_class))!=c.parent||metadata.Reference(c.klass,offsetof(class_metadata::Class11,class_loader))!=c.loader||(tables_[i/2]->Count(c.klass)!=c.methods||!tables_[i/2]->MatchesClassTable(c.klass)))return false;
  field_array::View instance,statik;
  if(!metadata.Fields(c.klass,false,&instance,nullptr)||!metadata.Fields(c.klass,true,&statik,nullptr)||instance.selected_count!=c.instance_fields||statik.selected_count!=c.static_fields||instance.array!=c.instance_array||statik.array!=c.static_array)return false;
 }
 for(const auto& fact:member_facts_){
  if(metadata.FieldDeclaringClass(fact.member)!=fact.declaring)return false;
  if(fact.method){if(native_schema_policy::JavaFlags(method_metadata::ReadAccessFlags(fact.member))!=fact.flags||([&]{method_metadata::Identity value;const auto& method_layout=schema_model_->model==method_identity::Model::kMethod11?method_metadata::kAndroid11:method_metadata::kAndroid12Onward;return method_metadata::ReadIdentity(method_layout,fact.member,method_layout.size,&value)?value.dex_method_index:UINT32_MAX;}())!=fact.index)return false;}
  else{const auto* field=static_cast<const abi::arm64::Field*>(fact.member);if((field->access_flags&0xffffu)!=fact.flags||field->dex_field_index!=fact.index)return false;}
 }
 return true;
}
bool Service::OwnsPreparedSource(const source_commit::Plan& p,const native_source_commit::Services& source)const noexcept{
 return plan_==&p&&prepared_source_==&source&&mapping_&&reflection_&&schema_metadata_&&schema_metadata_->Calibrated()&&schema_model_&&!tables_.empty()&&tables_.size()==p.old_classes.size()&&views_.size()==tables_.size();
}
void Service::ReleasePreparedSource(const native_source_commit::Services& source)noexcept{if(prepared_source_==&source)prepared_source_=nullptr;}
bool Service::CollectManagedRoots(source_commit::Services& source,const prepared_roots::Registrar& registrar){
 if(!mapping_||&source!=prepared_source_)return false;
 for(auto& c:class_facts_)if(!roots_.Add(&c.klass,registrar)||!roots_.Add(&c.parent,registrar)||!roots_.Add(&c.loader,registrar))return false;
 for(auto& request:guard_requests_)if(!roots_.Add(&request.loader,registrar))return false;
 for(auto& m:member_facts_)if(!roots_.Add(&m.declaring,registrar))return false;
 roots_.Seal();
 for(auto& table:tables_)if(!table->CollectManagedRoots(registrar))return false;
 if(!mapping_->CollectManagedRoots(registrar))return false;
 return static_cast<native_source_commit::Services&>(source).CollectManagedRoots(registrar);
}
bool Service::RefreshManagedRoots(const source_commit::Plan& p,source_commit::Services& source,const prepared_roots::Resolver& resolver)noexcept{
 if(&p!=plan_||!mapping_||&source!=prepared_source_)return false;
 auto& native=static_cast<native_source_commit::Services&>(source);
 if(!roots_.CanRefresh(resolver)||!native.CanRefreshManagedRoots(resolver)||!mapping_->CanRefreshManagedRoots(resolver))return false;
 for(const auto& table:tables_)if(!table->CanRefreshManagedRoots(resolver))return false;
 roots_.Refresh(resolver);
 for(auto& table:tables_)table->RefreshManagedRoots(resolver);
 mapping_->RefreshManagedRoots(resolver);native.RefreshManagedRoots(resolver);refreshed_=true;return true;
}
std::unique_ptr<source_commit::Services> Service::Prepare(const source_commit::Plan& p,Dependencies d,std::string* error){
 auto fail=[&](const char* text)->std::unique_ptr<source_commit::Services>{if(error)*error=text;return nullptr;};
 switch(d.expected_removal){
  case Removal::kAny:case Removal::kMethodsOnly:case Removal::kFieldsOnly:case Removal::kSchemaEvolution:break;
  default:return fail("Unknown native member removal policy");
 }
 if(!d.hooks||!d.methods||!d.method_model||!d.metadata||!d.fields||!d.cache||!d.reflection||!d.is_retired||
     !d.metadata->Calibrated()||!d.fields->Calibrated()||!d.cache->PreparedForCurrentThread()||
     p.old_classes.empty()||p.old_classes.size()!=p.new_classes.size())return fail("Incomplete independently calibrated native member provider");
 if((d.expected_removal==Removal::kSchemaEvolution)!=bool(d.schemas))return fail("Native schema evolution requires explicit complete policies and matching transaction mode");
 plan_=&p;prepared_source_=nullptr;roots_=prepared_roots::Bindings{};refreshed_=false;reflection_=d.reflection;tables_.clear();views_.clear();class_facts_.clear();member_facts_.clear();mapping_.reset();schemas_.clear();
 guard_requests_.clear();schema_guards_=d.schema_guards;schema_metadata_=d.metadata;schema_model_=d.method_model;
 if(d.schemas){if(d.schemas->empty())return fail("Native schema evolution lacks policies");schemas_=*d.schemas;}
 auto& views=views_;
 for(size_t n=0;n<p.old_classes.size();++n){native_method_table::View before,after;
  if(!native_method_table::Read(*d.metadata,*d.method_model,p.old_classes[n],&before,error)||
     !native_method_table::Read(*d.metadata,*d.method_model,p.new_classes[n],&after,error))return nullptr;
  auto methods=modern_methods::Service::PrepareTables(*d.methods,p.old_classes[n],before.methods,p.new_classes[n],after.methods,d.is_retired,error);
  if(!methods)return nullptr;views.push_back(methods.get());tables_.push_back(std::move(methods));
 }
 mapping_=native_structural_reflection::Plan::Prepare(p,*d.metadata,views,d.retirement,error,d.handle_guards,
     {d.hooks,d.method_model,d.fields},d.schemas?&schemas_:nullptr,d.schema_guards);
 if(!mapping_||!CaptureFacts(p))return nullptr;
 const bool fields=mapping_->HasDeletedFields(),methods=mapping_->HasDeletedMethods();
 if((!d.allow_field_removal&&fields)||
    (d.expected_removal==Removal::kMethodsOnly&&(!methods||fields))||
    (d.expected_removal==Removal::kFieldsOnly&&(!fields||methods)))return fail("Native member removal differs from the proven transaction shape");
 if(!reflection_->Validate(*mapping_,error))return nullptr;
 modern_source_commit::Dependencies dependencies;dependencies.observed=d.runtime;dependencies.metadata=d.metadata;
 dependencies.fields=d.fields;dependencies.prepare_field_copy=bool(d.schemas);
 dependencies.allow_removed_ancestors=std::any_of(schemas_.begin(),schemas_.end(),[](const auto& schema){return schema.Requires(native_schema_policy::GuardKind::HierarchyGraph);});
 dependencies.diagnostics=d.diagnostics;dependencies.caches=d.cache;dependencies.reflective={ReflectReady,ReflectUpdate,this,ReflectRetire};
 // Preparation is readonly, including the persistent retirement registries.
 // The source adapter publishes only after Run completes its final Validate.
 auto prepared=modern_source_commit::Prepare(*d.hooks,dependencies,p,views,error);prepared_source_=prepared.get();return prepared;
}
}
