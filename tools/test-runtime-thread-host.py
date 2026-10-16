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
using namespace artdcevm;
namespace target_abi = artdcevm::abi::arm64;
void* thread = nullptr;
uint32_t* marking = nullptr;
bool missing_mark = false, export_config = false, cc = true;
int marks = 0;
std::vector<int> candidates;
std::string report;
void* Current() { return thread; }
void* Mark(void* object) {
  ++marks;
  // The stub models only ART's observable call boundary and its own flag check.
  return *marking ? reinterpret_cast<void*>(uintptr_t{0x2000}) : object;
}
namespace artdcevm::class_layout {
std::vector<int> CandidateReleases(bool verified_only) {
  if (verified_only) throw std::runtime_error("Native probe requested Class verification");
  return candidates;
}
void Report(std::string state) { report = std::move(state); }
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
  Thread fake{};
  auto* jni = reinterpret_cast<JNIEnv*>(uintptr_t{0x1234});
  fake.tls32.tid = 42;
  fake.tls_ptr.jni_env = jni;
  thread = &fake;
  marking = &fake.tls32.is_gc_marking;
  candidates = {release};
  transaction::Scope scope;
  for (bool exported : {false, true}) {
    export_config = exported;
    cc = true;
    missing_mark = false;
    runtime_thread::Prepare();
    runtime_thread::Probe(jni);
    Require(runtime_thread::Ready() && runtime_thread::Supports(release), "Thread candidate rejected");
    Require(runtime_thread::Current() == &fake, "Wrong current thread");
    marks = 0;
    *marking = 0;
    Require(runtime_thread::Read(0) == nullptr && marks == 0, "Null reference reached Mark");
    Require(runtime_thread::Read(0x1000) == reinterpret_cast<void*>(0x1000), "Inactive CC changed reference");
    Require(marks == (exported ? 1 : 0), "Wrong inactive CC call boundary");
    *marking = 1;  // Change AFTER the Native probe: Read must not cache this flag.
    marks = 0;
    Require(runtime_thread::Read(0x1000) == reinterpret_cast<void*>(0x2000) && marks == 1,
            "Marking CC failed to forward reference");
    if (exported) {
      cc = false;
      marks = 0;
      Require(runtime_thread::Read(0x1000) == reinterpret_cast<void*>(0x1000) && marks == 0,
              "CMC reached CC Mark despite named collector configuration");
    }
    {
      transaction::Scope nested;
      Require(!runtime_thread::Ready(), "Nested transaction reused outer probe");
      fake.tls32.tid = 43;
      runtime_thread::Probe(jni);
      Require(!runtime_thread::Ready() && report.find("JNI environment/tid") != std::string::npos,
              "Wrong tid did not reject mapping");
      fake.tls32.tid = 42;
      fake.tls_ptr.jni_env = nullptr;
      runtime_thread::Probe(jni);
      Require(!runtime_thread::Ready(), "Wrong JNI environment was accepted");
      fake.tls_ptr.jni_env = jni;
      *marking = 2;
      runtime_thread::Probe(jni);
      Require(!runtime_thread::Ready(), "Out-of-range marking flag was accepted");
      *marking = 0;
      missing_mark = true;
      runtime_thread::Prepare();
      runtime_thread::Probe(jni);
      Require(!runtime_thread::Ready() && report.find("named services unavailable") != std::string::npos,
              "Missing Mark did not reject mapping");
      missing_mark = false;
      runtime_thread::Prepare();
    }
    Require(runtime_thread::Ready() && runtime_thread::Current() == &fake,
            "Nested transaction lost outer Thread state");
  }
}
int main() {
  try {
    Check<target_abi::android11::Thread>(11); Check<target_abi::android12::Thread>(12);
    Check<target_abi::android13::Thread>(13); Check<target_abi::android14::Thread>(14);
    Check<target_abi::android15::Thread>(15); Check<target_abi::android16::Thread>(16);
    Check<target_abi::android17::Thread>(17);
    Require(!runtime_thread::Ready(), "Transaction scope leaked Thread state");
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
            'void* shadowhook_dlsym(void*, const char*);\nvoid* shadowhook_dlsym_dynsym(void*, const char*);\n}\n')
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
                   str(native / 'platform/common/transaction_context.cc'), '-o', str(binary)]
        build = subprocess.run(command, capture_output=True, text=True)
        (evidence / 'compile.log').write_text(build.stdout + build.stderr)
        if build.returncode:
            raise RuntimeError('Host compilation failed; see build/evidence/host/runtime-thread/compile.log')
        test = subprocess.run([str(binary)], capture_output=True, text=True)
        (evidence / 'test.log').write_text(test.stdout + test.stderr)
        if test.returncode:
            raise RuntimeError('Host assertions failed; see build/evidence/host/runtime-thread/test.log')
        print(test.stdout.strip())


if __name__ == '__main__':
    main()
