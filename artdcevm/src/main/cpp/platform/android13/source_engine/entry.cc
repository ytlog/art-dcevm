// Compiled separately against the pinned AOSP platform headers and libc++.
// Only JNI and plain C values cross the NDK/platform C++ ABI boundary.
#include "ti_redefine.h"
#include "platform/android13/jni_method_targets_abi.h"
#include "platform/symbols/android13/xiaomi/source_engine_abi.h"
#include "hierarchy.h"
#include "retired_fields.h"
#include "retired_methods.h"
#include "art_method-inl.h"
#include "art_field-inl.h"
#include "mirror/class-inl.h"
#include "mirror/object-inl.h"
#include "scoped_thread_state_change-inl.h"
#include "runtime.h"
#include "jni/jni_id_manager.h"
#include "jni/jni_internal.h"
#include "jni/jni_env_ext.h"
#include "jni/check_jni.h"
#include "dex/dex_file_loader.h"
#include "class_linker-inl.h"
#include "mirror/iftable-inl.h"
#include "thread_list.h"
#include "stack.h"
#include "oat_quick_method_header.h"
#include "oat_file.h"
#include "jit/jit.h"
#include "jit/jit_code_cache.h"
#include "stack_map.h"
#include <atomic>
#include <cstring>
#include <unistd.h>

// Device structural redefinition is supplied by the enhancement library after
// named-symbol lookup. These aliases below name our source-compiled functions.
using StructuralRedefine = jvmtiError (*)(jvmtiEnv*, jint, const jvmtiClassDefinition*);
namespace { StructuralRedefine call_device_structural_redefine = nullptr; }
extern "C" void ArtDcevmSourceConfigureStructuralRedefine(StructuralRedefine callback) {
  call_device_structural_redefine = callback;
}
static_assert(sizeof(art::PointerSize) == sizeof(size_t));
static_assert(static_cast<size_t>(art::PointerSize::k64) == 8);
namespace {
artdcevm::android13::JniMethodTarget jni_method_target = nullptr;
}
extern "C" void ArtDcevmSourceConfigureJniMethodTargets(
    artdcevm::android13::JniMethodTarget callback) {
  jni_method_target = callback;
}
extern "C" void* ArtDcevmSourceMappedJniMethod(void* replacement) {
  return jni_method_target(replacement);
}

namespace { void (*update_structural_methods)(void*, void*) = nullptr; }
extern "C" void ArtDcevmSourceConfigureStructuralMethodUpdates(void (*callback)(void*, void*)) {
  update_structural_methods = callback;
}
extern "C" void ArtDcevmSourceUpdateStructuralMethods(void* old_class, void* new_class) {
  update_structural_methods(old_class, new_class);
}

extern "C" void SourceStructuralCommit(void*, const void*)
    __asm__(ARTDCEVM_A13_SOURCE_STRUCTURAL_COMMIT);
namespace {
std::atomic<uint64_t> guard_precommit_activations{0};
std::atomic<uint64_t> guard_failed_rollbacks{0};
std::atomic<uint64_t> guard_committed_retention{0};
std::atomic<uint64_t> compiled_precommit_marks{0};
std::atomic<uint64_t> compiled_precommit_aot_barriers{0};
std::atomic<uint64_t> compiled_precommit_barriers{0};
std::atomic<uint64_t> compiled_precommit_clean_scans{0};
std::atomic<uint64_t> compiled_precommit_unsupported{0};
std::atomic<uint64_t> compiled_precommit_boot_jit_inline{0};
bool NeedsReceiverGuard(jint count, const jvmtiClassDefinition* definitions) {
  if (count <= 0 || definitions == nullptr) return false;
  art::DexFileLoader loader;
  art::Thread* self = art::Thread::Current();
  for (jint i = 0; i < count; ++i) {
    const auto& definition = definitions[i];
    if (definition.class_byte_count <= 0 || definition.class_bytes == nullptr ||
        definition.klass == nullptr) continue;
    std::string error;
    auto dex = loader.Open(definition.class_bytes,
        static_cast<size_t>(definition.class_byte_count), "artdcevm-receiver-preflight", 0,
        nullptr, true, true, &error);
    if (!dex || dex->NumClassDefs() != 1) continue;  // ART performs final validation.
    const auto& class_def = dex->GetClassDef(0);
    if (!class_def.superclass_idx_.IsValid()) continue;
    const char* desired_name = dex->StringByTypeIdx(class_def.superclass_idx_);
    art::ScopedObjectAccess soa(self);
    auto current = soa.Decode<art::mirror::Class>(definition.klass);
    if (current == nullptr || current->GetSuperClass() == nullptr) continue;
    auto desired = art::Runtime::Current()->GetClassLinker()->LookupClass(
        self, desired_name, current->GetClassLoader());
    // Unresolved types are conservatively guarded. ART resolves and validates
    // them again in the structural transaction.
    if (desired == nullptr || !desired->IsSubClass(current->GetSuperClass())) return true;
    auto old_interfaces = current->GetIfTable();
    const auto* new_interfaces = dex->GetInterfacesList(class_def);
    for (int32_t old = 0; old < old_interfaces->Count(); ++old) {
      auto old_interface = old_interfaces->GetInterface(old);
      bool retained = old_interface->IsAssignableFrom(desired);
      if (new_interfaces != nullptr) {
        for (uint32_t next = 0; next < new_interfaces->Size(); ++next) {
          const char* name = dex->StringByTypeIdx(new_interfaces->GetTypeItem(next).type_idx_);
          auto iface = art::Runtime::Current()->GetClassLinker()->LookupClass(
              self, name, current->GetClassLoader());
          if (iface == nullptr) return true;
          retained |= old_interface->IsAssignableFrom(iface);
        }
      }
      if (!retained) return true;
    }
  }
  return false;
}
extern "C" bool ArtDcevmSourceMarkSuspendDeoptThread(art::Thread*);
extern "C" bool ArtDcevmSourceSuspendDeoptPending(art::Thread*);
extern "C" void ArtDcevmSourceCancelSuspendDeoptThread(art::Thread*);
enum class ActiveCompiledScan { kNone, kMarked, kUnsupported };
ActiveCompiledScan MarkActiveAppCompiledFrame(art::Thread** marked) {
  art::Thread* self = art::Thread::Current();
  ActiveCompiledScan result = ActiveCompiledScan::kNone;
  *marked = nullptr;
  art::ScopedThreadStateChange native(self, art::ThreadState::kNative);
  art::ScopedSuspendAll suspended("ART DCEVM active compiled precommit check");
  art::MutexLock threads(self, *art::Locks::thread_list_lock_);
  art::Runtime::Current()->GetThreadList()->ForEach([&](art::Thread* thread) {
    if (result != ActiveCompiledScan::kNone || thread == self) return;
    bool return_barrier_above = false;
    const void* previous_quick_frame = nullptr;
    auto* instrumentation_stack = thread->GetInstrumentationStack();
    art::StackVisitor::WalkStack(
        [&](const art::StackVisitor* frame) REQUIRES_SHARED(art::Locks::mutator_lock_) {
          if (frame->GetCurrentQuickFrame() == nullptr) return true;
          const void* quick_frame = frame->GetCurrentQuickFrame();
          const bool physical_frame = quick_frame != previous_quick_frame;
          previous_quick_frame = quick_frame;
          art::ArtMethod* method = frame->GetMethod();
          if (method == nullptr) return true;
          const bool has_exit_barrier = physical_frame && !method->IsRuntimeMethod() &&
              instrumentation_stack->find(frame->GetReturnPcAddr()) !=
                  instrumentation_stack->end();
          const auto* header = frame->GetCurrentOatQuickMethodHeader();
          const void* quick_pc = reinterpret_cast<const void*>(frame->GetCurrentQuickFramePc());
          auto* jit = art::Runtime::Current()->GetJit();
          auto* code_cache = jit == nullptr ? nullptr : jit->GetCodeCache();
          const bool boot_jit_inline = physical_frame && !method->IsRuntimeMethod() &&
              !method->IsNative() && method->GetDeclaringClass()->GetClassLoader() == nullptr &&
              header != nullptr && code_cache != nullptr && code_cache->ContainsPc(quick_pc) &&
              header->IsOptimized() &&
              art::CodeInfo::HasInlineInfo(header->GetOptimizedCodeInfoPtr());
          if (method->IsRuntimeMethod() || method->IsNative() || header == nullptr ||
              (method->GetDeclaringClass()->GetClassLoader() == nullptr &&
               !boot_jit_inline)) {
            return_barrier_above |= has_exit_barrier;
            return true;
          }
          if (boot_jit_inline)
            compiled_precommit_boot_jit_inline.fetch_add(1, std::memory_order_relaxed);
          const void* oat_code = method->GetOatMethodQuickCode(
              art::Runtime::Current()->GetClassLinker()->GetImagePointerSize());
          const bool aot = oat_code != nullptr && frame->GetCurrentOatQuickMethodHeader() ==
                  art::OatQuickMethodHeader::FromEntryPoint(oat_code);
          if (return_barrier_above) {
            compiled_precommit_barriers.fetch_add(1, std::memory_order_relaxed);
            if (aot) compiled_precommit_aot_barriers.fetch_add(1, std::memory_order_relaxed);
            return true;
          }
          if (boot_jit_inline && !code_cache->PrivateRegionContainsPc(quick_pc)) {
            result = ActiveCompiledScan::kUnsupported;
            compiled_precommit_unsupported.fetch_add(1, std::memory_order_relaxed);
            return false;
          }
          if (aot) {
            const art::DexFile* dex = method->GetDexFile();
            const art::OatDexFile* oat_dex = dex == nullptr ? nullptr : dex->GetOatDexFile();
            const art::OatFile* oat = oat_dex == nullptr ? nullptr : oat_dex->GetOatFile();
            if (oat == nullptr || !oat->IsDebuggable()) {
              result = ActiveCompiledScan::kUnsupported;
              compiled_precommit_unsupported.fetch_add(1, std::memory_order_relaxed);
              return false;
            }
          }
          if (!ArtDcevmSourceMarkSuspendDeoptThread(thread)) {
            result = ActiveCompiledScan::kUnsupported;
            compiled_precommit_unsupported.fetch_add(1, std::memory_order_relaxed);
          } else {
            *marked = thread;
            result = ActiveCompiledScan::kMarked;
            compiled_precommit_marks.fetch_add(1, std::memory_order_relaxed);
          }
          return_barrier_above |= has_exit_barrier;
          return false;
        }, thread, nullptr, art::StackVisitor::StackWalkKind::kIncludeInlinedFrames);
  });
  if (result == ActiveCompiledScan::kNone)
    compiled_precommit_clean_scans.fetch_add(1, std::memory_order_relaxed);
  return result;
}
bool DrainActiveAppCompiledFrames() {
  // DeoptimizeEverything redirects ordinary method entries to the interpreter.
  // Drain other threads' existing app quick frames, including JIT carriers of
  // inlined app methods, before an old compiled caller can enter AOT again.
  for (int frame = 0; frame < 64; ++frame) {
    art::Thread* marked = nullptr;
    ActiveCompiledScan scan = MarkActiveAppCompiledFrame(&marked);
    if (scan == ActiveCompiledScan::kNone) return true;
    if (scan == ActiveCompiledScan::kUnsupported) return false;
    for (int millisecond = 0; millisecond < 100 &&
        ArtDcevmSourceSuspendDeoptPending(marked); ++millisecond) usleep(1000);
    if (ArtDcevmSourceSuspendDeoptPending(marked)) {
      ArtDcevmSourceCancelSuspendDeoptThread(marked);
      return false;
    }
    usleep(1000);  // Let the target finish the non-returning deopt entrypoint.
  }
  return false;
}
}
extern "C" bool ArtDcevmSourceReceiverGuardEnabled();
extern "C" uint64_t ArtDcevmSourceCompiledPrecommitDiagnostics(int index) {
  switch (index) {
    case 0: return compiled_precommit_marks.load(std::memory_order_relaxed);
    case 1: return compiled_precommit_aot_barriers.load(std::memory_order_relaxed);
    case 2: return compiled_precommit_barriers.load(std::memory_order_relaxed);
    case 3: return compiled_precommit_clean_scans.load(std::memory_order_relaxed);
    case 4: return compiled_precommit_unsupported.load(std::memory_order_relaxed);
    case 5: return compiled_precommit_boot_jit_inline.load(std::memory_order_relaxed);
    default: return 0;
  }
}
extern "C" bool ArtDcevmSourceReceiverGuardRequired();
extern "C" void ArtDcevmSourceRequireReceiverGuard(bool);
extern "C" bool ArtDcevmSourceFieldReadProbeMode(bool, bool);
extern "C" uint64_t ArtDcevmSourceReceiverGuardTransactionDiagnostics(int index) {
  switch (index) {
    case 0: return guard_precommit_activations.load(std::memory_order_relaxed);
    case 1: return guard_failed_rollbacks.load(std::memory_order_relaxed);
    case 2: return guard_committed_retention.load(std::memory_order_relaxed);
    default: return 0;
  }
}
extern "C" __attribute__((visibility("default"))) int ArtDcevmSourceProbe(
    JNIEnv* jni, jobject object) {
  JavaVM* vm = nullptr;
  if (jni->GetJavaVM(&vm) != JNI_OK) return 1;
  auto* thread = art::Thread::Current();
  if (thread == nullptr || thread->GetJniEnv() != jni) return 2;
  if (reinterpret_cast<JavaVM*>(art::Runtime::Current()->GetJavaVM()) != vm) return 3;
  jclass java_class = jni->GetObjectClass(object);
  jmethodID compute = jni->GetMethodID(java_class, "compute", "()I");
  if (compute == nullptr || jni->ExceptionCheck()) { jni->DeleteLocalRef(java_class); return 9; }
  jfieldID value = jni->GetFieldID(java_class, "value", "I");
  jfieldID wide = jni->GetFieldID(java_class, "wide", "J");
  jni->DeleteLocalRef(java_class);
  if (value == nullptr || wide == nullptr || jni->ExceptionCheck()) return 11;
  if (jni->GetIntField(object, value) != 37 ||
      jni->GetLongField(object, wide) != 0x123456789ABCDEFLL) return 14;
  // These accesses are compiled from AOSP declarations. No binary-derived offsets.
  art::ScopedObjectAccess soa(jni);
  auto instance = soa.Decode<art::mirror::Object>(object);
  auto klass = instance->GetClass();
  std::string storage;
  if (std::strcmp(klass->GetDescriptor(&storage), "Lcom/github/ytlog/artdcevm/platform/android13/SourceAbiProbe;") != 0)
    return 4;
  if (klass->NumInstanceFields() != 2 || klass->NumStaticFields() != 1) return 5;
  auto* field = klass->FindDeclaredInstanceField("value", "I");
  if (field == nullptr || field->GetInt(instance) != 37) return 6;
  auto* wide_field = klass->FindDeclaredInstanceField("wide", "J");
  if (wide_field == nullptr || wide_field->GetLong(instance) != 0x123456789ABCDEFLL ||
      wide_field == field ||
      wide_field->GetOffset().Int32Value() == field->GetOffset().Int32Value()) return 15;
  auto* method = klass->FindClassMethod("compute", "()I", art::kRuntimePointerSize);
  if (method == nullptr || method->GetDeclaringClass() != klass) return 7;
  if (method->GetNameView() != "compute" || method->GetSignature().ToString() != "()I")
    return 8;
  if (art::jni::DecodeArtMethod(compute) != method) return 10;
  if (art::jni::DecodeArtField(value) != field) return 12;
  if (art::jni::DecodeArtField(wide) != wide_field) return 16;
  auto* counter = klass->FindDeclaredStaticField("counter", "I");
  if (counter == nullptr || counter->GetInt(klass) != 7) return 13;
  return 0;
}

jvmtiError RedefineSourceTransaction(jvmtiEnv* env, jint count,
    const jvmtiClassDefinition* definitions, bool allow_transform_hierarchy) {
  artdcevm::android13::source::HierarchyTransactionScope hierarchy_scope;
  // ART runs ClassFileLoadHook after entry. The standard API cannot know whether
  // a callback will remove an existing parent or interface, so arm the guard first.
  const bool needs_receiver_guard = allow_transform_hierarchy || NeedsReceiverGuard(count, definitions);
  const bool already_guarded = ArtDcevmSourceReceiverGuardEnabled();
  const bool previously_required = ArtDcevmSourceReceiverGuardRequired();
  if (needs_receiver_guard && !already_guarded &&
      !ArtDcevmSourceFieldReadProbeMode(true, false)) return JVMTI_ERROR_INTERNAL;
  if (needs_receiver_guard) ArtDcevmSourceRequireReceiverGuard(true);
  if (needs_receiver_guard && !already_guarded)
    guard_precommit_activations.fetch_add(1, std::memory_order_relaxed);
  // Let the device ART own allocation, Heap access, suspension and transaction setup.
  // The loader routes only the structural commit through ArtDcevmSourceCommit.
  // Drain active app compiled frames at their own suspend entrypoints before
  // ART starts mutation. Unsupported or non-cooperating frames reject the
  // transaction while all old class definitions are still intact.
  jvmtiError result = needs_receiver_guard && !DrainActiveAppCompiledFrames()
      ? JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED
      : call_device_structural_redefine(env, count, definitions);
  if (needs_receiver_guard) {
    const bool retain = result == JVMTI_ERROR_NONE &&
        artdcevm::android13::source::HasHierarchyNarrowing();
    if (!retain) {
      ArtDcevmSourceRequireReceiverGuard(previously_required);
      if (!already_guarded) {
        ArtDcevmSourceFieldReadProbeMode(false, false);
        if (result != JVMTI_ERROR_NONE)
          guard_failed_rollbacks.fetch_add(1, std::memory_order_relaxed);
      }
    } else {
      guard_committed_retention.fetch_add(1, std::memory_order_relaxed);
    }
  }
  return result;
}
extern "C" __attribute__((visibility("default"))) jvmtiError ArtDcevmSourceRedefine(
    jvmtiEnv* env, jint count, const jvmtiClassDefinition* definitions) {
  return RedefineSourceTransaction(env, count, definitions, false);
}
extern "C" __attribute__((visibility("default"))) jvmtiError ArtDcevmSourceRedefineStandard(
    jvmtiEnv* env, jint count, const jvmtiClassDefinition* definitions) {
  return RedefineSourceTransaction(env, count, definitions, true);
}

extern "C" __attribute__((visibility("default"))) void ArtDcevmSourceCommit(
    void* redefinition, const void* data) {
  SourceStructuralCommit(redefinition, data);
}


// Test-only, current-thread switch. Production guards never change CheckJNI.
extern "C" int ArtDcevmSourceCheckJniMode(JNIEnv* env, int requested) {
  auto* extended = static_cast<art::JNIEnvExt*>(env);
  // Use the public JNIEnv table pointer, not an unverified JNIEnvExt flag layout.
  int previous = env->functions == art::GetCheckJniNativeInterface() ? 1
      : env->functions == art::GetJniNativeInterface() ? 0 : -1;
  if (requested == 0 || requested == 1) extended->SetCheckJniEnabled(requested != 0);
  return previous;
}
