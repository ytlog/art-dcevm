#include "platform/common/native_structural_reflection.h"
#include <cstring>
#include <cstdlib>
#include "platform/common/native_structural_reflection_visitor.h"
#include "platform/common/native_method_table.h"
#include "platform/common/native_schema_binding.h"
#include <algorithm>
#include <cstdio>
#include <optional>
#include "platform/common/native_interface_graph.h"
#include "platform/common/readable_mapping_scope.h"
namespace artdcevm::native_structural_reflection {
using detail::Visitor;using detail::Visit;
namespace {
using namespace resolution_invalidation;
class NativeResolutionObserver final:public Observer {
 public:
  NativeResolutionObserver(const modern_field_metadata::FieldMetadata& metadata,
      const method_identity::Calibration& model,HookEngine& hooks,const modern_field_metadata::ReadContext* context):context_(context),metadata_(metadata),model_(model){
    root_=reinterpret_cast<void*(*)(void*)>(hooks.FindSymbol("libart.so","artReadBarrierForRootSlow"));
    same_=reinterpret_cast<bool(*)(void*,void*)>(hooks.FindSymbol("libart.so","_ZN3art9ArtMethod23HasSameNameAndSignatureEPS0_"));
    // Fixed Class::Implements(ObjPtr<Class>) supplies complete transitive
    // interface ancestry without reading an IfTable or guessing its layout.
    assignable_=reinterpret_cast<bool(*)(void*,uintptr_t)>(hooks.FindSymbol("libart.so","_ZN3art6mirror5Class16IsAssignableFromENS_6ObjPtrIS1_EE"));
    implements_=reinterpret_cast<bool(*)(void*,uintptr_t)>(hooks.FindSymbol("libart.so","_ZN3art6mirror5Class10ImplementsENS_6ObjPtrIS1_EE"));
  }
  bool Available()const{return root_&&same_;}
  bool PrepareHolder(void* holder,std::string* error){
    if(assignable_||implements_)return true;
    if(!metadata_.InterfaceDeclarationCalibrated()){if(error)*error="Interface table declaration lacks independent public calibration";return false;}
    holder_=holder;return native_interface_graph::ReadInContext(metadata_,holder,&interfaces_,error,context_);
  }
  bool ValidateScope(void* holder)const override {
    if(assignable_||implements_)return true;
    std::string error;
    if(holder!=holder_||!metadata_.InterfaceDeclarationCalibrated()||
       !native_interface_graph::MatchesInContext(metadata_,holder,interfaces_,&error,context_))return Fail("live-holder-interface-table-invalid");
    return true;
  }
  const char* FailureReason()const override{return failure_;}
  bool ReadFlags(void* member,Kind kind,uint32_t* out)const override {
    size_t extent=0;
    if(!member||!out||!modern_field_metadata::ReadMappingExtent(context_,member,&extent)||extent<8)return Fail("member-flags-unreadable");
    *out=kind==Kind::Method?method_metadata::ReadAccessFlags(member):field_array::Flags(member);return true;
  }
  bool ReadClass(void* klass,ClassIdentity* out)const override {
    const auto& source=metadata_.StorageModel()==modern_field_metadata::Model::kSplit?class_metadata::kLegacy:class_metadata::k17;
    size_t extent=0;field_copy::CopyClassView view;std::string error;
    if(!klass||!out||!modern_field_metadata::ReadMappingExtent(context_,klass,&extent)||extent<source.size||
       !metadata_.ClassIdentity(klass,&view,&error,context_)||!view.descriptor)return Fail("transaction-class-identity");
    *out={klass,view.loader,view.parent,view.descriptor};return true;
  }
  bool MatchesClass(const ClassIdentity& expected)const override {
    field_copy::CopyClassView actual;
    return metadata_.ClassIdentity(expected.klass,&actual,nullptr,context_)&&actual.parent==expected.parent&&actual.loader==expected.loader&&actual.descriptor&&expected.descriptor==actual.descriptor;
  }
  bool CollectManagedRoots(const prepared_roots::Registrar& r)const override {
    if(!roots_.Add(&holder_,r))return false;
    for(auto& value:interfaces_)if(!roots_.Add(&value,r))return false;
    roots_.Seal();return true;
  }
  bool CanRefreshManagedRoots(const prepared_roots::Resolver& r)const noexcept override{return roots_.CanRefresh(r);}
  void RefreshManagedRoots(const prepared_roots::Resolver& r)const noexcept override{roots_.Refresh(r);}
  bool ReadMember(void* member,Kind kind,MemberIdentity* out)const override {
    const auto& method=model_.model==method_identity::Model::kMethod11?method_metadata::kAndroid11:method_metadata::kAndroid12Onward;
    const size_t need=kind==Kind::Method?method.size:sizeof(field_array::Field);size_t extent=0;
    if(!root_||!member||!out||!modern_field_metadata::ReadMappingExtent(context_,member,&extent)||extent<need)return Fail("member-storage-unreadable");
    void* declaring=root_(member);
    *out={member,declaring,kind==Kind::Method?method_metadata::ReadAccessFlags(member):field_array::Flags(member)};return true;
  }
  bool IsAssignableFrom(void* target,void* holder,bool* out)const override {
    if(!out)return false;
    // The immutable plan already proved holder roots and the complete native
    // superclass graph. Outside that graph only an interface can be assignable
    // from this ordinary transaction Class. Unrelated arrays/proxies/primitive
    // classes need no descriptor or platform std::string ABI at all.
    const auto& source=metadata_.StorageModel()==modern_field_metadata::Model::kSplit?class_metadata::kLegacy:class_metadata::k17;
    size_t extent=0;class_metadata::State state;
    if(!target||!holder||!modern_field_metadata::ReadMappingExtent(context_,target,&extent)||
       !class_metadata::ReadState(source,target,extent,&state))return Fail("declaring-class-state-unreadable");
    if(state.IsPrimitive()){*out=false;return true;}
    if(state.access_flags&0x200u){
      if(assignable_){*out=assignable_(target,uintptr_t(holder));return true;}
      if(implements_){*out=implements_(holder,uintptr_t(target));return true;}
      if(!metadata_.InterfaceDeclarationCalibrated())return Fail("interface-table-declaration-not-independently-calibrated");
      std::string error;
      if(holder!=holder_||!native_interface_graph::MatchesInContext(metadata_,holder,interfaces_,&error,context_))return Fail("live-holder-interface-table-invalid");
      *out=std::find(interfaces_.begin(),interfaces_.end(),target)!=interfaces_.end();return true;
    }
    // The pure plan already captured the entire real superclass chain.
    // Non-interface targets outside it cannot be ancestors; descriptors and
    // loader delegation never substitute for an actual Class identity.
    *out=false;return true;
  }
  bool SameIdentity(const MemberIdentity& actual,const Declaration& candidate,bool* out)const override {
    if(!out)return Fail("signature-output-missing");
    if(candidate.kind==Kind::Method){if(!same_)return Fail("named-method-signature-unavailable");*out=same_(actual.member,candidate.member);return true;}
    field_copy::CopyFieldView text;std::string error;
    if(!metadata_.FieldIdentity(actual.member,&text,&error,context_)||!text.name||!text.descriptor)return Fail("field-name-descriptor-unavailable");
    *out=candidate.name==text.name&&candidate.descriptor==text.descriptor;return true;
  }
 private:
  bool Fail(const char* reason)const{failure_=reason;return false;}
  mutable const char* failure_=nullptr;
  mutable prepared_roots::Bindings roots_;
  mutable void* holder_=nullptr;mutable std::vector<void*> interfaces_;
  const modern_field_metadata::ReadContext* context_=nullptr;
  const modern_field_metadata::FieldMetadata& metadata_;method_identity::Calibration model_;
  void* (*root_)(void*)=nullptr;bool (*same_)(void*,void*)=nullptr;bool (*implements_)(void*,uintptr_t)=nullptr;bool (*assignable_)(void*,uintptr_t)=nullptr;
};
std::unique_ptr<ResolutionInvalidationPlan> PrepareResolution(const source_commit::Plan& source,
    const modern_field_metadata::FieldMetadata& metadata,const ResolutionBinding& binding,std::string* error,const modern_field_metadata::ReadContext* context){
  auto fail=[&](const char* text)->std::unique_ptr<ResolutionInvalidationPlan>{if(error)*error=text;return nullptr;};
  if(!binding.hooks||!binding.method_model||!binding.storage||!binding.storage->Calibrated()||
      binding.storage->StorageModel()!=metadata.StorageModel()||binding.method_model->identities.size()<3||
      (binding.method_model->model!=method_identity::Model::kMethod11&&binding.method_model->model!=method_identity::Model::kMethod12Onward))
    return fail("Resolution invalidation lacks independent native Class/loader/member provenance");
  auto observer=std::make_shared<NativeResolutionObserver>(metadata,*binding.method_model,*binding.hooks,context);
  if(!observer->Available())return fail("Resolution invalidation lacks named native identity services");
  if(!observer->PrepareHolder(source.mirror,error))return nullptr;
  std::vector<Replacement> pairs;
  for(size_t p=0;p<source.old_classes.size();++p){Replacement pair;
    if(!observer->ReadClass(source.old_classes[p],&pair.before)||!observer->ReadClass(source.new_classes[p],&pair.after))
      return fail("Resolution invalidation cannot prove transaction Class roots");
    native_method_table::View methods;
    if(!native_method_table::Read(metadata,*binding.method_model,pair.after.klass,&methods,error,context))return nullptr;
    for(uint32_t i=0;i<methods.methods.boundaries.declared_count;++i){void* method=methods.methods.At(i);
      method_metadata::Identity identity;const auto& layout=binding.method_model->model==method_identity::Model::kMethod11?method_metadata::kAndroid11:method_metadata::kAndroid12Onward;
      if(!method_metadata::ReadIdentity(layout,method,layout.size,&identity))return fail("Resolution invalidation unreadable replacement method");
      if(!(identity.access_flags&(0x8u|0x2u)))continue;
      auto member=std::find_if(methods.declarations.methods.begin(),methods.declarations.methods.end(),
          [&](const evolution::Member& m){return m.dex_index==identity.dex_method_index;});
      MemberIdentity observed;
      if(member==methods.declarations.methods.end()||!observer->ReadMember(method,Kind::Method,&observed)||observed.declaring!=pair.after.klass)
        return fail("Resolution invalidation direct method lacks complete DEX/native declaring identity");
      Declaration declaration;static_cast<MemberIdentity&>(declaration)=observed;declaration.kind=Kind::Method;
      declaration.name=member->name;declaration.descriptor=member->descriptor;pair.declarations.push_back(std::move(declaration));
    }
    for(bool stat:{false,true}){field_array::View fields;if(!metadata.Fields(pair.after.klass,stat,&fields,error,context))return nullptr;
      for(uint32_t i=0;i<fields.selected_count;++i){void* field=fields.AtSelected(i);field_copy::CopyFieldView text;MemberIdentity observed;
        if(!metadata.FieldIdentity(field,&text,error,context)||!observer->ReadMember(field,Kind::Field,&observed)||observed.declaring!=pair.after.klass)
          return fail("Resolution invalidation field lacks complete native identity");
        Declaration declaration;static_cast<MemberIdentity&>(declaration)=observed;declaration.kind=Kind::Field;
        declaration.name=text.name;declaration.descriptor=text.descriptor;pair.declarations.push_back(std::move(declaration));
      }
    }
    pairs.push_back(std::move(pair));
  }
  return ResolutionInvalidationPlan::Prepare(source.mirror,pairs,std::move(observer),error);
}
}
std::unique_ptr<Plan> Plan::Prepare(const source_commit::Plan& source,
    const modern_field_metadata::FieldMetadata& metadata,
    const std::vector<const modern_methods::Service*>& methods,Retirement retirement,std::string* error,HandleGuards guards,ResolutionBinding resolution,const std::vector<native_schema_policy::Plan>* schemas,const native_schema_policy::GuardVerifier* schema_guards,const modern_field_metadata::ReadContext* context) {
  const auto fail=[&](const char* text)->std::unique_ptr<Plan>{if(error)*error=text;return nullptr;};
  if(!metadata.Calibrated()||source.old_classes.empty()||source.old_classes.size()!=source.new_classes.size()||
      methods.size()!=source.old_classes.size()||!retirement.owner||!retirement.method_retired||!retirement.retire_method)
    return fail("Missing rooted structural mapping and persistent method guards");
  if(schemas&&(!resolution.method_model||!native_schema_policy::ValidateNative(source,metadata,*resolution.method_model,methods,*schemas,schema_guards,error,context)))return nullptr;
  auto result=std::unique_ptr<Plan>(new Plan);result->read_context_=context;result->retirement_=retirement;result->handle_guards_=guards;bool additions=false;
  for(size_t p=0;p<source.old_classes.size();++p){
    auto* service=methods[p];if(!service)return fail("Missing prepared native method pair");
    void* old=source.old_classes[p];void* replacement=source.new_classes[p];
    for(bool stat:{false,true}){
      field_array::View old_fields,new_fields;
      if(!metadata.Fields(old,stat,&old_fields,error,context)||!metadata.Fields(replacement,stat,&new_fields,error,context))return nullptr;
      auto before=metadata.Class(old,stat,context),after=metadata.Class(replacement,stat,context);
      if(!before.descriptor||!after.descriptor)return fail("Unreadable structural Class identity");
      for(size_t j=0;j<after.field_count;++j){auto candidate=metadata.Field(replacement,stat,j,context);if(!candidate.field||!candidate.name||!candidate.descriptor)return fail("Unreadable replacement field identity");bool existed=false;
        for(size_t i=0;i<before.field_count;++i){auto field=metadata.Field(old,stat,i,context);if(!field.field||!field.name||!field.descriptor)return fail("Unreadable source field identity");
          existed|=!std::strcmp(field.name,candidate.name)&&!std::strcmp(field.descriptor,candidate.descriptor);}
        additions|=!existed;
      }
      for(size_t i=0;i<before.field_count;++i){
        auto field=metadata.Field(old,stat,i,context);void* next=nullptr;
        if(!field.field||!field.name||!field.descriptor)return fail("Unreadable source field identity");
        for(size_t j=0;j<after.field_count;++j){auto candidate=metadata.Field(replacement,stat,j,context);
          if(!candidate.field||!candidate.name||!candidate.descriptor)return fail("Unreadable replacement field identity");
          if(!std::strcmp(field.name,candidate.name)&&!std::strcmp(field.descriptor,candidate.descriptor)){next=candidate.field;break;}}
        if(!next&&(!retirement.field_retired||!retirement.retire_field))
          return fail("Field deletion requires the persistent calibrated field guards");
        result->fields_[field.field]=next;
      }
    }
    for(size_t j=0;j<service->Count(replacement);++j){void* candidate=service->At(replacement,j);bool existed=false;
      if(service->DeclaringClass(candidate)!=replacement)continue;
      for(size_t i=0;i<service->Count(old);++i)existed|=service->SameIdentity(service->At(old,i),candidate)&&((service->Flags(service->At(old,i))^service->Flags(candidate))&8u)==0;
      additions|=!existed;
    }
    for(size_t i=0;i<service->Count(old);++i){
      void* method=service->At(old,i);void* next=nullptr;
      for(size_t j=0;j<service->Count(replacement);++j){void* candidate=service->At(replacement,j);
        if(!service->SameIdentity(method,candidate))continue;
        if((service->Flags(method)^service->Flags(candidate))&8u){if(!schemas)return fail("Method storage changes require complete native schema policy");continue;}
        if(!schemas&&((service->Flags(method)^service->Flags(candidate))&0x11fu))
          return fail("Method modifiers changed outside the owned deletion transaction");
        next=candidate;break;
      }
      if(!next&&(service->Flags(method)&0x10000u))
        return fail("Constructor/class-initializer deletion requires independent dispatch guards");
      // Copied defaults belong to their interface; deleting a class member must
      // never retire that shared interface identity.
      if(!next&&service->DeclaringClass(method)!=old)continue;
      result->methods_[method]=next;
    }
  }
  if(resolution.hooks||resolution.method_model||resolution.storage){
    result->resolution_=PrepareResolution(source,metadata,resolution,error,context);if(!result->resolution_)return nullptr;
  }else if(additions)return fail("Added declarations require complete native resolution invalidation provenance");
  if(error)error->clear();return result;
}
bool Plan::HasDeletedMethods()const{for(const auto& entry:methods_)if(!entry.second)return true;return false;}
bool Plan::HasDeletedFields()const{for(const auto& entry:fields_)if(!entry.second)return true;return false;}
bool Service::Bind(HookEngine& hooks,void* runtime,const executable_mirror::Layout&,std::string* error){
  if(!runtime){if(error)*error="Missing independently observed Runtime receiver";return false;}
  void* visit=hooks.FindSymbol("libart.so","_ZN3art7Runtime22VisitReflectiveTargetsEPNS_22ReflectiveValueVisitorE");
  if(!visit){if(error)*error="Missing named structural reflective visitor";return false;}
  runtime_=runtime;visit_=visit;return true;
}
bool Service::Validate(const Plan& plan,std::string* error,bool allocation_free)const{
  if(!runtime_||!visit_){if(error)*error="Unbound structural reflective visitor";return false;}
  std::optional<method_identity::ScopedReadableMappings> mapped_scope;if(!allocation_free&&!plan.read_context_)mapped_scope.emplace();
  const auto& guards=plan.handle_guards_;
  Visitor visitor(plan.methods_,plan.fields_,true,
      guards.method_handles&&guards.method_handles(guards.owner),
      guards.field_var_handles&&guards.field_var_handles(guards.owner),
      guards.field_method_handles&&guards.field_method_handles(guards.owner),plan.resolution_.get(),allocation_free);reinterpret_cast<Visit>(visit_)(runtime_,&visitor);
  if(visitor.Unsupported()){
    if(error){
      if(visitor.ResolutionUnavailable()){const auto& failure=visitor.ResolutionFailure();char details[512];
        std::snprintf(details,sizeof(details),"Native resolution invalidation unavailable: source=%u kind=%s member=%p declaring=%p flags=0x%x stage=%s observer=%s",
          failure.source_kind,failure.kind==resolution_invalidation::Kind::Method?"method":"field",failure.member,failure.declaring,failure.flags,
          failure.stage?failure.stage:"unknown",failure.observer_reason?failure.observer_reason:"identity-mismatch");*error=details;
      }else *error="Deleted members referenced by unsupported MethodHandle or VarHandle";
    }
    return false;
  }
  if(error)error->clear();return true;
}
void Service::Retire(const Plan& plan)const{
  for(const auto& entry:plan.methods_)if(!entry.second)plan.retirement_.retire_method(plan.retirement_.owner,entry.first);
  for(const auto& entry:plan.fields_)if(!entry.second)plan.retirement_.retire_field(plan.retirement_.owner,entry.first);
}
void Service::Update(const Plan& plan,bool allocation_free)const{
  if(!runtime_||!visit_)std::abort();
  std::optional<method_identity::ScopedReadableMappings> mapped_scope;if(!allocation_free&&!plan.read_context_)mapped_scope.emplace();
  Visitor visitor(plan.methods_,plan.fields_,false,false,false,false,plan.resolution_.get(),allocation_free);reinterpret_cast<Visit>(visit_)(runtime_,&visitor);
}
}
