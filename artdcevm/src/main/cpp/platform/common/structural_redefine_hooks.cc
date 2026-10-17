#include "platform/common/structural_redefine_internal.h"

#include <android/log.h>
#include "platform/common/art_dex_view.h"
#include "platform/common/art_hook.h"
#include "platform/common/class_layout_table.h"
#include "platform/common/fault_injection.h"
#include "platform/common/interface_dispatch.h"
#include "platform/common/member_mapping.h"
#include "platform/common/runtime_thread.h"

namespace artdcevm::structural_redefine::detail {
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
  // The snapshot carries the interface group's facts, the mapping rejects new
  // default conflicts, and retirement roots cover the group: an interface
  // target may evolve its members whenever the engine owns the mapping.
  abilities.interface_members = abilities.member_mapping;
  abilities.interface_abstract_methods =
      (snapshot.live.access & evolution::kAccInterface) && abilities.member_mapping &&
      interface_dispatch::Available(*thread, self, (interfaces && interfaces->AbstractLinkErrorsAvailable()));
  if (!abilities.interface_abstract_methods)
    abilities.interface_abstract_unavailable_reason = interface_dispatch::State();
  auto decision = structural_policy::Evaluate(snapshot.live, incoming, snapshot.hierarchy,
                                              abilities, snapshot.retired);
  const bool interface_target = (interfaces && interfaces->IsTarget(self));
  if (interface_target) {
    if (decision.error == JVMTI_ERROR_NONE && !abilities.member_mapping) {
      decision.error = JVMTI_ERROR_NOT_AVAILABLE;
      decision.reason = "interface redefinition requires verified member mapping";
    }
    // Even a body edit must relink copied default methods in implementation classes.
    decision.methods_structural = true;
  }
  // A structural ancestor of an interface group's subtypes joins that group's
  // rebuild, which links every member under its unchanged superclass.
  if (decision.error == JVMTI_ERROR_NONE && decision.superclass_changed &&
      (interfaces && interfaces->SharesImplementationGroup(self))) {
    decision.error = JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED;
    decision.reason = "superclass change overlaps the interface implementation group";
  }
  if (decision.error != JVMTI_ERROR_NONE) {
    self->driver->result = decision.error;
    Diagnose(incoming.descriptor + ": " + decision.reason, true);
    return false;
  }
  // Hand the accepted decision to the hooks on ART's transaction. When ART's
  // own mapping cannot commit the change, a later probe failure in this
  // transaction must reject it before ART's no-failure point.
  transaction::Requirements& requirements = context->targets[self];
  requirements.descriptor = incoming.descriptor;
  requirements.member_mapping = interface_target || decision.requires_member_mapping;
  if (decision.requires_no_instances) batch->no_instances.insert(self);
  else batch->no_instances.erase(self);
  for (const auto& [name, descriptor] : decision.interface_obligations)
    interface_dispatch::Require(self, name, descriptor);
  for (const auto& [declaring, key] : decision.parent_obligations)
    interface_dispatch::Require(self, key.first, key.second, snapshot.parent_interfaces.at(declaring));
  // The policy rejects a superclass change related to another batch target,
  // so an interface group member never also changes its superclass.
  requirements.superclass = decision.superclass_changed ? snapshot.superclass_class : nullptr;
  requirements.redirect_verification = decision.superclass_changed || snapshot.moved_ancestor ||
      (interfaces && interfaces->IsAffected(self));
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
    if (void* superclass = class_layout::DecodeClass(process->layout_services.mirror_class, self, target->superclass))
      *const_cast<uint32_t*>(super_class.reference) = class_layout::Reference(superclass);
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
  if (batch && batch->subclass_loaded && batch->subclass_loaded->load())
    return context->Reject(self, JVMTI_ERROR_INTERNAL,
        named("a subtype of a redefined class was loaded during the transaction"));
  using Function = bool (*)(ClassRedefinitionPrefix*, RedefinitionDataIter*);
  const auto collect = reinterpret_cast<Function>(process->original_instances);
  if (!collect(self, data)) return false;
  if (interfaces && !interfaces->CompleteInstances(self, data, collect)) return false;
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
    std::string rejection;
    if ((interfaces && !interfaces->Check(*thread, self, *data, &rejection)) ||
        !member_mapping::CheckDefaultConflicts(*data, &rejection))
      return context->Reject(self, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_ADDED,
                             std::move(rejection));
    class_layout::Select(selected);
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
