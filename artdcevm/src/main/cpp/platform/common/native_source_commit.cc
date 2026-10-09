#include "platform/common/native_source_commit.h"
#include "platform/common/class_metadata_abi.h"
#include "platform/common/method_identity_calibration.h"
#include <cstdlib>
namespace artdcevm::native_source_commit {
namespace {
constexpr size_t kExtData=offsetof(class_metadata::Class11,ext_data);
static_assert(kExtData==offsetof(class_metadata::Class17,ext_data));
bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
void Require(bool value){if(!value)std::abort();}
bool Mapped(const void* p,size_t need){size_t extent=0;return p&&method_identity::ReadableMappingExtent(p,&extent)&&extent>=need;}
uint32_t* Scalar(void* p,size_t at){return reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(p)+at);}
}
std::unique_ptr<Services> Services::Prepare(HookEngine& hooks,const Dependencies& dependencies,
    const source_commit::Plan& plan,std::vector<MethodPair> methods,std::string* error){
  auto result=std::unique_ptr<Services>(new Services);
  result->dependencies_=dependencies;result->plan_=&plan;result->methods_=std::move(methods);
  if(dependencies.metadata&&dependencies.metadata->Calibrated())
    result->layout_=dependencies.metadata->StorageModel()==modern_field_metadata::Model::kSplit?
        &class_metadata::kLegacy:&class_metadata::k17;
  if((dependencies.native_roots!=NativeRootPolicy::kPreserve&&
      dependencies.native_roots!=NativeRootPolicy::kRestore)||!dependencies.metadata||!dependencies.metadata->Calibrated()||
     (dependencies.native_roots==NativeRootPolicy::kPreserve&&
      dependencies.metadata->StorageModel()!=modern_field_metadata::Model::kSplit)||
     !dependencies.fields||!dependencies.fields->Calibrated()||
     dependencies.fields->StorageModel()!=dependencies.metadata->StorageModel()||
     !dependencies.caches||!dependencies.caches->PreparedForCurrentThread()||
     !dependencies.observed.runtime||!dependencies.observed.instrumentation||
     !dependencies.observed.receivers.thread_list||
     (bool(dependencies.observed.receivers.jit)!=bool(dependencies.observed.receivers.jit_code_cache))||
     (!dependencies.observed.receivers.jit&&!dependencies.jit_absence_proven)||
     !dependencies.reflective.prepared||!dependencies.reflective.update){
    Fail(error,"Native source commit requires independently calibrated native services");return nullptr;
  }
#define ART(member,symbol) result->member=reinterpret_cast<decltype(result->member)>(hooks.FindSymbol("libart.so",symbol))
  ART(set_status_,"_ZN3art6mirror5Class15SetStatusLockedENS_11ClassStatusE");
  ART(retain_,"_ZN3art6mirror8ClassExt16SetObsoleteClassENS_6ObjPtrINS0_5ClassEEE");
  ART(invalidate_,"_ZN3art3jit12JitCodeCache25InvalidateAllCompiledCodeEv");
  ART(deopt_frames_,"_ZN3art15instrumentation15Instrumentation25DeoptimizeAllThreadFramesEv");
  ART(deopt_everything_,"_ZN3art15instrumentation15Instrumentation20DeoptimizeEverythingEPKc");
#undef ART
  auto current=reinterpret_cast<void*(*)()>(hooks.FindSymbol("libart.so","_ZN3art6Thread14CurrentFromGdbEv"));
  result->replace_=reinterpret_cast<decltype(result->replace_)>(hooks.FindSymbol("libopenjdkjvmti.so",
      "_ZN12openjdkjvmti14HeapExtensions17ReplaceReferencesEPN3art6ThreadERKNSt3__113unordered_mapINS1_6ObjPtrINS1_6mirror6ObjectEEES9_NS1_10HashObjPtrENS4_8equal_toIS9_EENS4_9allocatorINS4_4pairIKS9_S9_EEEEEE"));
  if(!current||!result->set_status_||!result->retain_||!result->invalidate_||
     !result->deopt_frames_||!result->deopt_everything_||!result->replace_){
    Fail(error,"Missing named native structural commit service");return nullptr;
  }
  result->self_=current();
  if(dependencies.prepare_field_copy){
    if(plan.old_classes.empty()||plan.old_classes.size()!=plan.new_classes.size()){Fail(error,"Prepared field migration requires complete rooted Class pairs");return nullptr;}
    modern_field_storage::ScopedAccess scope(*dependencies.fields);const auto ops=scope.Access();
    std::vector<field_copy::ClassPair> pairs;for(size_t i=0;i<plan.old_classes.size();++i)pairs.push_back({plan.old_classes[i],plan.new_classes[i]});
    for(size_t i=0;i<plan.old_classes.size();++i){CopyPair copy{plan.old_classes[i],plan.new_classes[i],{}, {}};
      if(!field_copy::PreparedPlan::Prepare(ops,true,copy.new_class,copy.old_class,&copy.statics,error,pairs,dependencies.allow_removed_ancestors)||
         !field_copy::PreparedPlan::Prepare(ops,false,copy.new_class,copy.old_class,&copy.instances,error,pairs,dependencies.allow_removed_ancestors))return nullptr;
      result->copies_.push_back(std::move(copy));}
  }
  if(!result->self_||!result->Validate(plan)){
    Fail(error,"Native structural commit plan or prepared member services disagree");return nullptr;
  }
  std::vector<std::pair<void*,void*>> entries;
  entries.reserve(plan.old_classes.size()+plan.old_instances.size());
  for(size_t i=0;i<plan.old_classes.size();++i)entries.emplace_back(plan.old_classes[i],plan.new_classes[i]);
  for(size_t i=0;i<plan.old_instances.size();++i)entries.emplace_back(plan.old_instances[i],plan.new_instances[i]);
  result->replacements_=std::make_unique<object_replacement::ObjectReplacementMap>(entries);
  if(!object_replacement::VerifyObjectReplacementMap(*result->replacements_,error))return nullptr;
  if(error)error->clear();return result;
}
bool Services::Validate(const source_commit::Plan& p)const {
  if(!layout_||!dependencies_.caches||!dependencies_.caches->PreparedForCurrentThread()||
     &p!=plan_||!Mapped(p.mirror,layout_->size)||p.old_classes.empty()||p.old_classes.size()!=p.new_classes.size()||
     p.old_instances.size()!=p.new_instances.size()||methods_.size()!=p.old_classes.size()||
     (!p.class_root_origins.empty()&&p.class_root_origins.size()!=p.new_classes.size())||
     !dependencies_.reflective.prepared(p,dependencies_.reflective.state))return false;
  if(dependencies_.prepare_field_copy){
    if(copies_.size()!=p.old_classes.size())return false;
    modern_field_storage::ScopedAccess scope(*dependencies_.fields);const auto ops=scope.Access();
    for(size_t i=0;i<copies_.size();++i){const auto& copy=copies_[i];
      if(copy.old_class!=p.old_classes[i]||copy.new_class!=p.new_classes[i]||!copy.statics.Validate(ops)||!copy.instances.Validate(ops))return false;}
  }
  const auto& metadata=*dependencies_.metadata;
  void* meta=metadata.Reference(p.mirror,0);
  if(!meta)return false;
  for(size_t i=0;i<p.old_classes.size();++i){
    const auto origin=p.class_root_origins.empty()?source_commit::ClassRootOrigin{p.new_classes_root,i}:p.class_root_origins[i];
    if(!Mapped(origin.array,12)||origin.index>(SIZE_MAX-12)/4)return false;
    int32_t length=0;std::memcpy(&length,static_cast<const unsigned char*>(origin.array)+8,4);
    if(length<0||origin.index>=size_t(length)||!Mapped(origin.array,12+4*size_t(length))||
       dependencies_.metadata->Reference(origin.array,12+4*origin.index)!=p.new_classes[i])return false;
    void* old=p.old_classes[i];void* next=p.new_classes[i];const auto& pair=methods_[i];
    if(!Mapped(old,layout_->size)||!Mapped(next,layout_->size)||
       metadata.Reference(old,0)!=meta||metadata.Reference(next,0)!=meta||
       !metadata.Reference(next,kExtData)||
       pair.old_class!=old||pair.new_class!=next||!pair.service||
       (dependencies_.native_roots==NativeRootPolicy::kRestore&&!pair.restore_old_roots)||Status(old)==2||Status(old)==3)return false;
    for(void* klass:{old,next})for(size_t n=0;n<pair.service->Count(klass);++n){
      void* method=pair.service->At(klass,n);
      if(!method||!pair.service->DeclaringClass(method))return false;
    }
  }
  for(size_t i=0;i<p.old_instances.size();++i){
    if(!Mapped(p.old_instances[i],8)||!Mapped(p.new_instances[i],8))return false;
    void* old_type=ObjectClass(p.old_instances[i]);bool matched=false;
    for(size_t c=0;c<p.old_classes.size();++c)if(old_type==p.old_classes[c]){
      if(ObjectClass(p.new_instances[i])!=p.new_classes[c])return false;matched=true;break;
    }
    if(!matched)return false;
  }
  return true;
}
uint32_t Services::Status(void* klass)const{return __atomic_load_n(Scalar(klass,layout_->status),__ATOMIC_SEQ_CST)>>28;}
void Services::SetStatus(void* klass,uint32_t status)const{Require(status<=15);set_status_(klass,static_cast<uint8_t>(status));}
void Services::CopyFields(bool statik,void* target,void* target_class,void* source,void* source_class)const{
  if(dependencies_.prepare_field_copy){
    for(const auto& pair:copies_)if(pair.old_class==source_class&&pair.new_class==target_class){
      modern_field_storage::ScopedAccess scope(*dependencies_.fields);(statik?pair.statics:pair.instances).CopyAndClear(scope.Access(),target,source);return;}
    Require(false);
  }
  dependencies_.fields->CopyFields(statik,target,target_class,source,source_class);
}
void* Services::ObjectClass(void* object)const{return dependencies_.fields->ObjectClass(object);}
void Services::MarkClassAndMethodsObsolete(void* old,void* next)const{
  auto* flags=Scalar(old,layout_->access_flags);__atomic_fetch_or(flags,0x200000u,__ATOMIC_RELAXED);
  for(const auto& pair:methods_)if(pair.old_class==old&&pair.new_class==next){structural_methods::Update(*pair.service,old,next);return;}
  std::abort();
}
void Services::BeginCommit(const source_commit::Plan& plan)const{
  if(dependencies_.reflective.begin_commit)
    dependencies_.reflective.begin_commit(plan,dependencies_.reflective.state);
}
void Services::UpdateReflectiveReferences(const source_commit::Plan& plan)const{dependencies_.reflective.update(plan,dependencies_.reflective.state);}
void Services::ForceDeoptimizeFrames()const{deopt_frames_(dependencies_.observed.instrumentation);}
void Services::ReplaceObjectReferences()const{Require(replacements_!=nullptr);replace_(self_,replacements_->BorrowedMap());}
void Services::RestoreDeclaringClasses(const source_commit::Plan& plan)const {
  Require(&plan==plan_);
  if(dependencies_.native_roots==NativeRootPolicy::kPreserve)return;
  for(const auto& pair:methods_) {
    Require(pair.restore_old_roots!=nullptr);
    pair.restore_old_roots(pair.service);
    dependencies_.fields->RestoreOldFieldDeclaringClass(pair.old_class);
    for(size_t i=0;i<pair.service->Count(pair.old_class);++i){
      void* method=pair.service->At(pair.old_class,i);
      const uint32_t flags=pair.service->Flags(method);
      if((flags&0x1000000u)&&!(flags&0x80000000u))continue;
      Require(pair.service->DeclaringClass(method)==pair.old_class);
    }
    for(bool stat:{false,true}){
      const auto view=dependencies_.metadata->Class(pair.old_class,stat);
      for(size_t i=0;i<view.field_count;++i){
        const auto field=dependencies_.metadata->Field(pair.old_class,stat,i);
        Require(dependencies_.metadata->FieldDeclaringClass(field.field)==pair.old_class);
      }
    }
  }
  if(dependencies_.diagnostics)dependencies_.diagnostics->declaring_roots_verified=true;
}
void* Services::NewClassAfterReplacement(void* array,size_t index)const{return dependencies_.metadata->Reference(array,12+4*index);}
void Services::RetainOldClass(void* next,void* old)const{retain_(dependencies_.metadata->Reference(next,kExtData),old);}
void Services::InvalidateCompiledCode()const{if(void* cache=dependencies_.observed.receivers.jit_code_cache)invalidate_(cache);}
void Services::DeoptimizeEverythingForAbstractTransition()const{deopt_everything_(dependencies_.observed.instrumentation,"artdcevm-class-abstract");}
void Services::ClearInterpreterCaches()const{size_t cleared=0;std::string error;Require(dependencies_.caches->ClearStoppedWorld(&cleared,&error)&&cleared);if(dependencies_.diagnostics)dependencies_.diagnostics->cleared_threads=cleared;}
}
