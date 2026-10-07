#pragma once
#include "api/hook_engine.h"
#include "platform/android13/device_profile.h"
#include "platform/android13/allocation_validation_abi.h"
#include "platform/android13/hierarchy_allocation_abi.h"
#include <string>
namespace artdcevm::android13 {
struct TransactionHolderView {
 const void* holder;void* array;size_t count;
 hierarchy_allocation_abi::Vector owners;
 allocation_abi::BitVector initialized,actually_structural,initial_structural;
};
struct TransactionRootView {
 TransactionHolderView holder;size_t index;void* owner;
 bool initialized,actually_structural,initial_structural;
 void* cache;void* mirror;void* old_instances;void* new_instances;void* old_classes;void* new_classes;
};
struct MethodStorageView {void* array;uint32_t count;uint16_t direct_count,copied_start;};
// Fixed declarations are gated by the matched device profile and named helpers.
// Each reader still validates the transaction's live ranges and object shapes.
// All readers run under ART mutator access. Heap pointers are borrowed only
// until a suspension; callers must publish roots before suspension-capable calls.
class TransactionMetadataReader {
 public:
 bool Configured() const { return root_ != nullptr; }
 bool Configure(HookEngine&,const DeviceProfile&,std::string*);
 bool ReadHolder(const void*,TransactionHolderView*,std::string*) const;
 bool ReadRoots(const void* iterator,TransactionRootView*,std::string*) const;
 bool ReadCache(void* cache,hierarchy_allocation_abi::Cache*,std::string*) const;
 bool ReadMethods(void* klass,MethodStorageView*,std::string*) const;
 bool ReadAbstractVTable(void* klass,bool* has_abstract,std::string*) const;
 bool CheckCommitTypes(const TransactionRootView&,std::string*) const;
 private:
 void* (*root_)(void*)=nullptr;
};
TransactionMetadataReader& GetTransactionMetadataReader();
bool ConfigureTransactionMetadataReader(HookEngine&,const DeviceProfile&,std::string*);
}
