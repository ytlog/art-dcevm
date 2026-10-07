#include "platform/android13/structural_commit.h"
#include "platform/android13/allocation_validation_abi.h"
#include "platform/android13/field_copy_binding.h"
#include "platform/android13/heap_reference.h"
#include "platform/android13/handle_root_scope.h"
#include "platform/android13/hierarchy_validation.h"
#include "platform/android13/object_replacement_map.h"
#include "platform/android13/runtime_access.h"
#include "platform/android13/selective_deopt.h"
#include "platform/android13/selective_deopt_abi.h"
#include "platform/android13/structural_method_updates.h"
#include "platform/android13/structural_reflective.h"
#include "platform/android13/validation_metadata_abi.h"
#include "platform/symbols/android13.h"
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
  ObjectReplacementMap map(replacements);std::string error;
  Require(VerifyObjectReplacementMap(map,&error));
  // The matched release profile's ScopedAssertNoThreadSuspension is a no-op.
  // This body preserves its exclusive, no-managed-allocation transaction boundary.
  for(size_t i=0;i<olds.size();++i) {
    auto status=[&](void* c) {return __atomic_load_n(reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(c)+offsetof(validation_abi::ClassPrefix,status)),__ATOMIC_SEQ_CST)>>28;};
    uint32_t old_status=status(olds[i]);if(old_status>status(news[i])) set_status(news[i],static_cast<uint8_t>(old_status));
    CopyEvolutionFields(true,news[i],news[i],olds[i],olds[i]);
  }
  for(size_t i=0;i<old_instances.size();++i)
    CopyEvolutionFields(false,new_instances[i],ReadHeapReference(new_instances[i],0),old_instances[i],ReadHeapReference(old_instances[i],0));
  for(size_t i=0;i<olds.size();++i) {
    auto* flags=reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(olds[i])+offsetof(validation_abi::ClassPrefix,access_flags));
    *flags|=0x200000;UpdateStructuralMethods(olds[i],news[i]);
  }
  void* runtime=*singleton;
  VisitStructuralReflectiveTargets(runtime,roots.mirror,olds,news,mappings,HasHierarchyChange());
  DeoptimizeApplicationFrames();
  replace(CurrentHierarchyThread(),map.BorrowedMap());
  // ReplaceReferences rewrites holder roots. Keep the old class identities in
  // native storage until the replacement's ClassExt retains them for old frames.
  for(size_t i=0;i<olds.size();++i) {
    void* klass=At(roots.new_classes,i);
    retain(ReadHeapReference(klass,offsetof(validation_abi::ClassPrefix,ext_data)),olds[i]);
  }
  if(void* cache=RuntimeJitCodeCache()) invalidate(cache);
  if(abstract_instances) deopt_all(static_cast<unsigned char*>(runtime)+offsetof(selective_abi::RuntimePrefix,instrumentation),"artdcevm-class-abstract");
  void* self=CurrentHierarchyThread();lock(*list_lock,self);
  for_each(Read<void*>(runtime,offsetof(runtime_access_abi::RuntimePrefix,thread_list)),ClearThread,nullptr);
  unlock(*list_lock,self);
}
bool ConfigureStructuralCommit(HookEngine& hooks,std::string* error) {
#define ART(dst,name) dst=reinterpret_cast<decltype(dst)>(hooks.FindSymbol("libart.so",name))
  ART(singleton,"_ZN3art7Runtime9instance_E");ART(list_lock,"_ZN3art5Locks17thread_list_lock_E");
  ART(set_status,"_ZN3art6mirror5Class15SetStatusLockedENS_11ClassStatusE");
  ART(retain,"_ZN3art6mirror8ClassExt16SetObsoleteClassENS_6ObjPtrINS0_5ClassEEE");
  ART(invalidate,cas::kInvalidateAllCompiledCode);
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
