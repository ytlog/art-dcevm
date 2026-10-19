#!/usr/bin/env python3
"""Exercise production invocation guards with fake named ART services."""
from pathlib import Path
import runpy
import shutil
import subprocess
import tempfile
import sys
from evidence_paths import new_evidence_directory

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "platform/common/invoke_kind.h"
#include "platform/abi/arm64/common/runtime_objects.h"
#include "platform/symbols/invoke_kind_symbols.h"
#include "platform/symbols/structural_redefine_symbols.h"
#include "platform/symbols/runtime_thread_symbols.h"
#include "platform/symbols/interface_redefinition_symbols.h"
#include "platform/common/runtime_roots.h"
#include "platform/abi/arm64/common/redefinition.h"
#include <cstring>
#include <sys/mman.h>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace artdcevm;
using abi::arm64::Handle;
abi::arm64::ArtMethodPrefix method{};
int mode, exceptions = 0, original_calls = 0;
void* linker = reinterpret_cast<void*>(0x1000);
void* self = reinterpret_cast<void*>(0x2000);
void* referrer = reinterpret_cast<void*>(0x3000);
void Require(bool ok, const char* reason);
bool missing_method = false;
abi::arm64::ArtMethodPrefix own_caller{};
struct FakeScope { void* link; int32_t capacity; uint32_t size; uint32_t root; };
struct FakeThread { uint32_t tid = 7; uint32_t marking = 0; void* scope = nullptr; } fake_thread;
namespace artdcevm::abi::arm64 { struct RootInfo {}; }
const abi::arm64::RootInfo root_info{};
bool moving = false, other_loader = false, other_class = false;
void* symbolic_type = nullptr;
void* LookupType(void* l, uint16_t index, void* cache, void* loader) {
  Require(l == linker && index == 4 && cache == reinterpret_cast<void*>(0x10000300) &&
          loader == reinterpret_cast<void*>(1), "wrong symbolic owner lookup ABI");
  return symbolic_type;
}
int selections = 0;
int cache_reads = 0;
uintptr_t cache_value = 0x7000;
uintptr_t CacheRead(void*) { cache_reads++; return cache_value; }
bool use_barrier = true;
void* Mark(void* object) { return object == reinterpret_cast<void*>(0x9000) ? reinterpret_cast<void*>(0x8000) : object; }
void Visit(void* thread, abi::arm64::RootVisitor* visitor, uint32_t tid) {
  Require(thread == &fake_thread && tid == 7, "incorrect root visitor ABI");
  auto* scope = static_cast<FakeScope*>(fake_thread.scope);
  uint32_t* roots[] = {&scope->root};
  Require(scope->capacity == 1 && scope->size == 1, "incorrect HandleScope declaration");
  visitor->VisitRoots(roots, 1, root_info);
}
const char* Descriptor(void* m) { return m == referrer && other_class ? "LOther;" : "LSubject;"; }
uintptr_t Loader(void* m) { return m == referrer && other_loader ? 2 : 1; }
void* Selection(void*, void*, uintptr_t*, const uint16_t*, bool, bool*) { selections++; return &own_caller; }
const char* expected_type = "Ljava/lang/IncompatibleClassChangeError;";
void Require(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
void* Current() { return self; }
void Throw(void* thread, const char* type, const char*) {
  Require(thread == self, "incorrect Thread ABI");
  Require(std::strcmp(type, expected_type) == 0, "wrong exception");
  exceptions++;
}
void* Resolve(void* l, uint32_t index, void* caller) {
  Require(l == linker && index == 9 && caller == referrer, "incorrect resolver ABI");
  if (moving) {
    auto* scope = static_cast<FakeScope*>(fake_thread.scope);
    Require(scope && scope->root == 0x4000, "receiver was not rooted across resolution");
    scope->root = 0x5000;
  }
  return missing_method ? nullptr : &method;
}
// Android 14--15 ResolveMethod<kNoChecks>(Thread*, ...) continues into the
// cache overload on a DEX cache miss, which production also hooks.
void* (*cache_hook)(void*, uint32_t, Handle, Handle, void*, int) = nullptr;
void* Caller(void* l, void* thread, uint32_t index, void* caller, int type) {
  Require(thread == self, "incorrect caller Thread");
  return cache_hook ? cache_hook(l, index, {}, {}, caller, type) : Resolve(l, index, caller);
}
void* Cache(void* l, uint32_t index, Handle cache, Handle loader, void* caller, int) {
  Require(cache.reference == nullptr && loader.reference == nullptr, "incorrect Handle ABI");
  return Resolve(l, index, caller);
}
uintptr_t Nterp(void* thread, void* caller, const uint16_t*) {
  Require(thread == self && caller == referrer, "incorrect nterp ABI"); original_calls++; return 42;
}
extern "C" void* shadowhook_dlopen(const char*) { return linker; }
extern "C" void shadowhook_dlclose(void*) {}
extern "C" void* shadowhook_dlsym(void*, const char* name) {
  using namespace symbols::invoke_kind;
  if (mode == 0) return nullptr;
  if (name == std::string(symbols::runtime_thread::kCurrentThread.name)) return HookAddress(Current);
  if (name == std::string(symbols::structural::kThrowNewException.name)) return HookAddress(Throw);
  // Case 10 is the Android 14--15 shape: selectors without ResolveMethodId.
  if ((mode == 1 || mode == 4 || (mode >= 6 && mode != 10 && mode != 11)) && name == std::string(kResolveMethodId.name)) return HookAddress(Resolve);
  if ((mode == 2 || mode == 3 || mode == 5 || mode == 10 || mode == 11) && name == std::string(kResolveCaller.name)) return HookAddress(Caller);
  if ((mode == 2 || mode == 3 || mode == 10 || mode == 11) && name == std::string(kResolveCache.name)) return HookAddress(Cache);
  if ((mode == 1 || mode == 2 || mode == 5 || mode >= 6) && name == std::string(kNterpGetMethod.name)) return HookAddress(Nterp);
  if (mode == 11 || mode == 16) {
    if (name == std::string(kObsoleteCacheLegacy.name)) return HookAddress(CacheRead);
    if (name == std::string(kSelectVirtual.name) || name == std::string(kSelectInterface.name) ||
        name == std::string(kObsoleteCacheRead.name) || name == std::string(kObsoleteCacheRaw.name)) return nullptr;
  }
  if (mode >= 6) {
    if ((mode == 12 || mode == 13) && name == std::string(symbols::interface_redefinition::kLookupCache.name)) return HookAddress(LookupType);
    if (mode == 13 && (name == std::string(kSelectVirtual.name) || name == std::string(kSelectInterface.name) ||
        name == std::string(kMethodLoader.name) || name == std::string(symbols::runtime_thread::kUseReadBarrier.name))) return nullptr;
    if (mode != 14 && mode != 15 && name == std::string(kSelectVirtual.name)) return HookAddress(Selection);
    if (mode != 7 && mode != 15 && name == std::string(kSelectInterface.name)) return HookAddress(Selection);
    if (mode != 8 && name == std::string(kMethodDescriptor.name)) return HookAddress(Descriptor);
    if (name == std::string(kMethodLoader.name)) return HookAddress(Loader);
    if (name == std::string(kObsoleteCacheRead.name) || name == std::string(kObsoleteCacheRaw.name)) return HookAddress(CacheRead);
    if (name == std::string(symbols::runtime_thread::kReadBarrierMark.name)) return HookAddress(Mark);
    if (name == std::string(symbols::runtime_thread::kUseReadBarrier.name)) return mode == 16 ? nullptr : &use_barrier;
    if (mode != 9 && name == std::string(symbols::interface_redefinition::kHandleScopeVisitRoots.name)) return HookAddress(Visit);
  }
  return nullptr;
}
extern "C" void* shadowhook_dlsym_dynsym(void* h, const char* n) { return shadowhook_dlsym(h,n); }
int main(int argc, char** argv) {
  try {
    Require(argc == 2, "case required"); mode = std::stoi(argv[1]);
    for (uint32_t precompiled : {0x00200000u, 0x00800000u}) {
      method.access_flags.store(precompiled | 1);
      invoke_kind::KeepInterpreted(&method, precompiled);
      Require(method.access_flags.load() == 0x02000001u, "precompiled caller remained compilable");
      for (uint32_t flag : {0x0100u, 0x0400u, 0x80000000u}) {
        method.access_flags.store(flag);
        invoke_kind::KeepInterpreted(&method, precompiled);
        Require(method.access_flags.load() == flag, "changed a native, abstract or intrinsic method");
      }
    }
    std::vector<Hook> hooks;
    bool supported = mode == 1 || mode == 2 || mode == 3 || mode >= 6;
    Require(invoke_kind::Prepare(&hooks) == supported, "wrong optional-service decision");
    if (!supported) {
      Require(hooks.empty(), "partial optional hooks escaped preparation");
      Require(!invoke_kind::Enable(), "enabled missing service family");
    } else {
      for (const auto& hook : hooks) *hook.original = hook.target;
      Require(!invoke_kind::Enable(), "enabled before installation");
      invoke_kind::Installed(true);
      Require(!invoke_kind::Enable(), "enabled without observed ClassLinker");
      invoke_kind::ObserveClassLinker(linker);
      Require(invoke_kind::Enable(), "could not enable complete guards");
      uint16_t instructions[] = {0x6e, 9, 0};
      method.access_flags.store(8);
      for (const auto& hook : hooks) {
        using namespace symbols::invoke_kind;
        if (std::string(hook.name) == kNterpGetMethod.name) {
          auto call = reinterpret_cast<uintptr_t (*)(void*,void*,const uint16_t*)>(hook.replacement);
          for (uint16_t opcode : {0x6e,0x6f,0x70,0x72,0x74,0x75,0x76,0x78}) {
            instructions[0] = opcode; int before = original_calls;
            Require(call(self,referrer,instructions) == 0 && original_calls == before,
                    "incompatible method reached nterp cache fill");
          }
          for (uint16_t opcode : {0x71,0x77}) {
            instructions[0] = opcode; Require(call(self,referrer,instructions) == 42, "valid static rejected");
          }
          method.access_flags.store(0);
          for (uint16_t opcode : {0x71,0x77}) {
            instructions[0] = opcode; int before = original_calls;
            Require(call(self,referrer,instructions) == 0 && original_calls == before,
                    "instance method reached static nterp cache fill");
          }
          instructions[0] = 0x6e;
          Require(call(self,referrer,instructions) == 42, "valid instance rejected");
          method.access_flags.store(2); expected_type = "Ljava/lang/IllegalAccessError;";
          for (uint16_t opcode : {0x6e,0x6f,0x72,0x74,0x75,0x78}) {
            instructions[0] = opcode; int before = original_calls;
            Require(call(self,referrer,instructions) == 0 && original_calls == before,
                    "private method reached virtual/super/interface cache fill");
          }
          for (uint16_t opcode : {0x70,0x76}) {
            instructions[0] = opcode;
            Require(call(self,referrer,instructions) == 42, "legal private direct call rejected");
          }
          expected_type = "Ljava/lang/IncompatibleClassChangeError;"; method.access_flags.store(8);
          missing_method = true; instructions[0] = 0x6e; int before = exceptions;
          Require(call(self,referrer,instructions) == 0 && exceptions == before, "replaced existing resolver failure");
          missing_method = false;
        } else if (std::string(hook.name) == kResolveCaller.name) {
          auto call = reinterpret_cast<decltype(&Caller)>(hook.replacement);
          Require(call(linker,self,9,referrer,2) == nullptr, "cached virtual mismatch accepted");
          Require(call(linker,self,9,referrer,0) == &method, "valid static caller rejected");
          method.access_flags.store(0);
          Require(call(linker,self,9,referrer,2) == &method, "valid virtual caller rejected");
          Require(call(linker,self,9,referrer,0) == nullptr, "cached static mismatch accepted");
          method.access_flags.store(2); expected_type = "Ljava/lang/IllegalAccessError;";
          for (int type : {2,3,4}) Require(call(linker,self,9,referrer,type) == nullptr, "cached private mismatch accepted");
          Require(call(linker,self,9,referrer,1) == &method, "legal private caller rejected");
          expected_type = "Ljava/lang/IncompatibleClassChangeError;"; method.access_flags.store(8);
        } else if (std::string(hook.name) == kResolveCache.name) {
          auto call = reinterpret_cast<decltype(&Cache)>(hook.replacement);
          Require(call(linker,9,{}, {},referrer,2) == nullptr, "cold virtual mismatch accepted");
          Require(call(linker,9,{}, {},referrer,0) == &method, "valid cold static rejected");
          method.access_flags.store(0);
          Require(call(linker,9,{}, {},referrer,0) == nullptr, "cold static mismatch accepted");
          Require(call(linker,9,{}, {},referrer,2) == &method, "valid cold instance rejected");
          method.access_flags.store(2); expected_type = "Ljava/lang/IllegalAccessError;";
          for (int type : {2,3,4}) Require(call(linker,9,{}, {},referrer,type) == nullptr, "cold private mismatch accepted");
          Require(call(linker,9,{}, {},referrer,1) == &method, "legal cold private call rejected");
          expected_type = "Ljava/lang/IncompatibleClassChangeError;"; method.access_flags.store(8);
        }
      }
      Require(exceptions > 0, "no invocation mismatch tested");
      if (mode >= 6) {
        abi::arm64::RuntimeThread thread_layout{offsetof(FakeThread,tid),0,0,0,offsetof(FakeThread,scope)};
        abi::arm64::InterfaceRuntime layout{};
        layout.scope_header_size = offsetof(FakeScope,root); layout.scope_link = offsetof(FakeScope,link);
        layout.scope_capacity = offsetof(FakeScope,capacity); layout.scope_size = offsetof(FakeScope,size);
        FakeScope admission{nullptr,1,1,0x6000}; fake_thread.scope = &admission;
        invoke_kind::ObserveScope(&fake_thread,&thread_layout,&layout,{nullptr});
        Require(!invoke_kind::SelfCalls(), "unprobed scope enabled self calls");
        invoke_kind::ObserveScope(&fake_thread,&thread_layout,&layout,{&admission.root});
        Require(!invoke_kind::SelfCalls(), "unprobed Class enabled self calls");
        if (mode == 11 || mode == 16) {
          for (const auto& hook : hooks)
            if (std::string(hook.name) == symbols::invoke_kind::kObsoleteCacheLegacy.name)
              Require(reinterpret_cast<decltype(&CacheRead)>(hook.replacement)(&method) == 0x7000,
                      "unprobed legacy cache bypassed ART");
        }
        if (mode == 16) {
          bool found = false;
          for (const auto& hook : hooks) found |= std::string(hook.name) == symbols::invoke_kind::kObsoleteCacheLegacy.name;
          Require(found, "legacy copied-obsolete cache hook requires an unavailable modern collector flag");
          self = &fake_thread;
          fake_thread.marking = 1;
        }
        invoke_kind::ObserveClassLayout(0,4,8,8,offsetof(FakeThread,marking),8);
        const bool complete = mode == 6 || mode == 10 || mode == 12;
        Require(invoke_kind::SelfCalls() == complete && invoke_kind::SelectionServices() == complete,
                "combined selection capability is not the conjunction");
        Require(invoke_kind::SelfCalls(false) == (complete || mode == 7) &&
                invoke_kind::SelfCalls(true) == (complete || mode == 14), "wrong per-kind self admission");
        for (bool iface : {false, true})
          Require(invoke_kind::SelectionServices(iface) == (invoke_kind::SelectionUnavailable(iface) == nullptr),
                  "selection reason disagrees with capability");
        if (complete || mode == 7 || mode == 8 || mode == 9 || mode == 14 || mode == 15 || mode == 11 || mode == 13 || mode == 16) {
          void* klass = mmap(reinterpret_cast<void*>(0x10000000),4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON|MAP_FIXED,-1,0);
          Require(klass != MAP_FAILED, "unable to allocate low-address class fixture");
          auto* fields = static_cast<uint32_t*>(klass); fields[0] = 0x00200000; fields[1] = 0x9000;
          // Fake ClassExt and PointerArray in the same low-address page.
          fields[2] = 0x10000100; auto* ext = reinterpret_cast<uint32_t*>(0x10000100);
          auto* obsolete = reinterpret_cast<abi::arm64::PointerArray*>(0x10000200);
          ext[2] = 0x10000200; obsolete->length = 0;
          method.declaring_class = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(klass));
          for (const auto& hook : hooks) {
            bool read = std::string(hook.name) == symbols::invoke_kind::kObsoleteCacheRead.name ||
                        std::string(hook.name) == symbols::invoke_kind::kObsoleteCacheLegacy.name;
            if (!read && std::string(hook.name) != symbols::invoke_kind::kObsoleteCacheRaw.name) continue;
            auto call = reinterpret_cast<decltype(&CacheRead)>(hook.replacement);
            method.access_flags.store(0x01040000);
            Require(call(&method) == (read ? 0x8000u : 0x9000u), "copied obsolete frame used an unrelated DEX cache");
            obsolete->length = 1; obsolete->elements[0] = reinterpret_cast<uintptr_t>(&method);
            Require(call(&method) == 0x7000, "earlier non-structural frame lost its obsolete-map DEX");
            obsolete->length = 0;
            use_barrier = false; if (mode == 16) fake_thread.marking = 0;
            Require(call(&method) == 0x9000u, "CMC copied cache used the CC marker");
            use_barrier = true; if (mode == 16) fake_thread.marking = 1;
            fields[0] = 0;
            Require(call(&method) == 0x7000, "non-structural obsolete cache bypassed ART");
            fields[0] = 0x00200000; method.access_flags.store(0x00040000);
            Require(call(&method) == 0x7000, "declared obsolete cache bypassed ART");
          }
          Require(cache_reads == (mode == 13 ? 0 : (mode == 11 || mode == 16) ? 4 : 6), "wrong obsolete cache delegation");
          if (mode == 12 || mode == 13) {
            uint8_t bytes[256]{};
            uint32_t size = sizeof(bytes), count = 10, offset = 0x70;
            std::memcpy(bytes + 32, &size, 4); std::memcpy(bytes + 88, &count, 4);
            std::memcpy(bytes + 92, &offset, 4); bytes[0x70 + 9 * 8] = 4;
            abi::arm64::DexFilePrefix dex{};
            dex.begin = dex.data_begin = bytes; dex.data_size = sizeof(bytes);
            auto* cache = reinterpret_cast<abi::arm64::DexCache*>(0x10000300);
            cache->class_loader = 1;
            cache->dex_file = reinterpret_cast<uintptr_t>(&dex);
            fields[1] = 0x10000300;
            symbolic_type = reinterpret_cast<void*>(0x10000500);
            auto* symbolic_flags = static_cast<uint32_t*>(symbolic_type);
            own_caller.declaring_class = 0x10000000; own_caller.access_flags.store(1);
            if (mode == 13) {
              self = &fake_thread; cache_value = 0x10000300;
              own_caller.access_flags.store(abi::arm64::kAccObsoleteMethod);
            }
            referrer = &own_caller; method.access_flags.store(1);
            Require(invoke_kind::ClassKinds() && invoke_kind::EnableClassKinds(), "complete kind guards unavailable");
            for (const auto& hook : hooks) {
              if (std::string(hook.name) != symbols::invoke_kind::kNterpGetMethod.name) continue;
              auto call = reinterpret_cast<uintptr_t (*)(void*,void*,const uint16_t*)>(hook.replacement);
              uint16_t inst[]{0x6e,9,0};
              *symbolic_flags = 0;
              // The resolved method can belong to an interface (copied
              // default); only the DEX's symbolic class determines legality.
              fields[0] = 0x0200;
              Require(call(self,referrer,inst) == 42, "legal class call to inherited interface default rejected");
              *symbolic_flags = 0x0200; fields[0] = 0;
              Require(call(self,referrer,inst) == 0, "old virtual interface owner entered a vtable");
              method.access_flags.store(2);
              Require(call(self,referrer,inst) == 0, "private method access masked the symbolic class/interface ICCE");
              method.access_flags.store(1);
              inst[0] = 0x72;
              Require(call(self,referrer,inst) == 42, "interface call to Object declaration rejected");
              *symbolic_flags = 0;
              Require(call(self,referrer,inst) == 0, "old interface class owner entered an IMT");
              symbolic_type = nullptr;
              Require(call(self,referrer,inst) == 0, "missing symbolic owner was accepted");
              symbolic_type = reinterpret_cast<void*>(0x10000500);
              struct Constructor { uint32_t owner; std::atomic<uint32_t> flags; uint32_t index; } ctor{0x10000000, 0x00050000, 9};
              cache_value = 0x10000300;
              *symbolic_flags = 0x0600;
              Require(invoke_kind::UninstantiableConstructor(&ctor), "retired constructor ignored current interface instantiability");
              *symbolic_flags = 0;
              Require(!invoke_kind::UninstantiableConstructor(&ctor), "restored concrete class remained uninstantiable");
            }
            // Later self-call cases use a synthetic caller with no DEX.
            invoke_kind::Installed(false);
            munmap(klass,4096);
            std::cout << "Invocation guard case " << mode << " PASS\n";
            return 0;
          }
          munmap(klass,4096); method.declaring_class = 0;
          self = &fake_thread; referrer = &own_caller;
          own_caller.access_flags.store(abi::arm64::kAccObsoleteMethod);
          uint16_t inst[]{0x6e,9,0}; bool string_init = false;
          for (const auto& hook : hooks)
            if (std::string(hook.name) == symbols::invoke_kind::kResolveCache.name)
              cache_hook = reinterpret_cast<decltype(cache_hook)>(hook.replacement);
          Require((cache_hook != nullptr) == (mode == 10 || mode == 11), "unexpected cache overload hook");
          for (const auto& hook : hooks) {
            if (std::string(hook.name) != symbols::invoke_kind::kSelectVirtual.name &&
                std::string(hook.name) != symbols::invoke_kind::kSelectInterface.name) continue;
            auto call = reinterpret_cast<decltype(&Selection)>(hook.replacement);
            method.access_flags.store(2); uintptr_t receiver = 0x4000; moving = true;
            Require(call(self,referrer,&receiver,inst,false,&string_init) == &method, "own private target not selected directly");
            Require(receiver == 0x5000 && fake_thread.scope == &admission && selections == 0, "receiver/root chain not refreshed");
            moving = false; receiver = 0;
            expected_type = "Ljava/lang/NullPointerException;";
            Require(call(self,referrer,&receiver,inst,false,&string_init) == nullptr, "own null call accepted");
            receiver = 0x4000; expected_type = "Ljava/lang/IllegalAccessError;";
            other_loader = true;
            Require(call(self,referrer,&receiver,inst,false,&string_init) == nullptr, "same descriptor in another loader gained private access");
            other_loader = false; other_class = true;
            Require(call(self,referrer,&receiver,inst,false,&string_init) == nullptr, "another class gained private access");
            other_class = false;
            int before = exceptions;
            Require(call(self,referrer,&receiver,inst,true,&string_init) == nullptr && exceptions == before, "lookup-only performed resolution");
            missing_method = true;
            Require(call(self,referrer,&receiver,inst,false,&string_init) == nullptr && exceptions == before, "overwrote resolver failure");
            missing_method = false; method.access_flags.store(8); expected_type = "Ljava/lang/IncompatibleClassChangeError;";
            Require(call(self,referrer,&receiver,inst,false,&string_init) == nullptr, "own static mismatch accepted");
            method.access_flags.store(1);
            Require(call(self,referrer,&receiver,inst,false,&string_init) == &own_caller, "ordinary dispatch bypassed ART");
            selections = 0;
          }
        }
      }
      invoke_kind::Installed(false);
      Require(!invoke_kind::Enable(), "enabled after installer rollback");
      Require(!invoke_kind::SelfCalls(false) && !invoke_kind::SelfCalls(true), "self admission survived rollback");
    }
    std::cout << "Invocation guard case " << mode << " PASS\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
'''


def main():
    evidence = new_evidence_directory(ROOT / 'build/evidence/host/invoke-kind')
    helper = runpy.run_path(str(ROOT / 'tools/test-structural-policy-host.py'))
    include = helper['java_home'](None) / 'include'
    platform = 'darwin' if (include / 'darwin').is_dir() else 'linux'
    with tempfile.TemporaryDirectory(prefix='cases-', dir=evidence) as temp:
        work = Path(temp)
        (work / 'shadowhook.h').write_text('#pragma once\nextern "C" {\n'
            'void* shadowhook_dlopen(const char*); void shadowhook_dlclose(void*);\n'
            'void* shadowhook_dlsym(void*,const char*); void* shadowhook_dlsym_dynsym(void*,const char*);\n}\n')
        source = work / 'test.cc'; source.write_text(SOURCE)
        native = ROOT / 'artdcevm/src/main/cpp'; binary = work / 'test'
        command = [shutil.which('clang++') or shutil.which('c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                   '-I' + str(work), '-I' + str(native), '-I' + str(include), '-I' + str(include / platform),
                   '-I' + str(ROOT / 'third_party/jvmti'), str(source), str(native / 'platform/common/invoke_kind.cc'), '-o', str(binary)]
        if sys.platform == 'darwin':
            # ART heap references are 32-bit; reserve a low-address fake Class.
            command.extend(['-Wl,-pagezero_size,0x1000'])
        result = subprocess.run(command, capture_output=True, text=True)
        (evidence / 'compile.log').write_text(result.stdout + result.stderr)
        if result.returncode: raise RuntimeError('Guard compilation failed; inspect ignored evidence')
        for case in range(17):
            result = subprocess.run([str(binary), str(case)], capture_output=True, text=True)
            (evidence / ('case-' + str(case) + '.log')).write_text(result.stdout + result.stderr)
            if result.returncode: raise RuntimeError('Guard case failed; inspect ignored evidence')
            print(result.stdout.strip())
    print('Evidence: ' + str(evidence.relative_to(ROOT)))


if __name__ == '__main__':
    main()
