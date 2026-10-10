#include "platform/android13/structural_commit.h"
#include "platform/common/source_commit.h"
#include "platform/abi/arm64/android13/allocation_validation_abi.h"
#include "platform/android13/field_copy_binding.h"
#include "platform/android13/heap_reference.h"
#include "platform/android13/handle_root_scope.h"
#include "platform/android13/hierarchy_validation.h"
#include "platform/common/object_replacement_map.h"
#include "platform/android13/runtime_access.h"
#include "platform/android13/selective_deopt.h"
#include "platform/abi/arm64/android13/selective_deopt_abi.h"
#include "platform/android13/structural_method_updates.h"
#include "platform/android13/structural_reflective.h"
#include "platform/abi/arm64/android13/validation_metadata_abi.h"
#include "platform/symbols/android13/source_symbols.h"
#include <cstring>
#include <cstdlib>
#include "platform/android13/transaction_metadata.h"
namespace artdcevm::android13 {
namespace {
void** singleton;void** list_lock;
void (*set_status)(void*,uint8_t);
void (*replace)(void*,const void*);
void (*retain)(void*,void*);
void (*invalidate)(void*);
void (*deopt_all)(void*,const char*);
void (*lock)(void*,void*);void (*unlock)(void*,void*);
void (*for_each)(void*,void (*)(void*,void*),void*);
void (*clear_cache)(void*,void*);
constexpr size_t kInterpreterCacheOffset=2608; // Fixed matched release source declaration.
template<class T> T Read(const void* p,size_t offset=0) { T value;std::memcpy(&value,static_cast<const unsigned char*>(p)+offset,sizeof(value));return value; }
void Require(bool value) { if(!value) std::abort(); }
uint32_t Flags(void* c) {return Read<uint32_t>(c,offsetof(validation_abi::ClassPrefix,access_flags));}
void* At(void* a,size_t i) {return ReadHeapReference(a,12+4*i);}
std::vector<void*> Elements(void* a) {
  Require(a!=nullptr);int32_t n=Read<int32_t>(a,8);Require(n>=0);
  std::vector<void*> result;result.reserve(n);
  for(int32_t i=0;i<n;++i) {void* item=At(a,i);Require(item!=nullptr);result.push_back(item);}
  return result;
}
void ClearThread(void* thread,void*) {clear_cache(static_cast<unsigned char*>(thread)+kInterpreterCacheOffset,thread);}
}
void CommitEvolutionStructurally(void*,const void* raw) {
  TransactionRootView transaction;std::string metadata_error;
  Require(GetTransactionMetadataReader().ReadRoots(raw,&transaction,&metadata_error));
  Require(GetTransactionMetadataReader().CheckCommitTypes(transaction,&metadata_error));
  StructuralCommitRoots roots{transaction.mirror,transaction.old_instances,transaction.new_instances,transaction.old_classes,transaction.new_classes};
  auto olds=Elements(roots.old_classes),news=Elements(roots.new_classes);
  auto old_instances=Elements(roots.old_instances),new_instances=Elements(roots.new_instances);
  Require(olds.size()==news.size() && old_instances.size()==new_instances.size());
  for(void* klass:olds) { uint32_t status=Read<uint32_t>(klass,offsetof(validation_abi::ClassPrefix,status))>>28;Require(status!=2 && status!=3); }
  auto mappings=BuildStructuralMemberMappings(olds,news);
  std::vector<std::pair<void*,void*>> replacements;replacements.reserve(olds.size()+old_instances.size());
  for(size_t i=0;i<olds.size();++i) replacements.emplace_back(olds[i],news[i]);
  bool abstract_instances=false;
  for(size_t i=0;i<old_instances.size();++i) {
    replacements.emplace_back(old_instances[i],new_instances[i]);
    void* old_type=ReadHeapReference(old_instances[i],0);
    bool found=false;
    for(size_t j=0;j<olds.size();++j) if(old_type==olds[j]) {
      Require(ReadHeapReference(new_instances[i],0)==news[j]);found=true;
      abstract_instances|=(Flags(news[j])&0x400) && !(Flags(olds[j])&0x400);break;
    }
    Require(found);
  }
  object_replacement::ObjectReplacementMap map(replacements);std::string error;
  Require(object_replacement::VerifyObjectReplacementMap(map,&error));
  class NativeCommit final:public source_commit::Services {
   public:
    NativeCommit(const StructuralMemberMappings& mappings,const object_replacement::ObjectReplacementMap& map)
        :mappings_(mappings),map_(map){}
    bool Validate(const source_commit::Plan&)const override{return true;}
    uint32_t Status(void* klass)const override {
      return __atomic_load_n(reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(klass)+offsetof(validation_abi::ClassPrefix,status)),__ATOMIC_SEQ_CST)>>28;
    }
    void SetStatus(void* klass,uint32_t status)const override{set_status(klass,static_cast<uint8_t>(status));}
    void CopyFields(bool statik,void* target,void* target_class,void* source,void* source_class)const override {
      CopyEvolutionFields(statik,target,target_class,source,source_class);
    }
    void* ObjectClass(void* object)const override{return ReadHeapReference(object,0);}
    void MarkClassAndMethodsObsolete(void* old,void* replacement)const override {
      auto* flags=reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(old)+offsetof(validation_abi::ClassPrefix,access_flags));
      *flags|=0x200000;UpdateStructuralMethods(old,replacement);
    }
    void UpdateReflectiveReferences(const source_commit::Plan& plan)const override {
      VisitStructuralReflectiveTargets(*singleton,plan.mirror,plan.old_classes,plan.new_classes,mappings_,HasHierarchyChange());
    }
    void ForceDeoptimizeFrames()const override{DeoptimizeApplicationFrames();}
    void ReplaceObjectReferences()const override{replace(CurrentHierarchyThread(),map_.BorrowedMap());}
    void* NewClassAfterReplacement(void* root,size_t index)const override{return At(root,index);}
    void RetainOldClass(void* replacement,void* old)const override {
      retain(ReadHeapReference(replacement,offsetof(validation_abi::ClassPrefix,ext_data)),old);
    }
    void InvalidateCompiledCode()const override{if(void* cache=RuntimeJitCodeCache())invalidate(cache);}
    void DeoptimizeEverythingForAbstractTransition()const override {
      deopt_all(RuntimeInstrumentation(),"artdcevm-class-abstract");
    }
    void ClearInterpreterCaches()const override {
      void* self=CurrentHierarchyThread();lock(*list_lock,self);
      for_each(Read<void*>(*singleton,offsetof(runtime_access_abi::RuntimePrefix,thread_list)),ClearThread,nullptr);unlock(*list_lock,self);
    }
   private:
    const StructuralMemberMappings& mappings_;const object_replacement::ObjectReplacementMap& map_;
  } services(mappings,map);
  source_commit::Plan plan{roots.mirror,roots.new_classes,olds,news,old_instances,new_instances,abstract_instances};
  Require(source_commit::Run(plan,services));
}
bool ConfigureStructuralCommit(HookEngine& hooks,std::string* error) {
#define ART(dst,name) dst=reinterpret_cast<decltype(dst)>(hooks.FindSymbol("libart.so",name))
  ART(singleton,"_ZN3art7Runtime9instance_E");ART(list_lock,"_ZN3art5Locks17thread_list_lock_E");
  ART(set_status,"_ZN3art6mirror5Class15SetStatusLockedENS_11ClassStatusE");
  ART(retain,"_ZN3art6mirror8ClassExt16SetObsoleteClassENS_6ObjPtrINS0_5ClassEEE");
  ART(invalidate,::artdcevm::symbols::android13::kInvalidateAllCompiledCode);
  ART(deopt_all,"_ZN3art15instrumentation15Instrumentation20DeoptimizeEverythingEPKc");
  ART(lock,"_ZN3art5Mutex13ExclusiveLockEPNS_6ThreadE");ART(unlock,"_ZN3art5Mutex15ExclusiveUnlockEPNS_6ThreadE");
  ART(for_each,"_ZN3art10ThreadList7ForEachEPFvPNS_6ThreadEPvES3_");ART(clear_cache,"_ZN3art16InterpreterCache5ClearEPNS_6ThreadE");
#undef ART
  replace=reinterpret_cast<decltype(replace)>(hooks.FindSymbol("libopenjdkjvmti.so","_ZN12openjdkjvmti14HeapExtensions17ReplaceReferencesEPN3art6ThreadERKNSt3__113unordered_mapINS1_6ObjPtrINS1_6mirror6ObjectEEES9_NS1_10HashObjPtrENS4_8equal_toIS9_EENS4_9allocatorINS4_4pairIKS9_S9_EEEEEE"));
  if(!singleton || !list_lock || !set_status || !replace || !retain || !invalidate || !deopt_all || !lock || !unlock || !for_each || !clear_cache) {
    *error="Missing named structural commit functions";return false;
  }
  return true;
}
}
