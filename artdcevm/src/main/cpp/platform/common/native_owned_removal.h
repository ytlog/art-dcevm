#pragma once
#include "platform/common/source_commit.h"
#include "platform/common/owned_structural_transaction.h"
#include <jvmti.h>
#include "api/runtime_backend.h"
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
  // Required for multiple targets. The production owner independently checks
  // all original DEX provenance and stages every field alias before heap entry.
  bool (*preflight)(JNIEnv*,jvmtiEnv*,const std::vector<Definition>&,void*,std::string*)=nullptr;
  // Called only at the original post-verification class-loading pause, before
  // allocations pause / irreversible publication. Borrowed JNI Class locals.
  bool (*closed_graph)(JNIEnv*,jvmtiEnv*,const std::vector<jclass>&,void*,std::string*)=nullptr;
};
// Narrow source entry: removes existing members with unchanged hierarchy
// and modifiers. Original CheckClass/CheckRedefinable still run. Does not grant
// general additions/modifier/hierarchy evolution or select an ABI from the SDK.
// A batch may include unchanged-declaration body replacements, provided at least
// one target removes a member; ART still verifies every incoming method body.
bool Install(HookEngine&,std::string* error);
bool GraphWindowReady();
// One ART extension call for the entire input. Every target is validated before
// the first mutation; structural islands share one final commit and one retire
// publication. The single-class Run below remains a compatible wrapper.
jvmtiError RunBatch(HookEngine&,JNIEnv*,jvmtiEnv*,const std::vector<Definition>&,
    const Provider&,std::string* error);
jvmtiError Run(HookEngine&,JNIEnv*,jvmtiEnv*,jclass owned_target,
    const std::vector<unsigned char>& incoming,const Provider&,std::string* error);
}
