#pragma once
#include "platform/common/structural_policy.h"
#include <jni.h>
#include <jvmti.h>

// Native-state hierarchy facts through public JNI/JVMTI only. No ART layout,
// thread service, member mapping or interface implementation dependencies.
namespace artdcevm::hierarchy_snapshot {
struct Snapshot {
  evolution::ClassSchema live;
  structural_policy::LoadedHierarchy hierarchy;
  // Global references owned by the caller's roots vector.
  std::map<std::string, jclass> interface_classes;
  // Interface targets: each declaring interface of hierarchy.parent_methods.
  std::map<std::string, jclass> parent_interfaces;
  jclass superclass_class = nullptr;
};
struct LoadedType {
  jclass klass;  // Borrowed from GetLoadedClasses; its array must outlive this index.
  bool prepared;
};
struct LoadedIndex {
  // One list per ordinary target, including the target itself, in loaded order.
  std::vector<std::vector<LoadedType>> subtypes;
  // Union for all targets, including interface targets and unprepared types.
  std::vector<jclass> affected;
};
// Scans loaded classes once, sharing JNI assignability checks between ordinary
// subtype lists and the all-status retention union without assigning identity hashes.
// Interface targets are used only for the all-status retention union; their
// policy facts continue to come from the caller's unchanged interface groups.
jvmtiError BuildIndex(jvmtiEnv* env, JNIEnv* jni, jint count, const jclass* loaded,
                      const std::vector<jclass>& ordinary_targets,
                      const std::vector<jclass>& interface_targets,
                      LoadedIndex* output, std::string* error);
jvmtiError Prepare(jvmtiEnv* env, JNIEnv* jni, const jvmtiClassDefinition& definition,
                   Snapshot* snapshot, std::vector<jobject>* roots, std::string* error);
jvmtiError CollectSubclasses(jvmtiEnv* env, JNIEnv* jni, jclass target,
                             const std::vector<LoadedType>& candidates,
                             Snapshot* snapshot, std::string* error);
jvmtiError CollectInterfaceSubclasses(jvmtiEnv* env, JNIEnv* jni, jclass target,
                                      const std::vector<jclass>& group,
                                      Snapshot* snapshot, std::string* error);
}  // namespace artdcevm::hierarchy_snapshot
