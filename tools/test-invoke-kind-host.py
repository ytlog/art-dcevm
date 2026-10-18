#!/usr/bin/env python3
"""Exercise production invocation guards with fake named ART services."""
from pathlib import Path
import runpy
import shutil
import subprocess
import tempfile
from evidence_paths import new_evidence_directory

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "platform/common/invoke_kind.h"
#include "platform/abi/arm64/common/runtime_objects.h"
#include "platform/symbols/invoke_kind_symbols.h"
#include "platform/symbols/structural_redefine_symbols.h"
#include "platform/symbols/runtime_thread_symbols.h"
#include <cstring>
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
bool missing_method = false;
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
  return missing_method ? nullptr : &method;
}
void* Caller(void* l, void* thread, uint32_t index, void* caller, int) {
  Require(thread == self, "incorrect caller Thread"); return Resolve(l, index, caller);
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
  if ((mode == 1 || mode == 4) && name == std::string(kResolveMethodId.name)) return HookAddress(Resolve);
  if ((mode == 2 || mode == 3 || mode == 5) && name == std::string(kResolveCaller.name)) return HookAddress(Caller);
  if ((mode == 2 || mode == 3) && name == std::string(kResolveCache.name)) return HookAddress(Cache);
  if ((mode == 1 || mode == 2 || mode == 5) && name == std::string(kNterpGetMethod.name)) return HookAddress(Nterp);
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
    bool supported = mode == 1 || mode == 2 || mode == 3;
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
        } else {
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
      invoke_kind::Installed(false);
      Require(!invoke_kind::Enable(), "enabled after installer rollback");
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
        result = subprocess.run(command, capture_output=True, text=True)
        (evidence / 'compile.log').write_text(result.stdout + result.stderr)
        if result.returncode: raise RuntimeError('Guard compilation failed; inspect ignored evidence')
        for case in range(6):
            result = subprocess.run([str(binary), str(case)], capture_output=True, text=True)
            (evidence / ('case-' + str(case) + '.log')).write_text(result.stdout + result.stderr)
            if result.returncode: raise RuntimeError('Guard case failed; inspect ignored evidence')
            print(result.stdout.strip())
    print('Evidence: ' + str(evidence.relative_to(ROOT)))


if __name__ == '__main__':
    main()
