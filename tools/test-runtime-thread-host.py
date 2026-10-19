#!/usr/bin/env python3
"""Exercise shared Thread probes and collector routing with fake named ART services.

No ART objects or collector algorithms run on the host. Compile the production
service and context, and check failure, nesting and CC/CMC call boundaries.
"""
from pathlib import Path
import runpy
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "platform/common/runtime_thread.h"
#include "platform/common/transaction_context.h"
#include "platform/common/class_layout.h"
#include "platform/abi/arm64/runtime_thread_table.h"
#include "platform/abi/arm64/release_order.h"
#include <type_traits>
#include "platform/symbols/runtime_thread_symbols.h"
#include "platform/abi/arm64/android11/thread.h"
#include "platform/abi/arm64/android12/thread.h"
#include "platform/abi/arm64/android13/thread.h"
#include "platform/abi/arm64/android14/thread.h"
#include "platform/abi/arm64/android15/thread.h"
#include "platform/abi/arm64/android16/thread.h"
#include "platform/abi/arm64/android17/thread.h"
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>
using namespace artdcevm;
namespace target_abi = artdcevm::abi::arm64;
void* thread = nullptr;
uint32_t* marking = nullptr;
bool missing_mark = false, export_config = false, cc = true;
int marks = 0;
int platform_api = 30;
extern "C" void* shadowhook_hook_sym_addr(void*, void*, void**) { return nullptr; }
extern "C" int android_get_device_api_level() { return platform_api; }
static_assert(!std::is_default_constructible_v<runtime_thread::VerifiedThread>);
static_assert(!std::is_constructible_v<runtime_thread::VerifiedThread, const transaction::Part*>);
static_assert(target_abi::CompatibleThreadPrefixes(target_abi::kRuntimeThreads));
constexpr auto BrokenThreadPrefixes() {
  auto layouts = target_abi::kRuntimeThreads;
  ++layouts[6].exception;
  return layouts;
}
static_assert(!target_abi::CompatibleThreadPrefixes(BrokenThreadPrefixes()));
constexpr auto BrokenScopePrefixes() {
  auto layouts = target_abi::kRuntimeThreads;
  ++layouts[6].top_handle_scope;
  return layouts;
}
static_assert(!target_abi::CompatibleThreadPrefixes(BrokenScopePrefixes()));
void* Current() { return thread; }
void* Mark(void* object) {
  ++marks;
  // The stub models only ART's observable call boundary and its own flag check.
  return *marking ? reinterpret_cast<void*>(uintptr_t{0x2000}) : object;
}
extern "C" int __android_log_write(int, const char*, const char*) { return 0; }
extern "C" void* shadowhook_dlopen(const char*) { return reinterpret_cast<void*>(1); }
extern "C" void shadowhook_dlclose(void*) {}
extern "C" void* shadowhook_dlsym(void*, const char* name) {
  using namespace symbols::runtime_thread;
  if (std::string_view(name) == kCurrentThread.name) return reinterpret_cast<void*>(Current);
  if (std::string_view(name) == kReadBarrierMark.name)
    return missing_mark ? nullptr : reinterpret_cast<void*>(Mark);
  if (std::string_view(name) == kUseReadBarrier.name) return export_config ? &cc : nullptr;
  throw std::runtime_error("Unexpected named service");
}
extern "C" void* shadowhook_dlsym_dynsym(void* handle, const char* name) {
  return shadowhook_dlsym(handle, name);
}
void Require(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
template <class Thread> void Check(int release) {
  struct { Thread fake{}; unsigned char tail[1024]{}; } storage;
  auto& fake = storage.fake;
  auto* jni = reinterpret_cast<JNIEnv*>(uintptr_t{0x1234});
  fake.tls32.tid = 42;
  fake.tls_ptr.jni_env = jni;
  thread = &fake;
  marking = &fake.tls32.is_gc_marking;
  platform_api = release == 11 ? 30 : release == 12 ? 31 : release + 20;
  transaction::Scope scope;
  runtime_thread::ThreadStarted();
  runtime_thread::CacheProofs(true);
  for (bool exported : {false, true}) {
    export_config = exported;
    cc = true;
    missing_mark = false;
    runtime_thread::Prepare();
    runtime_thread::Probe(jni);
    const auto ready = runtime_thread::Ready();
    Require(ready.has_value(), "Thread candidate rejected");
    Require(runtime_thread::LayoutFor(*ready, release) == target_abi::ThreadLayoutFor(release),
            "LayoutFor returned the wrong selected declaration");
    Require(&runtime_thread::Layout(*ready) == target_abi::ThreadLayoutFor(release),
            "Platform candidate was not first");
    for (int old = 10; old < release; ++old)
      Require(!runtime_thread::LayoutFor(*ready, old), "Older/unaccepted layout exposed");
    Require(!runtime_thread::LayoutFor(*ready, 18), "Out-of-range layout exposed");
    if (release == 15) {
      Require(runtime_thread::LayoutFor(*ready, 16) == target_abi::ThreadLayoutFor(16),
              "Accepted non-first release lost its own declaration");
      Require(runtime_thread::LayoutFor(*ready, 16) != &runtime_thread::Layout(*ready),
              "LayoutFor collapsed the selected release into the first accepted layout");
    }
    Require(runtime_thread::Current(*ready) == &fake, "Wrong current thread");
    Require(runtime_thread::Environment(*ready) == jni, "Wrong verified environment");
    {
      transaction::Scope linking;
      Require(!runtime_thread::Restore(reinterpret_cast<void*>(1)) && !runtime_thread::Ready(),
              "Cache accepted a different attachment");
      Require(runtime_thread::Restore(&fake), "Native thread proof was not cached");
      auto restored = runtime_thread::Ready();
      Require(restored && runtime_thread::Current(*restored) == &fake &&
              runtime_thread::Environment(*restored) == jni &&
              runtime_thread::LayoutFor(*restored, release), "Cache lost accepted layout facts");
    }
    bool foreign_restored = true;
    std::thread foreign([&] {
      transaction::Scope linking;
      foreign_restored = runtime_thread::Restore(&fake);
    });
    foreign.join();
    Require(!foreign_restored, "Cache crossed OS threads");
    runtime_thread::ThreadEnded();
    {
      transaction::Scope detached;
      Require(!runtime_thread::Restore(&fake), "Detached attachment reused Thread/JNIEnv addresses");
      runtime_thread::Probe(jni);
      Require(runtime_thread::Ready().has_value(), "New attachment could not establish a fresh Native proof");
      {
        transaction::Scope end_callback;
        Require(!runtime_thread::Restore(&fake), "A later agent's ThreadEnd callback repopulated the cache");
      }
      runtime_thread::ThreadStarted();
      runtime_thread::Probe(jni);
      transaction::Scope reattached;
      Require(runtime_thread::Restore(&fake), "ThreadStart did not permit a new attachment's proof");
    }
    marks = 0;
    *marking = 0;
    Require(runtime_thread::Read(*ready, 0) == nullptr && marks == 0, "Null reference reached Mark");
    Require(runtime_thread::Read(*ready, 0x1000) == reinterpret_cast<void*>(0x1000), "Inactive CC changed reference");
    Require(marks == (exported ? 1 : 0), "Wrong inactive CC call boundary");
    *marking = 1;  // Change AFTER the Native probe: Read must not cache this flag.
    marks = 0;
    Require(runtime_thread::Read(*ready, 0x1000) == reinterpret_cast<void*>(0x2000) && marks == 1,
            "Marking CC failed to forward reference");
    if (exported) {
      cc = false;
      marks = 0;
      Require(runtime_thread::Read(*ready, 0x1000) == reinterpret_cast<void*>(0x1000) && marks == 0,
              "CMC reached CC Mark despite named collector configuration");
    }
    {
      transaction::Scope nested;
      Require(!runtime_thread::Ready(), "Nested transaction reused outer probe");
      fake.tls32.tid = 43;
      runtime_thread::Probe(jni);
      Require(!runtime_thread::Ready() && std::string(runtime_thread::Rejection()).find("JNI environment/tid") != std::string::npos,
              "Wrong tid did not reject mapping");
      Require(!runtime_thread::Restore(&fake), "Rejected Native probe retained a cached proof");
      fake.tls32.tid = 42;
      fake.tls_ptr.jni_env = nullptr;
      runtime_thread::Probe(jni);
      Require(!runtime_thread::Ready(), "Wrong JNI environment was accepted");
      fake.tls_ptr.jni_env = jni;
      *marking = 2;
      runtime_thread::Probe(jni);
      const auto alternative = runtime_thread::Ready();
      Require(!alternative || !runtime_thread::LayoutFor(*alternative, release),
              "Out-of-range marking flag was accepted for its declaration");
      *marking = 0;
      missing_mark = true;
      runtime_thread::Prepare();
      runtime_thread::Probe(jni);
      Require(!runtime_thread::Ready() && std::string(runtime_thread::Rejection()).find("named services unavailable") != std::string::npos,
              "Missing Mark did not reject mapping");
      missing_mark = false;
      runtime_thread::Prepare();
    }
    Require(runtime_thread::Ready() && runtime_thread::Current(*ready) == &fake,
            "Nested transaction lost outer Thread state");
    runtime_thread::CacheProofs(false);
    {
      transaction::Scope no_lifecycle;
      runtime_thread::Probe(jni);
      transaction::Scope linking;
      Require(!runtime_thread::Restore(&fake), "Cache enabled without ThreadEnd notification");
    }
    runtime_thread::CacheProofs(true);
  }
}
int main() {
  try {
    Check<target_abi::android11::Thread>(11); Check<target_abi::android12::Thread>(12);
    Check<target_abi::android13::Thread>(13); Check<target_abi::android14::Thread>(14);
    Check<target_abi::android15::Thread>(15); Check<target_abi::android16::Thread>(16);
    Check<target_abi::android17::Thread>(17);
    Require(!runtime_thread::Ready(), "Transaction scope leaked Thread state");
    Require(target_abi::CandidateReleases(31) == std::vector<int>({12,13,14,15,16,17}),
            "Release order changed");
    Require(target_abi::CandidateReleases(38).empty(), "Future platform accepted old ART");
    // Exercise the real disabled layout module, not a stub report: failed
    // Thread services must preserve an unavailable mapping's original reason.
    class_layout::Prepare({});
    class_layout::Report("unavailable: NDK pointer-map layout differs from libc++ ABI v1");
    {
      transaction::Scope unavailable_mapping;
      missing_mark = true;
      runtime_thread::Prepare();
      runtime_thread::Probe(nullptr);
      Require(!class_layout::VerifyTarget(runtime_thread::Ready(), nullptr, {}),
              "Unavailable mapping verified");
      Require(class_layout::State() == "unavailable: NDK pointer-map layout differs from libc++ ABI v1",
              "Thread failure overwrote mapping installation diagnosis");
    }
    std::cout << "Runtime Thread probes, nested contexts and CC/CMC routing PASS\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
'''


def main():
    helper = runpy.run_path(str(ROOT / 'tools/test-structural-policy-host.py'))
    include = helper['java_home'](None) / 'include'
    platform = 'darwin' if (include / 'darwin').is_dir() else 'linux'
    evidence = ROOT / 'build/evidence/host/runtime-thread'
    evidence.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='cases-', dir=evidence) as directory:
        work = Path(directory)
        (work / 'android').mkdir()
        (work / 'sys').mkdir()
        (work / 'android/log.h').write_text('#pragma once\n#define ANDROID_LOG_FATAL 7\n'
            'extern "C" int __android_log_write(int, const char*, const char*);\n')
        (work / 'shadowhook.h').write_text('#pragma once\nextern "C" {\n'
            'void* shadowhook_dlopen(const char*);\nvoid shadowhook_dlclose(void*);\n'
            'void* shadowhook_dlsym(void*, const char*);\nvoid* shadowhook_dlsym_dynsym(void*, const char*);\n'
            'void* shadowhook_hook_sym_addr(void*, void*, void**);\n}\n')
        (work / 'android/api-level.h').write_text('#pragma once\nextern \"C\" int android_get_device_api_level();\n')
        (work / 'sys/syscall.h').write_text('#pragma once\n#define __NR_gettid 1\n')
        (work / 'unistd.h').write_text('#pragma once\ninline long syscall(long, ...) { return 42; }\n')
        source = work / 'test.cc'
        source.write_text(SOURCE)
        native = ROOT / 'artdcevm/src/main/cpp'
        binary = work / 'test'
        command = [shutil.which('clang++') or shutil.which('c++'), '-std=c++17', '-Wall',
                   '-Wextra', '-Werror', '-I' + str(work), '-I' + str(native),
                   '-I' + str(include), '-I' + str(include / platform), str(source),
                   str(native / 'platform/common/runtime_thread.cc'),
                   str(native / 'platform/common/transaction_context.cc'),
                   str(native / 'platform/common/class_layout.cc'),
                   str(native / 'platform/common/invoke_kind.cc'),
                   str(native / 'platform/common/structural_policy.cc'),
                   str(native / 'evolution/dex_schema.cc'), '-o', str(binary)]
        build = subprocess.run(command, capture_output=True, text=True)
        (evidence / 'compile.log').write_text(build.stdout + build.stderr)
        if build.returncode:
            raise RuntimeError('Host compilation failed; see build/evidence/host/runtime-thread/compile.log')
        test = subprocess.run([str(binary)], capture_output=True, text=True)
        (evidence / 'test.log').write_text(test.stdout + test.stderr)
        if test.returncode:
            raise RuntimeError('Host assertions failed; see build/evidence/host/runtime-thread/test.log')
        # A public caller cannot construct a proof or omit it from a read/layout.
        negative_cases = {
            'default-proof': 'runtime_thread::VerifiedThread unverified;',
            'unverified-read': 'runtime_thread::Read(0x1000);',
            'unverified-layout': 'runtime_thread::Layout();',
            'incompatible-prefix': 'static_assert(target_abi::CompatibleThreadPrefixes(BrokenThreadPrefixes()));',
            'incompatible-scope-prefix': 'static_assert(target_abi::CompatibleThreadPrefixes(BrokenScopePrefixes()));',
        }
        for label, statement in negative_cases.items():
            invalid = work / (label + '.cc')
            invalid.write_text(SOURCE + '\nvoid MustNotCompile() { ' + statement + ' }\n')
            check = subprocess.run(command[:command.index(str(source))] +
                                   ['-fsyntax-only', str(invalid)], capture_output=True, text=True)
            (evidence / (label + '.log')).write_text(check.stdout + check.stderr)
            if check.returncode == 0:
                raise RuntimeError(label + ' unexpectedly compiled')
        print(test.stdout.strip())
        print('Verified handle constraints, selected layouts, prefix assertions and unavailable mapping diagnosis PASS')


if __name__ == '__main__':
    main()
