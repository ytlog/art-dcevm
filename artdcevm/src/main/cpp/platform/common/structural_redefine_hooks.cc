#include "platform/common/structural_redefine_internal.h"

#include <android/log.h>
#include "platform/common/art_dex_view.h"
#include "platform/common/art_hook.h"
#include "platform/common/class_layout_table.h"
#include "platform/common/fault_injection.h"
#include "platform/common/imt_sealing.h"
#include "platform/common/interface_method_selection.h"
#include "platform/common/narrowed_references.h"
#include "platform/common/proxy_relink.h"
#include "platform/common/member_mapping.h"
#include "platform/common/invoke_kind.h"
#include "platform/common/runtime_thread.h"

namespace artdcevm::structural_redefine::detail {
namespace {
// Plan compiler restrictions for callers protected by invocation checks.
// Without the named guards, require updated callers in the batch. Boot code
// cannot name application classes; newly prepared classes invalidate the snapshot.
std::string KindCallers(const runtime_thread::VerifiedThread& thread, ClassRedefinitionPrefix* self,
                        const Snapshot& snapshot, const evolution::ClassSchema& incoming, Batch& batch,
                        const std::vector<std::pair<structural_policy::MemberKey, uint32_t>>& changes,
                        bool class_kind = false) {
  const std::string method = changes.front().first.first + changes.front().first.second;
  const bool interface_target = snapshot.live.access & evolution::kAccInterface;
  const auto* subtypes = interface_target ? &snapshot.interface_subtypes : snapshot.subtypes;
  if (!batch.loaded || !subtypes) return "loaded code cannot be checked for the old kind of " + method;
  std::set<std::string> classes{snapshot.live.descriptor};
  for (const auto& type : *subtypes) {
    const abi::arm64::DexFilePrefix* dex = nullptr;
    int32_t class_def = -1;
    bool boot = false;
    std::string descriptor;
    if (class_layout::ClassOrigin(thread, self, type.klass, &dex, &class_def, &boot, &descriptor))
      classes.insert(std::move(descriptor));
  }
  std::set<void*> submitted;
  for (const auto& [klass, ignored] : batch.classes)
    submitted.insert(class_layout::DecodeClass(process->layout_services.mirror_class, self, klass));
  for (const auto& [key, access] : changes) {
    bool protected_change = class_kind, private_change = false;
    for (const auto& next : incoming.methods) {
      if (next.name != key.first || next.descriptor != key.second) continue;
      private_change = !(access & (evolution::kAccStatic | evolution::kAccPrivate)) &&
          !next.IsStatic() && (next.access & evolution::kAccPrivate);
      protected_change = class_kind || ((bool(access & evolution::kAccStatic) != next.IsStatic() || private_change) &&
          invoke_kind::Enable());
    }
    // Old self frames require a probed selector that bypasses the vtable/IMT.
    if (batch.active_frame && (!protected_change || (private_change && !invoke_kind::SelfCalls(interface_target))))
      return "an active frame of " + *batch.active_frame + " may still invoke the old kind of " + method;
    transaction::Current()->protect_invocations |= protected_change;
    std::map<const abi::arm64::DexFilePrefix*, std::vector<uint32_t>> ids;
    for (jint i = 0; i < batch.loaded_count; ++i) {
      if (submitted.count(class_layout::DecodeClass(process->layout_services.mirror_class, self, batch.loaded[i]))) continue;
      const abi::arm64::DexFilePrefix* dex = nullptr;
      int32_t class_def = -1;
      bool boot = false;
      std::string caller;
      if (!class_layout::ClassOrigin(thread, self, batch.loaded[i], &dex, &class_def, &boot, &caller) || boot ||
          class_def < 0)
        continue;
      const evolution::DexView view = DexViewOf(*dex);
      std::string error;
      auto found = ids.find(dex);
      if (found == ids.end()) {
        std::vector<uint32_t> matching;
        if (!evolution::FindMethodIds(view, classes, key.first, key.second, &matching, &error))
          return "loaded DEX cannot be checked for the old kind of " + method + ": " + error;
        found = ids.emplace(dex, std::move(matching)).first;
      }
      if (found->second.empty()) continue;
      bool invokes = false;
      if (!evolution::CodeReferencesMethods(view, static_cast<uint32_t>(class_def), found->second, &invokes, &error))
        return "loaded code cannot be checked for the old kind of " + key.first + key.second + ": " + error;
      if (invokes) {
        if (!protected_change)
          return "loaded class " + caller + " still invokes the old kind of " + key.first + key.second;
        transaction::Current()->interpreted_callers.insert(batch.loaded[i]);
      }
    }
  }
  return {};
}
}  // namespace

bool CheckClass(ClassRedefinitionPrefix* self) noexcept try {
  interface_redefinition::Backend* interfaces = InterfaceBackend();
  transaction::Context* context = transaction::Current();
  Batch* batch = BatchOf(context);
  if (!batch) return reinterpret_cast<Check>(process->original_class)(self);
  batch->decisions.erase(self);
  const auto found = batch->classes.find(self->klass);
  if (found == batch->classes.end() || !self->dex_file || !self->dex_file->begin)
    return reinterpret_cast<Check>(process->original_class)(self);
  evolution::ClassSchema incoming;
  std::string error;
  if (!evolution::ReadClassDeclarations(DexViewOf(*self->dex_file), &incoming, &error)) {
    Diagnose("CheckClass DEX fallback: " + error);
    return reinterpret_cast<Check>(process->original_class)(self);
  }
  if (incoming.descriptor != found->second.live.descriptor)
    return reinterpret_cast<Check>(process->original_class)(self);
  auto& snapshot = found->second;
  structural_policy::Abilities abilities;
  const auto thread = runtime_thread::Ready();
  abilities.member_mapping = process->capabilities.member_mapping &&
      class_layout::VerifyTarget(thread, self, snapshot.live);
  for (auto& [descriptor, type] : snapshot.hierarchy.added_interfaces) {
    uint32_t access = 0;
    type.access.reset();
    if (thread && class_layout::ClassAccess(*thread, self, snapshot.interface_classes.at(descriptor), &access))
      type.access = access;
  }
  if (auto& superclass = snapshot.hierarchy.new_superclass) {
    uint32_t access = 0;
    superclass->access.reset();
    if (thread && class_layout::ClassAccess(*thread, self, snapshot.superclass_class, &access))
      superclass->access = access;
  }
  abilities.field_retirement =
      abilities.member_mapping && process->capabilities.field_retirement;
  abilities.instance_check = process->capabilities.instance_check && thread.has_value();
  abilities.superclass_change =
      abilities.member_mapping && process->capabilities.superclass_change;
  // The snapshot carries the interface group's facts and retirement roots
  // cover the group: an interface target may evolve its members whenever the
  // engine owns the mapping. As on JBR, an implementation that lacks a new
  // abstract method or newly inherits conflicting defaults links a method
  // that throws at the call.
  abilities.interface_members = abilities.member_mapping;
  abilities.interface_selection = interface_method_selection::Available();
  abilities.class_kinds = abilities.member_mapping &&
      (!((snapshot.live.access ^ incoming.access) & evolution::kAccInterface) ||
       (process->capabilities.subclass_watch && invoke_kind::ClassKinds()));
  abilities.kind_changes = abilities.member_mapping && batch->loaded;
  abilities.interface_removal = abilities.member_mapping && process->capabilities.subclass_watch &&
      imt_sealing::Available() && narrowed_references::Available();
  abilities.batch_interfaces = interfaces && interfaces->Available();
  auto decision = structural_policy::Evaluate(snapshot.live, incoming, snapshot.hierarchy,
                                              abilities, snapshot.retired);
  if (decision.error == JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED &&
      ((snapshot.live.access ^ incoming.access) & evolution::kAccInterface) && !invoke_kind::ClassKinds())
    decision.reason += std::string(": ") + invoke_kind::ClassKindUnavailable();
  if (decision.error == JVMTI_ERROR_NONE && decision.requires_instance_precheck &&
      !process->capabilities.kind_instance_check) {
    decision.error = JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED;
    decision.reason = "class kind conversion requires a public heap precheck before ART instance allocation";
  }
  const bool interface_target = (interfaces && interfaces->IsTarget(self));
  if (interface_target) {
    if (decision.error == JVMTI_ERROR_NONE && !abilities.member_mapping) {
      decision.error = JVMTI_ERROR_NOT_AVAILABLE;
      decision.reason = "interface redefinition requires verified member mapping";
    }
    // Even a body edit must relink copied default methods in implementation classes.
    decision.methods_structural = true;
  }


  // Without the source-probed batch allocator, a structural dependency of a
  // moving class must not fall back to ART's old per-entry allocation order.
  // Body-only dependencies keep their Class identity and need no new order.
  if (decision.error == JVMTI_ERROR_NONE && (!interfaces || !interfaces->Available()) &&
      (decision.fields_structural || decision.methods_structural || decision.superclass_changed)) {
    for (const auto& [klass, moving] : batch->classes) {
      if (std::find(moving.chain_in_batch.begin(), moving.chain_in_batch.end(), self->klass) ==
          moving.chain_in_batch.end()) continue;
      decision.error = JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED;
      decision.reason = "structural dependency of a superclass change requires verified batch allocation";
      break;
    }
  }

  if (decision.error == JVMTI_ERROR_NONE && !decision.kind_changes.empty()) {
    if (std::string callers = KindCallers(*thread, self, snapshot, incoming, *batch, decision.kind_changes);
        !callers.empty()) {
      decision.error = JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED;
      decision.reason = std::move(callers);
    }
  }
  if (decision.error == JVMTI_ERROR_NONE && decision.interface_kind_changed) {
    // The symbolic class/interface convention changes for every method id,
    // including inherited Object methods. Scan each old declared/inherited
    // signature with the existing caller protection path.
    std::map<structural_policy::MemberKey, uint32_t> keys;
    for (const auto& method : snapshot.live.methods)
      if (method.name != "<init>" && method.name != "<clinit>")
        keys[{method.name, method.descriptor}] = method.access;
    for (const auto& key : snapshot.hierarchy.inherited_public) keys.emplace(key, 0);
    for (const auto& key : snapshot.hierarchy.inherited_interface) keys.emplace(key, 0);
    std::vector<std::pair<structural_policy::MemberKey, uint32_t>> changes(keys.begin(), keys.end());
    bool frame_snapshot = false;
    for (const auto& [klass, prepared] : batch->classes) frame_snapshot |= prepared.changes_invoke_kind;
    if (!frame_snapshot || batch->active_frame || !invoke_kind::EnableClassKinds()) {
      decision.error = JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED;
      decision.reason = "class/interface conversion requires a Native snapshot of inactive batch frames and invocation protection";
    } else if (!changes.empty()) {
      // Class-kind guards cover unchanged methods too. Make KindCallers scan
      // and retain their callers without asking for a method-kind transition.
      context->protect_invocations = true;
      if (std::string callers = KindCallers(*thread, self, snapshot, incoming, *batch, changes, true);
          !callers.empty()) {
        decision.error = JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED;
        decision.reason = std::move(callers);
      }
    }
  }
  if (decision.error != JVMTI_ERROR_NONE) {
    self->driver->result = decision.error;
    Diagnose(incoming.descriptor + ": " + decision.reason, true);
    return false;
  }
  if (decision.interfaces_removed && !context->references) {
    // The Native phase predicted no removal, so it skipped the reference
    // capture this removal needs: fail this attempt and capture next time.
    context->capture_references_on_retry = true;
    self->driver->result = JVMTI_ERROR_INTERNAL;
    Diagnose(incoming.descriptor + ": an interface removal appeared after the Native snapshot; retrying", true);
    return false;
  }
  // Activate before ART links any replacements. This is intentionally
  // monotonic: a rejected transaction can retain dispatch protection without
  // changing the old classes or their behavior. A later structural rebuild,
  // including an interface group with no new removal, must preserve it.
  if (decision.interfaces_removed && !imt_sealing::Enable(self, &error)) {
    self->driver->result = JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED;
    Diagnose(incoming.descriptor + ": " + error, true);
    return false;
  }
  context->narrowing |= decision.interfaces_removed;
  // Hand the accepted decision to the hooks on ART's transaction. When ART's
  // own mapping cannot commit the change, a later probe failure in this
  // transaction must reject it before ART's no-failure point.
  transaction::Requirements& requirements = context->targets[self];
  requirements.descriptor = incoming.descriptor;
  requirements.member_mapping = interface_target || decision.requires_member_mapping;
  requirements.instance_precheck = decision.requires_instance_precheck;
  requirements.published_verification = decision.published_verification &&
      !(interfaces && interfaces->IsAffected(self));
  if (decision.requires_no_instances) batch->no_instances.insert(self);
  else batch->no_instances.erase(self);
  // The batch allocator resolves this root against the final replacement graph.
  requirements.superclass = decision.superclass_changed ? snapshot.superclass_class : nullptr;
  requirements.linked_interfaces.clear();
  if (snapshot.hierarchy.added_interface_in_batch)
    for (const auto& [descriptor, type] : snapshot.interface_classes) requirements.linked_interfaces.push_back(type);
  requirements.chain_rebuilds = snapshot.chain_in_batch;
  requirements.retyped_fields.clear();
  for (const auto& field : snapshot.retyped_fields)
    requirements.retyped_fields.push_back({field.name, field.is_static, field.descriptor, field.type});
  requirements.redirect_verification = decision.methods_structural || decision.superclass_changed || snapshot.moved_ancestor ||
      !requirements.linked_interfaces.empty() || (interfaces && interfaces->IsAffected(self));
  if (!decision.retired_methods.empty() || !decision.retired_fields.empty())
    batch->retires_members = true;
  snapshot.retiring = decision.retired_methods;
  batch->decisions.emplace(self, std::move(decision));
  return true;
} catch (...) {
  self->driver->result = ArtHookExceptionError();
  return false;
}

bool CheckFields(ClassRedefinitionPrefix* self) noexcept {
  if (Batch* batch = BatchOf(transaction::Current())) {
    const auto found = batch->decisions.find(self);
    if (found != batch->decisions.end()) {
      if (found->second.fields_structural) process->record_field(self);
      return true;
    }
  }
  return reinterpret_cast<Check>(process->original_fields)(self);
}

bool CheckMethods(ClassRedefinitionPrefix* self) noexcept {
  if (Batch* batch = BatchOf(transaction::Current())) {
    const auto found = batch->decisions.find(self);
    if (found != batch->decisions.end()) {
      if (found->second.methods_structural) process->record_method(self);
      batch->decisions.erase(found);
      return true;
    }
  }
  return reinterpret_cast<Check>(process->original_methods)(self);
}

// ART links the target's new class under the old superclass it passes here
// ("We know the interfaces and supers didn't change"). For a superclass change,
// store the new superclass in that handle's slot: the slot belongs to the
// caller's handle scope, so it stays a GC root. Subclasses are allocated through
// the same redefinition with the target's new class as their superclass.
void* AllocateNewClassObject(ClassRedefinitionPrefix* self, abi::arm64::Handle old_class, abi::arm64::Handle super_class,
                             abi::arm64::Handle cache, uint16_t index) noexcept {
  const transaction::Context* context = transaction::Current();
  const transaction::Requirements* target = context ? context->Find(self) : nullptr;
  if (target && target->superclass && super_class.reference &&
      old_class.Get<void>() == process->layout_services.mirror_class(self)) {
    if (void* superclass = class_layout::DecodeClass(process->layout_services.mirror_class, self, target->superclass)) {
      // A structural class of the new chain was rebuilt first: link under its replacement.
      if (context->holder)
        if (const auto thread = runtime_thread::Ready())
          if (void* rebuilt = class_layout::Rebuilt(*thread, context->holder, superclass)) superclass = rebuilt;
      *const_cast<uint32_t*>(super_class.reference) = class_layout::Reference(superclass);
    }
  }
  using Function = void* (*)(ClassRedefinitionPrefix*, abi::arm64::Handle, abi::arm64::Handle, abi::arm64::Handle, uint16_t);
  return reinterpret_cast<Function>(process->original_allocate)(self, old_class, super_class, cache, index);
}

bool CheckInstances(const runtime_thread::VerifiedThread& thread,
                    ClassRedefinitionPrefix* self, const RedefinitionDataIter& data) {
  auto* context = transaction::Current();
  for (const auto& [target, requirements] : context->targets) {
    if (!BatchOf(context)->no_instances.count(target)) continue;
    const auto* instances = data.OldInstances();
    const uint32_t reference = class_layout::Reference(process->layout_services.mirror_class(target));
    for (int32_t i = 0; instances && i < instances->length; ++i) {
      const auto* instance = class_layout::Element<abi::arm64::Object>(thread, instances, i);
      if (instance && instance->klass == reference)
        return context->Reject(self, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED,
            requirements.descriptor + ": class made abstract has live instances");
    }
  }
  return true;
}
bool CollectAndCreateNewInstances(ClassRedefinitionPrefix* self, RedefinitionDataIter* data) noexcept try {
  interface_redefinition::Backend* interfaces = InterfaceBackend();
  auto* context = transaction::Current();
  const auto* target = context ? context->Find(self) : nullptr;
  const auto named = [&](const char* reason) {
    return (target ? target->descriptor + ": " : "") + reason;
  };
  if (ARTDCEVM_FAULT_IF(3, (interfaces && interfaces->IsTarget(self)) && data->Classes(true)))
    return context->Reject(self, JVMTI_ERROR_OUT_OF_MEMORY, named("injected instance allocation failure"));
  const Batch* batch = BatchOf(context);
  if (batch && ((batch->subclass_loaded && batch->subclass_loaded->load()) ||
                ((context->narrowing || context->protect_invocations) && batch->any_class_loaded && batch->any_class_loaded->load())))
    return context->Reject(self, JVMTI_ERROR_INTERNAL,
        named("a subtype of a redefined class was loaded during the transaction"));
  // ART has paused allocations and class loading before this decision point.
  // The public JVMTI filter visits only exact instances. The callback neither
  // tags objects nor calls JNI. Run before ART allocates any replacement: an
  // interface cannot satisfy Class::Alloc's instantiability contract.
  if (context) for (const auto& [target, requirements] : context->targets) {
    if (!requirements.instance_precheck) continue;
    bool exists = false;
    jvmtiHeapCallbacks callbacks{};
    callbacks.heap_iteration_callback = [](jlong, jlong, jlong*, jint, void* data) -> jint {
      *static_cast<bool*>(data) = true;
      return JVMTI_VISIT_ABORT;
    };
    const auto result = process->snapshot_env->IterateThroughHeap(0, target->klass, &callbacks, &exists);
    if (result != JVMTI_ERROR_NONE || exists)
      return context->Reject(self, result != JVMTI_ERROR_NONE ? result : JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED,
          requirements.descriptor + (exists ? ": class kind conversion has live instances before ART allocation"
                                           : ": class kind instance precheck failed"));
  }
  using Function = bool (*)(ClassRedefinitionPrefix*, RedefinitionDataIter*);
  const auto collect = reinterpret_cast<Function>(process->original_instances);
  if (interfaces ? !interfaces->CompleteInstances(self, data, collect) : !collect(self, data)) return false;
  // Corrupt only the transaction-private holder; cleanup roots are independent.
  ARTDCEVM_TEST(
    if (ARTDCEVM_FAULT_IF(4, (interfaces && interfaces->IsTarget(self)) && data->Classes(true))) {
      fault_injection::CorruptHolder(*data);
    }
  )
  if (!context) return true;
  const auto thread = runtime_thread::Ready();
  if (thread && !CheckInstances(*thread, self, *data)) return false;
  if (!context->probed) return true;
  std::string why = thread ? "" : runtime_thread::Rejection();
  const int selected = thread ? class_layout::VerifyReplacements(*thread, self, *data, &why) : -1;
  if (selected >= 0 && member_mapping::Plan(*thread, self, *data, selected, &why)) {
    if (target && target->superclass &&
        !class_layout::SuperclassMatches(*thread, self, *data, target->superclass, selected))
      return context->Reject(self, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,
          named("the new superclass was not applied"));
    if (!proxy_relink::Plan(*thread, self, *data, selected, &why))
      return context->Reject(self, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,
                             named(("Proxy implementation cannot be relinked: " + why).c_str()));
    if (context->narrowing && !narrowed_references::Plan(*thread, self, *data, selected, &why))
      return context->Reject(self, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,
                             named(("interface reference cleanup cannot be planned: " + why).c_str()));
    // Last: every check above may still reject, and only the unpublished
    // replacements change here.
    // Replacements sit in an interface group's owner arrays, regardless of
    // which entry narrows. Once narrowing has ever been accepted, every later
    // rebuild must keep protection, including batches with no new removal.
    if (imt_sealing::Protecting() && !imt_sealing::Seal(*thread, self, *data, selected, &why))
      return context->Reject(self, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,
                             named(("interface dispatch cannot be sealed: " + why).c_str()));
    class_layout::Select(selected);
    if (invoke_kind::Protecting() && !context->collected_interpreted_callers) {
      // ART invalidates every compiled method even for an unrelated target.
      // Its counter reset can reuse PreCompiled's bit for PreviouslyWarm.
      // Reapply existing restrictions after each Update, including when the
      // caller is implicitly rebuilt. Loaded-class locals are updated by ART.
      std::set<void*> submitted;
      for (const auto& [klass, ignored] : batch->classes)
        submitted.insert(class_layout::DecodeClass(process->layout_services.mirror_class, self, klass));
      for (jint i = 0; i < batch->loaded_count; ++i) {
        jclass klass = batch->loaded[i];
        if (submitted.count(class_layout::DecodeClass(process->layout_services.mirror_class, self, klass))) continue;
        const abi::arm64::DexFilePrefix* dex = nullptr;
        int32_t class_def;
        bool boot;
        std::string descriptor;
        if (class_layout::ClassOrigin(*thread, self, klass, &dex, &class_def, &boot, &descriptor) && !boot &&
            class_layout::HasInterpretedMethods(*thread, self, klass))
          context->interpreted_callers.insert(klass);
      }
      context->collected_interpreted_callers = true;
    }
    if (ARTDCEVM_FAULT(22))
      return context->Reject(self, JVMTI_ERROR_OUT_OF_MEMORY,
                            named("injected failure after interface mapping plan"));
    return true;
  }
  if (selected >= 0) class_layout::Select(-1);
  class_layout::Report("rejected: " + why);
  if (!context->RequiresMemberMapping()) return true;
  __android_log_print(ANDROID_LOG_ERROR, "ART-DCEVM", "member mapping probe failed: %s", why.c_str());
  return context->Reject(self, JVMTI_ERROR_INTERNAL, {});
} catch (...) {
  self->driver->result = ArtHookExceptionError();
  return false;
}

}  // namespace artdcevm::structural_redefine::detail
