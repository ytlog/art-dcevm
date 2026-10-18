#include "platform/common/structural_redefine_internal.h"

#include <algorithm>
#include <array>
#include <android/log.h>
#include "shadowhook.h"
#include "platform/common/imt_sealing.h"
#include "platform/common/proxy_relink.h"
#include "platform/common/interface_dispatch.h"
#include "platform/common/member_mapping.h"
#include "platform/common/runtime_thread.h"
#include "platform/common/verification_redirect.h"
#include "platform/symbols/structural_redefine_symbols.h"

namespace artdcevm::structural_redefine {
namespace detail {
// Serializes first construction and reports before Install creates process state.
std::mutex installation_mutex;
std::unique_ptr<Process> process;

void Diagnose(const std::string& text, bool rejection) {
  {
    std::lock_guard<std::mutex> lock(process->report_mutex);
    (rejection ? process->last_rejection : process->diagnostic) = text;
  }
  __android_log_write(ANDROID_LOG_WARN, "ART-DCEVM", text.c_str());
}

// Named source declarations and java.lang.Class root shared by lower modules.
bool PrepareLayoutServices(JNIEnv* jni, std::string* failure) {
  using namespace symbols::structural;
  struct Required { symbols::NamedSymbol name; void** address; };
  const Required required[] = {
      {kFindDeclaredStaticField, HookSlot(&process->layout_services.find_static_field)},
      {kFindDeclaredInstanceField, HookSlot(&process->layout_services.find_declared_instance_field)},
      {kClassRedefinitionGetMirrorClass, HookSlot(&process->layout_services.mirror_class)},
      {kFindInstanceField, HookSlot(&process->layout_services.find_instance_field)}};
  for (const auto& entry : required) {
    *entry.address = symbols::Find(entry.name);
    if (!*entry.address) { *failure = std::string("missing ") + entry.name.name; return false; }
  }
  if (!jni) { *failure = "no JNIEnv for the JNI guards"; return false; }
  if (process->layout_services.class_type) return true;
  jclass local = jni->FindClass("java/lang/Class");
  process->layout_services.class_type = local ? static_cast<jclass>(jni->NewGlobalRef(local)) : nullptr;
  if (local) jni->DeleteLocalRef(local);
  if (process->layout_services.class_type) return true;
  jni->ExceptionClear();
  *failure = "exception classes unavailable";
  return false;
}

// The engine and verification redirection see the same backend, or neither does.
void PublishInterfaceBackend(interface_redefinition::Backend* backend) {
  process->interface_backend.store(backend, std::memory_order_release);
  verification_redirect::BindInterfaceBackend(backend);
}

bool RollBack(std::string* failure, size_t count = SIZE_MAX) {
  bool removed_all = true;
  for (size_t i = std::min(count, process->hook_tokens.size()); i-- > 0;) {
    if (!process->hook_tokens[i]) continue;
    const int error = shadowhook_unhook(process->hook_tokens[i]);
    if (error == 0) process->hook_tokens[i] = nullptr;
    else {
      *failure += std::string("; rollback failed: ") + shadowhook_to_errmsg(error);
      removed_all = false;
    }
  }
  return removed_all;
}
}  // namespace detail
using namespace detail;

jvmtiError Install(JavaVM* vm, jvmtiEnv* env,
                   const member_retirement::FieldPlaceholders& placeholders) {
  {
    std::lock_guard<std::mutex> installation_lock(installation_mutex);
    if (!process) process = std::make_unique<Process>();
  }
  // Route cannot observe hooks until their final JNI table and capabilities publish.
  std::lock_guard<std::recursive_mutex> transaction_lock(process->transaction_mutex);
  {
    std::lock_guard<std::mutex> lock(process->report_mutex);
    if (process->installed) return JVMTI_ERROR_NONE;
  }
  if (!vm || !env) {
    Diagnose("JavaVM or JVMTI environment unavailable");
    return JVMTI_ERROR_INVALID_ENVIRONMENT;
  }
  jvmtiCapabilities potential{};
  jvmtiError result = env->GetPotentialCapabilities(&potential);
  if (result != JVMTI_ERROR_NONE || !potential.can_redefine_classes) {
    Diagnose("can_redefine_classes unavailable; process must be Java-debuggable at startup");
    return result == JVMTI_ERROR_NONE ? JVMTI_ERROR_MUST_POSSESS_CAPABILITY : result;
  }
  const int initialization = shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false);
  if (initialization != 0) {
    Diagnose(std::string("ShadowHook initialization failed: ") + shadowhook_to_errmsg(initialization));
    return JVMTI_ERROR_INTERNAL;
  }
  using namespace symbols::structural;
  const symbols::NamedSymbol names[] = {kJvmtiRedefineClasses, kStructurallyRedefineClasses,
      kCheckClass, kCheckFields, kCheckMethods, kRecordNewFieldAdded, kRecordNewMethodAdded};
  std::array<void*, 7> addresses{};
  for (size_t i = 0; i < addresses.size(); ++i) {
    addresses[i] = symbols::Find(names[i]);
    if (!addresses[i]) {
      Diagnose(std::string("missing named symbol: ") + names[i].name);
      return JVMTI_ERROR_NOT_AVAILABLE;
    }
  }
  // Verify the public table without using its address as a hook target.
  // Installation still targets only the address returned by the named lookup.
  if (reinterpret_cast<void*>(env->functions->RedefineClasses) != addresses[0]) {
    Diagnose("standard JVMTI table does not use the named JvmtiFunctions::RedefineClasses wrapper");
    return JVMTI_ERROR_NOT_AVAILABLE;
  }
  std::string rollback = "previous partial installation";
  if (!RollBack(&rollback)) { Diagnose(rollback); return JVMTI_ERROR_INTERNAL; }
  process->runtime_vm = vm;
  process->layout_services.vm = vm;
  process->snapshot_env = env;
  process->structural_entry = reinterpret_cast<Entry>(addresses[1]);
  process->record_field = reinterpret_cast<Record>(addresses[5]);
  process->record_method = reinterpret_cast<Record>(addresses[6]);
  std::vector<Hook> replacements;
  ProcessCapabilities prepared;
  runtime_thread::Prepare();
  prepared.runtime_thread = runtime_thread::Available();
  class_layout::Prepare({});
  std::string retirement_note, interface_note, sealing_note, failure;
  JNIEnv* jni = nullptr;
  vm->GetEnv(reinterpret_cast<void**>(&jni), JNI_VERSION_1_6);
  std::vector<Hook> retirement_hooks;
  bool mapping_ready = PrepareLayoutServices(jni, &failure);
  if (mapping_ready) mapping_ready = member_retirement::Prepare({process->layout_services.find_instance_field}, &retirement_hooks, &failure);
  if (mapping_ready) mapping_ready = member_mapping::Prepare(process->layout_services, &retirement_hooks, &failure);
  if (mapping_ready) mapping_ready = verification_redirect::Prepare(&retirement_hooks, &failure);
  if (mapping_ready) {
    struct Required { symbols::NamedSymbol symbol; void* replacement; void** original; };
    const Required decisions[] = {
        {kCollectAndCreateNewInstances, HookAddress(CollectAndCreateNewInstances), &process->original_instances},
        {kAllocateNewClassObject, HookAddress(AllocateNewClassObject), &process->original_allocate}};
    for (const auto& decision : decisions) {
      void* address = symbols::Find(decision.symbol);
      if (!address) {
        failure = std::string("missing ") + decision.symbol.name;
        mapping_ready = false;
        break;
      }
      retirement_hooks.push_back({address, decision.replacement, decision.original, decision.symbol.name});
    }
  }
  if (mapping_ready) {
    class_layout::Prepare(process->layout_services);
    interface_dispatch::Prepare(jni, {process->layout_services.find_declared_instance_field,
                                      process->layout_services.mirror_class, process->layout_services.class_type});
    prepared.member_mapping = true;
    prepared.retirement = true;
    prepared.instance_check = true;
    prepared.superclass_change = verification_redirect::ResolvesClasses();
    // Optional: without it an ordinary class cannot drop an interface.
    if (!imt_sealing::Prepare(process->layout_services, &failure))
      sealing_note = "interface removal unavailable: " + failure;
    else if (!proxy_relink::Install(process->layout_services, &failure))
      sealing_note = "Proxy implementations unavailable: " + failure;
    replacements = std::move(retirement_hooks);
    class_layout::Report("unverified");
  } else {
    retirement_note = "method retirement unavailable: " + failure;
    // Native Thread probing never overwrites this installation failure.
    class_layout::Report("unavailable: " + failure);
  }
  PublishInterfaceBackend(interface_redefinition::Prepare(
      {prepared.superclass_change, prepared.runtime_thread,
      process->layout_services.mirror_class, process->layout_services.find_instance_field}, &replacements, &interface_note, &process->interface_runtime));
  const size_t retirement_hook_count = replacements.size();
  // Route last, so a routed batch can only see a complete set of validators.
  const Hook validators[] = {
      {addresses[2], reinterpret_cast<void*>(CheckClass), &process->original_class, names[2].name},
      {addresses[3], reinterpret_cast<void*>(CheckFields), &process->original_fields, names[3].name},
      {addresses[4], reinterpret_cast<void*>(CheckMethods), &process->original_methods, names[4].name},
      {addresses[0], reinterpret_cast<void*>(Route), &process->original_entry, names[0].name}};
  replacements.insert(replacements.end(), std::begin(validators), std::end(validators));
  process->hook_tokens.resize(replacements.size(), nullptr);
  for (size_t i = 0; i < replacements.size(); ++i) {
    const Hook& hook = replacements[i];
    process->hook_tokens[i] = shadowhook_hook_sym_addr(hook.target, hook.replacement, hook.original);
    if (!process->hook_tokens[i]) {
      const int error = shadowhook_get_errno();
      std::string failure = std::string("hook installation failed: ") + hook.name + ": " +
          shadowhook_to_errmsg(error);
      RollBack(&failure);
      Diagnose(failure);
      return JVMTI_ERROR_INTERNAL;
    }
  }
  // This process override cannot be undone; no later inline hook may fail.
  if (prepared.retirement && !member_retirement::InstallJniGuards(vm, env, &failure)) {
    std::string reason = "JNI guard installation failed: " + failure;
    if (!RollBack(&reason, retirement_hook_count)) {
      RollBack(&reason);
      Diagnose(reason);
      return JVMTI_ERROR_INTERNAL;
    }
    // Preserve the existing optional-retirement failure semantics: the base
    // validators/route stay installed, and additions/body edits remain usable.
    prepared = {};
    prepared.runtime_thread = runtime_thread::Available();
    retirement_note = "method retirement unavailable: " + failure;
    class_layout::Prepare({});
    class_layout::Report("unavailable: " + failure);
    PublishInterfaceBackend(interface_redefinition::Prepare(
        {false, prepared.runtime_thread,
        process->layout_services.mirror_class, process->layout_services.find_instance_field},
        &replacements, &interface_note, &process->interface_runtime));
  }
  // The engine's own environment carries no other events.
  jvmtiEventCallbacks callbacks{};
  callbacks.ClassPrepare = ClassPrepared;
  prepared.subclass_watch = prepared.instance_check &&
      env->SetEventCallbacks(&callbacks, sizeof(callbacks)) == JVMTI_ERROR_NONE;
  std::string field_note;
  if (prepared.retirement)
    member_retirement::ResolveFieldPlaceholders(env, placeholders, &field_note);
  std::lock_guard<std::mutex> lock(process->report_mutex);
  prepared.field_retirement = prepared.retirement && member_retirement::FieldRetirementAvailable();
  process->capabilities = prepared;
  process->installed = true;
  process->diagnostic = !process->capabilities.retirement
      ? "ART structural transaction with additions and modifier changes; members cannot retire"
      : process->capabilities.field_retirement
          ? "ART structural transaction with method and field retirement"
          : "ART structural transaction with method retirement; fields cannot retire";
  for (const auto* note : {&retirement_note, &field_note, &interface_note, &sealing_note})
    if (!note->empty()) process->diagnostic += "; " + *note;
  return JVMTI_ERROR_NONE;
}

}  // namespace artdcevm::structural_redefine
