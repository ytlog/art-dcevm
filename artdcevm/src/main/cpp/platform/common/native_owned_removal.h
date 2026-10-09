#pragma once
#include "platform/common/source_commit.h"
#include "platform/common/owned_structural_transaction.h"
#include <jvmti.h>
#include "api/runtime_backend.h"
namespace artdcevm::native_owned_removal {
// Provider bootstrap owns all dependencies/retirement registries for the entire
// synchronous call. Preparation receives actual ART-rooted old/new objects.
enum class GraphWindow {Verified,Collected};
struct RootedPartition {
  // Borrowed JNI locals rooted by CaptureGraph's actual Holder arrays. No
  // synthetic arrays and no native pointers survive this callback.
  jobjectArray old_classes=nullptr,new_classes=nullptr;
  jobjectArray old_instances=nullptr,new_instances=nullptr;
  size_t holder_index=0,class_count=0,instance_count=0;
};
struct RootedGraph {jclass mirror=nullptr;std::vector<RootedPartition> partitions;};
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
  // Called at both original refundable source booleans: post verification
  // and completed instance collection. Borrowed actual Holder JNI locals.
  bool (*closed_graph)(JNIEnv*,jvmtiEnv*,const std::vector<jclass>&,const RootedGraph&,GraphWindow,void*,std::string*)=nullptr;
  // Requests complete source-owned policy validation. Original CheckClass and
  // CheckRedefinable remain authoritative until their separate guard adapter.
  bool schema_evolution=false;
};
// Default entry removes members with unchanged hierarchy and modifiers.
// Schema mode requires the complete rooted provider in both source windows.
// Original CheckClass/CheckRedefinable still retain their Class/hierarchy checks;
// no API number grants private ABI or unproved permission capabilities.
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
