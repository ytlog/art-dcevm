#include "platform/common/native_member_provider.h"
namespace artdcevm::native_member_provider {
bool Service::ReflectReady(const source_commit::Plan& p,void* raw){auto& s=*static_cast<Service*>(raw);return s.plan_==&p&&s.mapping_&&s.reflection_->Validate(*s.mapping_,nullptr);}
void Service::ReflectRetire(const source_commit::Plan&,void* raw){auto& s=*static_cast<Service*>(raw);s.reflection_->Retire(*s.mapping_);}
void Service::ReflectUpdate(const source_commit::Plan&,void* raw){auto& s=*static_cast<Service*>(raw);s.reflection_->Update(*s.mapping_);}
std::unique_ptr<source_commit::Services> Service::Prepare(const source_commit::Plan& p,Dependencies d,std::string* error){
 auto fail=[&](const char* text)->std::unique_ptr<source_commit::Services>{if(error)*error=text;return nullptr;};
 switch(d.expected_removal){
  case Removal::kAny:case Removal::kMethodsOnly:case Removal::kFieldsOnly:break;
  default:return fail("Unknown native member removal policy");
 }
 if(!d.hooks||!d.methods||!d.method_model||!d.metadata||!d.fields||!d.cache||!d.reflection||!d.is_retired||
     !d.metadata->Calibrated()||!d.fields->Calibrated()||!d.cache->PreparedForCurrentThread()||
     p.old_classes.empty()||p.old_classes.size()!=p.new_classes.size())return fail("Incomplete independently calibrated native member provider");
 plan_=&p;reflection_=d.reflection;tables_.clear();mapping_.reset();
 std::vector<const modern_methods::Service*> views;
 for(size_t n=0;n<p.old_classes.size();++n){native_method_table::View before,after;
  if(!native_method_table::Read(*d.metadata,*d.method_model,p.old_classes[n],&before,error)||
     !native_method_table::Read(*d.metadata,*d.method_model,p.new_classes[n],&after,error))return nullptr;
  auto methods=modern_methods::Service::PrepareTables(*d.methods,p.old_classes[n],before.methods,p.new_classes[n],after.methods,d.is_retired,error);
  if(!methods)return nullptr;views.push_back(methods.get());tables_.push_back(std::move(methods));
 }
 mapping_=native_structural_reflection::Plan::Prepare(p,*d.metadata,views,d.retirement,error,d.handle_guards,
     {d.hooks,d.method_model,d.fields});
 if(!mapping_)return nullptr;
 const bool fields=mapping_->HasDeletedFields(),methods=mapping_->HasDeletedMethods();
 if((!d.allow_field_removal&&fields)||
    (d.expected_removal==Removal::kMethodsOnly&&(!methods||fields))||
    (d.expected_removal==Removal::kFieldsOnly&&(!fields||methods)))return fail("Native member removal differs from the proven transaction shape");
 if(!reflection_->Validate(*mapping_,error))return nullptr;
 modern_source_commit::Dependencies dependencies;dependencies.observed=d.runtime;dependencies.metadata=d.metadata;
 dependencies.fields=d.fields;dependencies.diagnostics=d.diagnostics;dependencies.caches=d.cache;dependencies.reflective={ReflectReady,ReflectUpdate,this,ReflectRetire};
 // Preparation is readonly, including the persistent retirement registries.
 // The source adapter publishes only after Run completes its final Validate.
 return modern_source_commit::Prepare(*d.hooks,dependencies,p,views,error);
}
}
