#pragma once
#include "platform/common/source_commit.h"
#include "platform/common/owned_structural_transaction.h"
#include <jvmti.h>
namespace artdcevm::native_owned_removal {
// Provider bootstrap owns all dependencies/retirement registries for the entire
// synchronous call. Preparation receives actual ART-rooted old/new objects.
struct Provider {
  bool (*ready)(void*)=nullptr;
  std::unique_ptr<source_commit::Services> (*prepare)(
      const source_commit::Plan&,void*,std::string*)=nullptr;
  const modern_field_metadata::FieldMetadata* metadata=nullptr;
  void* state=nullptr;
  bool allow_field_removal=false;
};
// Narrow source entry: only removes existing members with unchanged hierarchy
// and modifiers. Original CheckClass/CheckRedefinable still run. Does not grant
// general additions/modifier/hierarchy evolution or select an ABI from the SDK.
bool Install(HookEngine&,std::string* error);
jvmtiError Run(HookEngine&,JNIEnv*,jvmtiEnv*,jclass owned_target,
    const std::vector<unsigned char>& incoming,const Provider&,std::string* error);
}
