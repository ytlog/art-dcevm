#pragma once
#include <jni.h>
#include <jvmti.h>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include "platform/abi/arm64/common/reflective_value_visitor.h"
#include "platform/abi/arm64/common/runtime_objects.h"
#include "platform/common/art_hook.h"

// Retired members and the guards on every other way to reach them. A retired
// member keeps its real old ArtMethod/ArtField, retained with its obsolete
// class; the guards throw before new code can use it through JNI, reflection,
// MethodHandle or VarHandle.
namespace artdcevm::member_retirement {

// Resolve the guard hooks by name, then install the JNI Call*Method* and field
// guards through JVMTI's function-table override, which cannot be undone and
// so comes last. False with `failure` when a name is missing or the override
// fails; `hooks` is then unchanged.
bool Prepare(JavaVM* vm, jvmtiEnv* jvmti, std::vector<Hook>* hooks, std::string* failure);

// Classes whose fields stand in for retired fields in reflective Field objects
// (com.github.ytlog.artdcevm.internal.RetiredField / RetiredStaticField).
struct FieldPlaceholders {
  JNIEnv* jni;
  jclass instance_fields;
  jclass static_fields;
};
// After the hooks are installed: resolve the placeholder ArtFields. Field
// retirement stays off, with `note` explaining why, when they are unavailable.
void ResolveFieldPlaceholders(jvmtiEnv* jvmti, const FieldPlaceholders& placeholders,
                              std::string* note);
bool FieldRetirementAvailable();

// An old field without a replacement, and its placeholder kind.
struct RetiredField {
  abi::arm64::ArtField* field;
  int kind;  // A primitive descriptor's placeholder, or the reference one.
  bool is_static;
  static RetiredField Of(abi::arm64::ArtField* field, std::string_view descriptor, bool is_static);
  bool IsStaticReference() const;
};
// Commit-time registration, under ART's suspend-all.
void RetireFields(const std::vector<RetiredField>& fields);
void RetireMethod(const void* method);
// Read-only query for candidate dispatch validation before commit.
bool IsRetiredMethod(const void* method);
// After ART's commit: the guards' fast path relies on ART marking every
// retired method obsolete, and stops trusting the flag if it did not.
void ConfirmObsolete(const std::vector<const void*>& methods);

// Wraps ART's reflective-target visitor during a structural commit: clears
// resolved DexCache entries of retired members and of `cleared` copied
// methods, records JNI IDs of retired methods and fields, and remaps
// reflective objects of retired fields to their placeholders.
class Visitor final : public abi::arm64::ReflectiveValueVisitor {
 public:
  Visitor(ReflectiveValueVisitor* original, const std::set<const void*>& cleared)
      : original_(original), cleared_(cleared) {}
  abi::arm64::ArtMethod* VisitMethod(abi::arm64::ArtMethod* method,
                                     const abi::arm64::ReflectionSourceInfo& info) noexcept override;
  abi::arm64::ArtField* VisitField(abi::arm64::ArtField* field,
                                   const abi::arm64::ReflectionSourceInfo& info) noexcept override;

 private:
  ReflectiveValueVisitor* original_;
  const std::set<const void*>& cleared_;
};

// JNI globals are prepared before entering ART; ReplaceReferences updates them
// to the replacement Class whose ClassExt roots the complete obsolete chain.
void RetainRoots(std::vector<jobject> roots);
bool Rooted(JNIEnv* jni, jclass klass);
// JNI and Method.invoke calls rejected because their target was retired.
uint64_t JniRetirementGuardHits();
}  // namespace artdcevm::member_retirement
