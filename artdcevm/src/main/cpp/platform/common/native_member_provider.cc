#include "platform/common/native_member_provider.h"
#include "platform/common/native_schema_binding.h"
#include <algorithm>
namespace artdcevm::native_member_provider {
bool Service::ReflectReady(const source_commit::Plan& p,void* raw){auto& s=*static_cast<Service*>(raw);return s.plan_==&p&&s.mapping_&&s.reflection_->Validate(*s.mapping_,nullptr)&&([&]{
 if(s.schemas_.empty())return true;
 std::vector<const modern_methods::Service*> methods;for(const auto& table:s.tables_)methods.push_back(table.get());
 return s.schema_metadata_&&s.schema_model_&&native_schema_policy::ValidateNative(p,*s.schema_metadata_,*s.schema_model_,methods,s.schemas_,s.schema_guards_,nullptr);
 })();}
void Service::ReflectRetire(const source_commit::Plan&,void* raw){auto& s=*static_cast<Service*>(raw);if(s.schema_guards_&&std::any_of(s.schemas_.begin(),s.schemas_.end(),[](const auto& policy){return !policy.Requirements().empty();}))s.schema_guards_->BeginCommit(*s.plan_);s.reflection_->Retire(*s.mapping_);}
void Service::ReflectUpdate(const source_commit::Plan&,void* raw){auto& s=*static_cast<Service*>(raw);s.reflection_->Update(*s.mapping_);}
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
 plan_=&p;reflection_=d.reflection;tables_.clear();mapping_.reset();schemas_.clear();
 schema_guards_=d.schema_guards;schema_metadata_=d.metadata;schema_model_=d.method_model;
 if(d.schemas){if(d.schemas->empty())return fail("Native schema evolution lacks policies");schemas_=*d.schemas;}
 std::vector<const modern_methods::Service*> views;
 for(size_t n=0;n<p.old_classes.size();++n){native_method_table::View before,after;
  if(!native_method_table::Read(*d.metadata,*d.method_model,p.old_classes[n],&before,error)||
     !native_method_table::Read(*d.metadata,*d.method_model,p.new_classes[n],&after,error))return nullptr;
  auto methods=modern_methods::Service::PrepareTables(*d.methods,p.old_classes[n],before.methods,p.new_classes[n],after.methods,d.is_retired,error);
  if(!methods)return nullptr;views.push_back(methods.get());tables_.push_back(std::move(methods));
 }
 mapping_=native_structural_reflection::Plan::Prepare(p,*d.metadata,views,d.retirement,error,d.handle_guards,
     {d.hooks,d.method_model,d.fields},d.schemas?&schemas_:nullptr,d.schema_guards);
 if(!mapping_)return nullptr;
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
 return modern_source_commit::Prepare(*d.hooks,dependencies,p,views,error);
}
}
