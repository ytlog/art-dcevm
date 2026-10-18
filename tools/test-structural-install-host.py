#!/usr/bin/env python3
"""Exercise the production installer with fake named services and public JNI tables.

No ART object, hook trampoline or collector executes on the host. Failures cover
installation ordering, rollback, optional-retirement fallback and diagnostics.
The contract fixture records the pre-refactor capability JSON and hook/rollback
order for these fake services, including an unsupported interface runtime.
A routed Native transaction also checks retention after nested snapshot
invalidation; its ART transaction entry is a fake.
"""
from pathlib import Path
import hashlib
import json
import runpy
import re
import shutil
import subprocess
import tempfile

from evidence_paths import new_evidence_directory

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "platform/common/structural_redefine_engine.h"
#include "platform/common/interface_redefinition_internal.h"
#include "platform/common/class_layout.h"
#include "platform/common/interface_dispatch.h"
#include "platform/common/verification_redirect.h"
#include "platform/common/runtime_thread.h"
#include "platform/common/transaction_context.h"
#include "platform/common/imt_sealing.h"
#include "platform/common/narrowed_references.h"
#include "platform/common/proxy_relink.h"
#include "platform/symbols/narrowed_reference_symbols.h"
#include "platform/symbols/imt_sealing_symbols.h"
#include "platform/symbols/structural_redefine_symbols.h"
#include "platform/symbols/runtime_thread_symbols.h"
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
using namespace artdcevm;
int mode = 0, hook_calls = 0, set_calls = 0, hooks_at_set = 0;
bool route_hooked = false;
int sealing_hooks = 0;
bool failed_sealing_hook = false;
int probe_loads = 0;
std::set<void*> sealing_tokens;
int reference_hooks = 0;
bool failed_reference_hook = false;
int local_frames = 0, popped_frames = 0, borrowed_deletions = 0;
bool pending_exception = false, failed_frame = false;
std::set<jobject> frame_locals;
interface_redefinition::Backend* prepared_interface = nullptr;
using Route = jvmtiError (*)(jvmtiEnv*, jint, const jvmtiClassDefinition*);
Route routed = nullptr;
bool nested_preload = false;
int outer_routes = 0, child_method_reads = 0;
jclass root_type = reinterpret_cast<jclass>(0x3100);
jclass first_type = reinterpret_cast<jclass>(0x3200);
jclass second_type = reinterpret_cast<jclass>(0x3300);
jclass child_type = reinterpret_cast<jclass>(0x3400);
jclass nested_type = reinterpret_cast<jclass>(0x3500);
std::map<jobject, int> global_refs;

std::set<void*> active;
std::map<std::string, int> addresses;
JNINativeInterface functions{};
JNIInvokeInterface invocation{};
jvmtiInterface_1_ tooling{};
JNIEnv jni{&functions};
JavaVM vm{&invocation};
jvmtiEnv env{&tooling};
void Require(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
int conflict_token = 0, direct_token = 0, unimplemented_token = 0, cache_token = 0;
using FillSuperFn = void (*)(void*, abi::arm64::Handle, void*, void*, bool*, void**);
using LinkFn = bool (*)(void*, void*, abi::arm64::Handle, abi::arm64::Handle, bool*, void**);
FillSuperFn hooked_fill_super = nullptr;
LinkFn hooked_link = nullptr;
void* Conflict(void*) { return &conflict_token; }
bool probe_running = false;
bool LinkOriginal(void*, void*, abi::arm64::Handle, abi::arm64::Handle, bool* fresh, void** imt) {
  std::fill_n(imt, 43, &direct_token);
  if (probe_running) {
    if (mode == 10 || mode == 13 || mode == 14) {
      // All unimplemented, inherited cache, and direct outputs without any
      // new conflict cannot establish the canonical method.
      std::fill_n(imt, 43, mode == 10 ? &unimplemented_token : mode == 13 ? &cache_token : &direct_token);
      return true;
    }
    std::fill_n(imt, 43, mode == 17 ? nullptr : &conflict_token);
    *fresh = true;
    if (mode == 16) imt[0] = &direct_token;
  }
  return true;
}
void FillSuperOriginal(void*, abi::arm64::Handle, void*, void*, bool*, void** imt) {
  std::fill_n(imt, 43, &direct_token);
}
void SetRefOriginal(void*, void*, void* conflict, void* method, bool* fresh, void** slot) {
  if (*slot == conflict) *fresh = true;
  else *slot = method;
}
jvmtiError JNICALL Structural(jvmtiEnv*, jint count, const jvmtiClassDefinition*) {
  if (count == 2) {
    Require(global_refs[child_type] == 2,
            "Nested preload invalidation lost retirement or static-compatibility roots");
    ++outer_routes;
  }
  return JVMTI_ERROR_NONE;
}
extern "C" int android_get_device_api_level() { return mode == 5 ? 32 : 33; }
extern "C" int __android_log_write(int, const char*, const char*) { return 0; }
extern "C" int __android_log_print(int, const char*, const char*, ...) { return 0; }
void* Current() { return nullptr; }
void* Mark(void* object) { return object; }
jvmtiError JNICALL Redefine(jvmtiEnv*, jint, const jvmtiClassDefinition*) { return JVMTI_ERROR_NONE; }
extern "C" void* shadowhook_dlopen(const char*) { return reinterpret_cast<void*>(1); }
extern "C" void shadowhook_dlclose(void*) {}
extern "C" void* shadowhook_dlsym(void*, const char* name) {
  const std::string key(name);
  if (key == symbols::structural::kStructurallyRedefineClasses.name) return reinterpret_cast<void*>(Structural);
  if (key == symbols::structural::kJvmtiRedefineClasses.name) return reinterpret_cast<void*>(Redefine);
  if (key == symbols::runtime_thread::kCurrentThread.name) return reinterpret_cast<void*>(Current);
  if (key == symbols::runtime_thread::kReadBarrierMark.name) return reinterpret_cast<void*>(Mark);
  if (key == symbols::runtime_thread::kUseReadBarrier.name) return nullptr;
  if (mode == 3 && key == symbols::structural::kCollectNewFieldAndMethodMappings.name) return nullptr;
  if (mode >= 6 && key == symbols::imt_sealing::kLinearAllocAllocKind.name) return nullptr;
  if (mode == 19 && key == symbols::narrowed_references::kVisitReference.name) return nullptr;
  if ((mode == 6 || mode == 10 || mode == 11 || mode == 12 || mode == 13 || mode == 14 || mode >= 16) &&
      key == symbols::imt_sealing::kGetImtConflictMethod.name) return nullptr;
  if ((mode == 7 || mode == 12 || mode == 15) &&
      (key == symbols::imt_sealing::kLinkMethods.name || key == symbols::imt_sealing::kLinkMethods64.name)) return nullptr;
  if (mode == 15 && key == symbols::imt_sealing::kFillImtFromSuperClass.name) return nullptr;
  if (key == symbols::imt_sealing::kGetImtConflictMethod.name) return reinterpret_cast<void*>(Conflict);
  // No field placeholders are needed to exercise method-guard installation.
  if (key.find("EncodeFieldId") != std::string::npos) return nullptr;
  return &addresses[key];
}
extern "C" void* shadowhook_dlsym_dynsym(void* handle, const char* name) {
  return shadowhook_dlsym(handle, name);
}
extern "C" int shadowhook_init(int, bool) { return 0; }
extern "C" int shadowhook_get_errno() { return 1; }
extern "C" const char* shadowhook_to_errmsg(int) { return "injected hook error"; }
extern "C" void* shadowhook_hook_sym_addr(void* target, void* replacement, void** original) {
  ++hook_calls;
  if (target == reinterpret_cast<void*>(Redefine)) std::cout << "HOOK public-redefine\n";
  else {
    bool found = false;
    for (const auto& [name, address] : addresses) {
      if (&address == target) { std::cout << "HOOK " << name << '\n'; found = true; break; }
    }
    Require(found, "Hook target was not resolved by name");
  }
  if (target == &addresses[symbols::imt_sealing::kLinkMethods64.name] ||
      target == &addresses[symbols::imt_sealing::kFillImtFromSuperClass.name]) {
    ++sealing_hooks;
    if (mode == 8 && !failed_sealing_hook) { failed_sealing_hook = true; return nullptr; }
  }
  if (target == &addresses[symbols::narrowed_references::kVisitReference.name]) {
    ++reference_hooks;
    if (mode == 20 && !failed_reference_hook) { failed_reference_hook = true; return nullptr; }
  }
  if (mode == 1 && hook_calls == 2) return nullptr;
  *original = target;
  if (target == &addresses[symbols::imt_sealing::kLinkMethods64.name]) {
    *original = reinterpret_cast<void*>(LinkOriginal); hooked_link = reinterpret_cast<LinkFn>(replacement);
  } else if (target == &addresses[symbols::imt_sealing::kFillImtFromSuperClass.name]) {
    *original = reinterpret_cast<void*>(FillSuperOriginal); hooked_fill_super = reinterpret_cast<FillSuperFn>(replacement);


  }
  if (target == reinterpret_cast<void*>(Redefine)) {
    route_hooked = true; routed = reinterpret_cast<Route>(replacement);
  }
  auto token = reinterpret_cast<void*>(static_cast<uintptr_t>(hook_calls));
  active.insert(token);
  if (target == &addresses[symbols::imt_sealing::kLinkMethods64.name] ||
      target == &addresses[symbols::imt_sealing::kFillImtFromSuperClass.name]) sealing_tokens.insert(token);
  return token;
}
extern "C" int shadowhook_unhook(void* token) {
  std::cout << "UNHOOK " << reinterpret_cast<uintptr_t>(token) << '\n';
  Require(active.erase(token) == 1, "Rollback used an unknown token");
  sealing_tokens.erase(token);
  return 0;
}
jint JNICALL GetEnv(JavaVM*, void** out, jint) { *out = &jni; return JNI_OK; }
jclass JNICALL FindClass(JNIEnv*, const char* name) {
  if (std::string(name) == "com/github/ytlog/artdcevm/internal/ImtLinkProbe") {
    ++probe_loads;
    if (mode == 11) return nullptr;
    bool fresh = mode == 18; void* imt[43] = {};
    probe_running = true;
    if (hooked_link) hooked_link(nullptr, nullptr, {}, {}, &fresh, imt);
    if (hooked_fill_super) hooked_fill_super(nullptr, {}, nullptr, &conflict_token, &fresh, imt);
    probe_running = false;
  }
  jclass result = std::string(name) == "java/lang/Object" ? root_type : reinterpret_cast<jclass>(0x1000);
  if (local_frames > popped_frames) frame_locals.insert(result);
  return result;
}
jobject JNICALL NewGlobalRef(JNIEnv*, jobject object) { ++global_refs[object]; return object; }
void JNICALL DeleteLocalRef(JNIEnv*, jobject object) {
  frame_locals.erase(object);
  if (mode >= 21 && object == first_type) ++borrowed_deletions;
}
jint JNICALL PushLocalFrame(JNIEnv*, jint) {
  if (mode == 22 && !failed_frame) {
    failed_frame = true; pending_exception = true; return JNI_ERR;
  }
  ++local_frames;
  return JNI_OK;
}
jobject JNICALL PopLocalFrame(JNIEnv*, jobject) {
  ++popped_frames; frame_locals.clear(); return nullptr;
}
jboolean JNICALL IsProxy(JNIEnv*, jclass, jmethodID, va_list) { return JNI_FALSE; }
void JNICALL ClearException(JNIEnv*) { pending_exception = false; }
jmethodID JNICALL GetMethodID(JNIEnv*, jclass, const char*, const char*) {
  return reinterpret_cast<jmethodID>(0x2001);
}
jvmtiError JNICALL Potential(jvmtiEnv*, jvmtiCapabilities* caps) {
  *caps = {}; caps->can_redefine_classes = 1; return JVMTI_ERROR_NONE;
}
jvmtiError JNICALL GetJni(jvmtiEnv*, jniNativeInterface** table) {
  *table = static_cast<jniNativeInterface*>(std::malloc(sizeof(jniNativeInterface)));
  **table = functions; return JVMTI_ERROR_NONE;
}
jvmtiError JNICALL SetJni(jvmtiEnv*, const jniNativeInterface*) {
  ++set_calls;
  prepared_interface = interface_redefinition::detail::process->backend.get();
  Require(route_hooked, "JNI table changed before the final inline hook");
  hooks_at_set = hook_calls;
  return (mode == 2 || mode == 9) ? JVMTI_ERROR_INTERNAL : JVMTI_ERROR_NONE;
}
jvmtiError JNICALL Callbacks(jvmtiEnv*, const jvmtiEventCallbacks*, jint) {
  return JVMTI_ERROR_NONE;
}
void JNICALL DeleteGlobal(JNIEnv*, jobject value) { --global_refs[value]; }
jboolean JNICALL Same(JNIEnv*, jobject a, jobject b) { return a == b; }
jboolean JNICALL Exception(JNIEnv*) { return pending_exception; }
jclass JNICALL Super(JNIEnv*, jclass klass) {
  return klass == child_type ? first_type : klass == root_type ? nullptr : root_type;
}
jboolean JNICALL Assign(JNIEnv*, jclass klass, jclass target) {
  for (jclass current = klass; current; current = Super(nullptr, current))
    if (current == target) return true;
  return false;
}
jvmtiError JNICALL Modifiers(jvmtiEnv*, jclass, jint* value) { *value = 1; return JVMTI_ERROR_NONE; }
jvmtiError JNICALL Modifiable(jvmtiEnv*, jclass klass, jboolean* value) {
  *value = klass != nested_type; return JVMTI_ERROR_NONE;
}
jvmtiError JNICALL Loader(jvmtiEnv*, jclass klass, jobject* value) {
  if (mode == 4 && klass == second_type && !nested_preload) {
    nested_preload = true;
    const jvmtiClassDefinition definition{nested_type, 0, nullptr};
    Require(routed(&env, 1, &definition) == JVMTI_ERROR_NONE, "Nested preload transaction failed");
  }
  *value = nullptr; return JVMTI_ERROR_NONE;
}
jvmtiError JNICALL Signature(jvmtiEnv*, jclass klass, char** value, char**) {
  if (mode == 21 && klass == reinterpret_cast<jclass>(0x4100)) return JVMTI_ERROR_INTERNAL;
  const std::string text = klass == root_type ? "Ljava/lang/Object;" :
      "Lfixture/Type" + std::to_string(reinterpret_cast<uintptr_t>(klass)) + ";";
  *value = static_cast<char*>(std::malloc(text.size() + 1));
  std::strcpy(*value, text.c_str()); return JVMTI_ERROR_NONE;
}
jvmtiError JNICALL Interfaces(jvmtiEnv*, jclass klass, jint* n, jclass** value) {
  if (mode == 21 && klass == first_type) {
    *n = 2; *value = static_cast<jclass*>(std::malloc(2 * sizeof(jclass)));
    (*value)[0] = reinterpret_cast<jclass>(0x4100);
    (*value)[1] = reinterpret_cast<jclass>(0x4200);
    frame_locals.insert((*value)[0]); frame_locals.insert((*value)[1]);
    return JVMTI_ERROR_NONE;
  }
  *n = 0; *value = nullptr; return JVMTI_ERROR_NONE;
}
jvmtiError JNICALL Fields(jvmtiEnv*, jclass, jint* n, jfieldID** value) {
  *n = 0; *value = nullptr; return JVMTI_ERROR_NONE;
}
jvmtiError JNICALL Methods(jvmtiEnv*, jclass klass, jint* n, jmethodID** value) {
  if (klass == child_type) ++child_method_reads;
  *n = 0; *value = nullptr; return JVMTI_ERROR_NONE;
}
jvmtiError JNICALL Loaded(jvmtiEnv*, jint* n, jclass** value) {
  *n = 4; *value = static_cast<jclass*>(std::malloc(4 * sizeof(jclass)));
  const jclass classes[] = {child_type, second_type, first_type, root_type};
  std::memcpy(*value, classes, sizeof(classes)); return JVMTI_ERROR_NONE;
}
jvmtiError JNICALL Hash(jvmtiEnv*, jobject value, jint* hash) {
  *hash = reinterpret_cast<uintptr_t>(value) >> 8; return JVMTI_ERROR_NONE;
}
jvmtiError JNICALL IsInterface(jvmtiEnv*, jclass, jboolean* value) { *value = false; return JVMTI_ERROR_NONE; }
jvmtiError JNICALL Status(jvmtiEnv*, jclass, jint* value) { *value = JVMTI_CLASS_STATUS_PREPARED; return JVMTI_ERROR_NONE; }
jvmtiError JNICALL Deallocate(jvmtiEnv*, unsigned char* value) { std::free(value); return JVMTI_ERROR_NONE; }
jvmtiError JNICALL Events(jvmtiEnv*, jvmtiEventMode, jvmtiEvent, jthread, ...) { return JVMTI_ERROR_NONE; }
int main(int argc, char** argv) {
  try {
    Require(argc == 2, "Missing installation case");
    mode = std::stoi(argv[1]);
    invocation.GetEnv = GetEnv;
    functions.FindClass = FindClass; functions.NewGlobalRef = NewGlobalRef;
    functions.DeleteLocalRef = DeleteLocalRef; functions.GetMethodID = GetMethodID;
    functions.PushLocalFrame = PushLocalFrame; functions.PopLocalFrame = PopLocalFrame;
    functions.CallStaticBooleanMethodV = IsProxy; functions.ExceptionClear = ClearException;
    tooling.GetPotentialCapabilities = Potential; tooling.RedefineClasses = Redefine;
    tooling.GetJNIFunctionTable = GetJni; tooling.SetJNIFunctionTable = SetJni;
    tooling.SetEventCallbacks = Callbacks;
    functions.DeleteGlobalRef = DeleteGlobal; functions.IsSameObject = Same;
    functions.ExceptionCheck = Exception; functions.GetSuperclass = Super;
    functions.IsAssignableFrom = Assign; functions.GetStaticMethodID = GetMethodID;
    tooling.GetClassModifiers = Modifiers; tooling.IsModifiableClass = Modifiable;
    tooling.GetClassLoader = Loader; tooling.GetClassSignature = Signature;
    tooling.GetImplementedInterfaces = Interfaces; tooling.GetClassFields = Fields;
    tooling.GetClassMethods = Methods; tooling.GetLoadedClasses = Loaded;
    tooling.GetObjectHashCode = Hash; tooling.IsInterface = IsInterface;
    tooling.GetClassStatus = Status; tooling.Deallocate = Deallocate;
    tooling.SetEventNotificationMode = Events;
    const auto cold = structural_redefine::Capabilities();
    std::cout << "COLD " << cold << '\n';
    Require(cold.find("\"installed\":false") != std::string::npos &&
            cold.find("engine not installed") != std::string::npos,
            "Cold capability report requires constructed hook state");
    {
      transaction::Scope outer;
      verification_redirect::Begin();
      interface_dispatch::Require(nullptr, "added", "()V");
      auto* verification = outer.context().verification.get();
      auto* dispatch = outer.context().dispatch.get();
      Require(verification && dispatch, "Module transaction Parts were not created");
      {
        transaction::Scope nested;
        Require(!nested.context().verification && !nested.context().dispatch,
                "Nested transaction inherited module-specific state");
        verification_redirect::Begin();
        interface_dispatch::Require(nullptr, "nested", "()I");
        Require(nested.context().verification.get() != verification &&
                nested.context().dispatch.get() != dispatch,
                "Nested transaction reused outer module-specific state");
      }
      Require(transaction::Current() == &outer.context() &&
              outer.context().verification.get() == verification && outer.context().dispatch.get() == dispatch,
              "Nested transaction did not restore its enclosing module Parts");
    }
    const auto result = structural_redefine::Install(&vm, &env, {});
    if (mode == 1) {
      Require(result == JVMTI_ERROR_INTERNAL && set_calls == 0 && active.empty(),
              "Failed inline install changed JNI table or retained hooks");
      Require(structural_redefine::Install(&vm, &env, {}) == JVMTI_ERROR_NONE && set_calls == 1,
              "Retry after rollback did not reuse process state");
    } else if (mode == 19) {
      Require(result == JVMTI_ERROR_NONE && imt_sealing::Available() &&
              !narrowed_references::Available() && proxy_relink::Available(),
              "Missing reference visitor disabled unrelated Proxy or sealing services");
      const auto caps = structural_redefine::Capabilities();
      Require(caps.find("\"interfaceRemoval\":false") != std::string::npos &&
              caps.find("\"methodDeletion\":true") != std::string::npos,
              "Missing reference visitor advertised removal or disabled member editing");
    } else if (mode == 20) {
      Require(result == JVMTI_ERROR_INTERNAL && active.empty() &&
              !narrowed_references::Available() && !imt_sealing::Available(),
              "Failed reference hook retained hooks or advertised interface removal");
      Require(structural_redefine::Install(&vm, &env, {}) == JVMTI_ERROR_NONE &&
              reference_hooks == 2 && narrowed_references::Available() && imt_sealing::Available(),
              "Retry skipped the prepared reference visitor hook");
    } else if (mode == 21 || mode == 22) {
      Require(result == JVMTI_ERROR_NONE && narrowed_references::Available(), "Reference snapshot service unavailable");
      transaction::Scope capture;
      std::string why;
      const auto error = narrowed_references::Capture(&env, &jni, 1, &first_type, &why);
      Require(error == (mode == 21 ? JVMTI_ERROR_INTERNAL : JVMTI_ERROR_OUT_OF_MEMORY),
              "Reference snapshot failure lost its error code");
      Require(!capture.context().references && frame_locals.empty() && !pending_exception && borrowed_deletions == 0,
              "Failed capture retained temporary locals, damaged borrowed roots or left an exception");
      Require(local_frames == popped_frames && local_frames == (mode == 21 ? 1 : 0),
              "Reference snapshot did not balance successful local frames");
      if (mode == 22) {
        Require(narrowed_references::Capture(&env, &jni, 0, nullptr, &why) == JVMTI_ERROR_NONE &&
                capture.context().references && local_frames == 1 && popped_frames == 1 && frame_locals.empty(),
                "Reference snapshot could not retry after local frame allocation failed");
      }
    } else if (mode >= 6) {
      if (mode == 8) {
        Require(result == JVMTI_ERROR_INTERNAL && !imt_sealing::Available() && active.empty(),
                "Failed sealing hook installation retained hooks or enabled removal");
        Require(structural_redefine::Install(&vm, &env, {}) == JVMTI_ERROR_NONE &&
                sealing_hooks == 2 && imt_sealing::Available(),
                "Retry skipped the prepared pre-publication linking hook");
      } else if (mode == 6 || mode == 7 || mode == 12) {
        Require(result == JVMTI_ERROR_NONE && imt_sealing::Available(), "Named IMT fallback was not installed");
        {
          Require(probe_loads == 1, "Missing getter did not perform a real linking probe");
          std::string reason;
          imt_sealing::Installed(false);
          Require(!imt_sealing::Available(), "Rollback retained published availability");
          imt_sealing::Installed(true);
          Require(imt_sealing::Prime(&jni, &reason) && probe_loads == 1,
                  "Retry required relinking an already loaded probe or lost the captured canonical method");
        }
        abi::arm64::RedefinerPrefix driver{};
        abi::arm64::ClassRedefinitionPrefix target{&driver, nullptr, nullptr};
        std::string reason;
        Require(imt_sealing::Enable(&target, &reason), "Named fallback could not enable protection");
        bool fresh = false; void* imt[43] = {};
        if (mode == 7 || mode == 12) hooked_fill_super(nullptr, {}, nullptr, &conflict_token, &fresh, imt);
        else Require(hooked_link(nullptr, nullptr, {}, {}, &fresh, imt), "Link callback lost original success");
        Require(fresh && std::all_of(std::begin(imt), std::end(imt), [](void* slot) { return slot == &conflict_token; }),
                "Pre-publication fallback left a direct slot or a shared-table indication");
        SetRefOriginal(nullptr, nullptr, &conflict_token, &direct_token, &fresh, imt);
        Require(imt[0] == &conflict_token, "A later per-method insert unsealed the legacy table");
      } else {
        Require(result == JVMTI_ERROR_NONE, "Missing optional sealing service broke initialization");
        Require(!imt_sealing::Available(), "Unavailable or rolled-back sealing hook enabled removal");
        const auto caps = structural_redefine::Capabilities();
        Require(caps.find("\"interfaceRemoval\":false") != std::string::npos,
                "Missing linking service still advertised interface removal");
        Require(caps.find(mode == 9 ? "\"methodDeletion\":false" : "\"methodDeletion\":true") != std::string::npos,
                "Sealing failure changed unrelated member capabilities");
        if (mode == 9) Require(sealing_hooks == 1 && active.size() == 4,
                "JNI fallback retained the optional pre-publication linking hook");
        else if (mode == 10 || mode == 11 || mode == 13 || mode == 14 || mode >= 16) Require(sealing_hooks == 1 && sealing_tokens.empty(),
                "Probe failure left part of the optional hook group installed");
        else Require(sealing_hooks == 0, "Incomplete sealing services installed a linking hook");
      }
    } else {
      Require(result == JVMTI_ERROR_NONE, "Optional retirement failure changed initialization error");
      const auto caps = structural_redefine::Capabilities();
      Require(caps.find("\"installed\":true") != std::string::npos, "Base engine was not installed");
      if (mode == 0 || mode == 4 || mode == 5) {
        Require(set_calls == 1 && hooks_at_set == hook_calls && active.size() > 4,
                "JNI installation was not the final hook operation");
        Require(caps.find("\"methodDeletion\":true") != std::string::npos, "Retirement disabled");
        Require(caps.find(mode == 5 ? "disabled: interface redefinition is device-verified only on "
                                    : "unverified: interface runtime declarations") != std::string::npos,
                "Optional interface backend changed the runtime report");
        Require(structural_redefine::Install(&vm, &env, {}) == JVMTI_ERROR_NONE && set_calls == 1,
                "Idempotent Install changed the JNI table twice");
      } else {
        Require(active.size() == 4 && caps.find("\"methodDeletion\":false") != std::string::npos,
                "Optional failure did not retain exactly the base hooks");
        const auto reason = class_layout::State();
        Require(reason.find(mode == 2 ? "SetJNIFunctionTable failed" : "missing ") != std::string::npos,
                "Mapping diagnosis lost the original failure");
        Require(set_calls == (mode == 2 ? 1 : 0), "Mapping failure attempted JNI override");
        if (mode == 2) {
          Require(prepared_interface &&
                  prepared_interface == interface_redefinition::detail::process->backend.get(),
                  "JNI rollback destroyed a backend still borrowed by an ART hook");
          Require(!prepared_interface->Available() &&
                  prepared_interface->RuntimeState().find("disabled: ") == 0,
                  "A retained backend did not observe unavailable process services");
        }
        Require(runtime_thread::Available(), "Mapping failure prevented Thread preparation");
        transaction::Scope transaction;
        runtime_thread::Probe(&jni);
        Require(!runtime_thread::Ready() && class_layout::State() == reason,
                "Failed Thread probe overwrote unavailable mapping diagnosis");
      }
    }
    if (mode == 4) {
      const jvmtiClassDefinition definitions[] = {{first_type, 0, nullptr}, {second_type, 0, nullptr}};
      Require(routed(&env, 2, definitions) == JVMTI_ERROR_NONE && nested_preload && outer_routes == 1,
              "Native route did not preserve retention after nested snapshot invalidation");
      Require(global_refs[child_type] == 0, "Native route leaked the descendant root");
      Require(child_method_reads == 0, "A surviving snapshot collected facts from the invalidated target subtree");
    }
    std::cout << "CAPABILITIES " << structural_redefine::Capabilities() << '\n';
    std::cout << "Installer case " << mode << " PASS\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
'''


def verify_probe():
    """Ensure shrink-safe probe members saturate the fixed ART 13--17 hash."""
    probe = ROOT / 'artdcevm/src/main/java/com/github/ytlog/artdcevm/internal/ImtLinkProbe.java'
    source = probe.read_text()
    concrete = re.findall(r'@Override public void (probe[0-9]+)\(\) \{\}', source)
    declared = re.findall(r'^    void (probe[0-9]+)\(\);', source, re.MULTILINE)
    assert len(concrete) == len(set(concrete)) == 86 and sorted(concrete) == sorted(declared)
    def utf_hash(value):
        result = 0
        for char in value:
            result = (31 * result + ord(char)) & 0xffffffff
        return result
    descriptor = 'Lcom/github/ytlog/artdcevm/internal/ImtLinkProbeMethods;'
    counts = [0] * 43
    for name in declared:
        mixed = (427 * utf_hash(descriptor) + 16 * utf_hash(name) + 14 * utf_hash('V')) & 0xffffffff
        counts[mixed % 43] += 1
    assert counts == [2] * 43, 'Probe must have distinct concrete implementations in every slot'
    rules = (ROOT / 'artdcevm/consumer-rules.pro').read_text()
    for name in ('ImtLinkProbe', 'ImtLinkProbeMethods'):
        assert f'-keep class com.github.ytlog.artdcevm.internal.{name} {{ *; }}' in rules


def main():
    verify_probe()
    helper = runpy.run_path(str(ROOT / 'tools/test-structural-policy-host.py'))
    include = helper['java_home'](None) / 'include'
    platform = 'darwin' if (include / 'darwin').is_dir() else 'linux'
    evidence = new_evidence_directory(ROOT / 'build/evidence/host/structural-install')
    with tempfile.TemporaryDirectory(prefix='cases-', dir=evidence) as directory:
        work = Path(directory)
        (work / 'android').mkdir()
        (work / 'sys').mkdir()
        (work / 'android/log.h').write_text('#pragma once\n#define ANDROID_LOG_WARN 5\n'
            '#define ANDROID_LOG_ERROR 6\n#define ANDROID_LOG_FATAL 7\n'
            'extern "C" int __android_log_write(int, const char*, const char*);\n'
            'extern "C" int __android_log_print(int, const char*, const char*, ...);\n')
        (work / 'android/api-level.h').write_text('#pragma once\nextern "C" int android_get_device_api_level();\n')
        (work / 'sys/syscall.h').write_text('#pragma once\n#define __NR_gettid 1\n')
        (work / 'unistd.h').write_text('#pragma once\ninline long syscall(long, ...) { return 42; }\n')
        (work / 'shadowhook.h').write_text('#pragma once\n#define SHADOWHOOK_MODE_UNIQUE 0\n'
            'extern "C" {\nvoid* shadowhook_dlopen(const char*);\nvoid shadowhook_dlclose(void*);\n'
            'void* shadowhook_dlsym(void*, const char*);\nvoid* shadowhook_dlsym_dynsym(void*, const char*);\n'
            'int shadowhook_init(int, bool);\nint shadowhook_get_errno();\nconst char* shadowhook_to_errmsg(int);\n'
            'void* shadowhook_hook_sym_addr(void*, void*, void**);\nint shadowhook_unhook(void*);\n}\n')
        native = ROOT / 'artdcevm/src/main/cpp'
        source = work / 'test.cc'
        source.write_text(SOURCE)
        binary = work / 'test'
        common = native / 'platform/common'
        sources = sorted(common.glob('*.cc')) + [native / 'evolution/dex_schema.cc']
        # Select the host libc++ platform before enabling only the production
        # Android pointer-map declaration. No target libc headers are loaded.
        prelude = work / 'prelude.h'
        prelude.write_text(''.join('#include <' + header + '>\n' for header in (
            'algorithm', 'array', 'atomic', 'chrono', 'cstdarg', 'cstring', 'iostream',
            'limits', 'map', 'memory', 'mutex', 'optional', 'set', 'shared_mutex',
            'stdexcept', 'string', 'string_view', 'tuple', 'unordered_map', 'utility', 'vector')) +
            '#define __ANDROID__ 1\n')
        command = [shutil.which('clang++') or shutil.which('c++'), '-std=c++17',
                   '-include', str(prelude),
                   '-DJNINativeInterface_=JNINativeInterface', '-DJNIInvokeInterface_=JNIInvokeInterface',
                   '-I' + str(work), '-I' + str(native), '-I' + str(ROOT / 'third_party/jvmti'),
                   '-I' + str(include), '-I' + str(include / platform), str(source),
                   *map(str, sources), '-o', str(binary)]
        result = subprocess.run(command, capture_output=True, text=True)
        (evidence / 'compile.log').write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError('Installer compilation failed; see build/evidence/host/structural-install/compile.log')
        contracts = json.loads((ROOT / "tools/fixtures/structural-install-contract.json").read_text())
        for case in range(23):
            result = subprocess.run([str(binary), str(case)], capture_output=True, text=True)
            (evidence / f'case-{case}.log').write_text(result.stdout + result.stderr)
            if result.returncode:
                raise RuntimeError(f'Installer case {case} failed; inspect ignored evidence')
            lines = result.stdout.splitlines()
            contract = {
                'cold': json.loads(next(line[5:] for line in lines if line.startswith('COLD '))),
                'capabilities': json.loads(next(line[13:] for line in lines if line.startswith('CAPABILITIES '))),
                'hooksSha256': hashlib.sha256('\n'.join(line[5:] for line in lines if line.startswith('HOOK ')).encode()).hexdigest(),
                'unhooks': [int(line[7:]) for line in lines if line.startswith('UNHOOK ')]}
            if case < 6 and contract != contracts[str(case)]:
                raise RuntimeError(f'Installer case {case} changed capabilities, hook order or rollback order')
            print(next(line for line in lines if line.startswith('Installer case ')))
        print('Evidence: ' + str(evidence.relative_to(ROOT)))


if __name__ == '__main__':
    main()
