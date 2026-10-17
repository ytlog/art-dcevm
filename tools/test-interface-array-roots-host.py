#!/usr/bin/env python3
"""Exercise Native JNI array ownership and named global decoding with table fakes.

No ART allocator, private ABI or collector executes on the host.
"""
from pathlib import Path
import runpy
import shutil
import subprocess
import tempfile

from evidence_paths import new_evidence_directory

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "platform/common/interface_array_roots.h"
#include <iostream>
#include <map>
#include <stdexcept>
using artdcevm::interface_redefinition::ClassArrayRoots;
JavaVM vm{};
jclass element = reinterpret_cast<jclass>(0x1000);
std::map<jobject, uintptr_t> locals, globals;
uintptr_t next_ref = 100, next_object = 1000;
int arrays = 0, promotions = 0, deletions = 0, decodes = 0;
int fail_array = 0, fail_global = 0;
bool runnable = false, pending = false;
void Require(bool value, const char* why) {
  if (!value) throw std::runtime_error(why);
}
jobject Ref() { return reinterpret_cast<jobject>(++next_ref * 8); }
void Move() {
  for (auto& [ref, object] : locals) object += 0x10000;
  for (auto& [ref, object] : globals) object += 0x10000;
}
jobjectArray JNICALL NewArray(JNIEnv*, jsize length, jclass type, jobject initial) {
  Require(!runnable, "Array allocated inside ART");
  Require(type == element && length == 7 && !initial, "Array schema changed");
  Move(); // An allocation can move already rooted objects.
  if (++arrays == fail_array) { pending = true; return nullptr; }
  jobject ref = Ref(); locals[ref] = ++next_object;
  return reinterpret_cast<jobjectArray>(ref);
}
jobject JNICALL Global(JNIEnv*, jobject local) {
  Require(!runnable && locals.count(local), "Invalid Native local promotion");
  if (++promotions == fail_global) { pending = true; return nullptr; }
  jobject ref = Ref(); globals[ref] = locals.at(local); return ref;
}
void JNICALL DeleteLocal(JNIEnv*, jobject ref) {
  Require(!runnable && locals.erase(ref) == 1, "Local reference leak or double deletion");
}
void JNICALL DeleteGlobal(JNIEnv*, jobject ref) {
  Require(!runnable && globals.erase(ref) == 1, "Global reference leak or double deletion");
  ++deletions;
}
jboolean JNICALL Exception(JNIEnv*) { return pending; }
void JNICALL Clear(JNIEnv*) { Require(!runnable, "Exception cleared inside ART"); pending = false; }
void* Decode(JavaVM* actual_vm, jobject ref) {
  Require(runnable && actual_vm == &vm && globals.count(ref), "Invalid global decode boundary");
  ++decodes;
  return reinterpret_cast<void*>(globals.at(ref));
}
void Check(JNIEnv* jni, int array_failure, int global_failure) {
  arrays = promotions = deletions = decodes = 0;
  fail_array = array_failure; fail_global = global_failure;
  auto roots = ClassArrayRoots::Create(jni, &vm, element, 7);
  Require(locals.empty() && !pending, "Native failure retained locals or an exception");
  if (array_failure || global_failure) {
    Require(!roots && globals.empty(), "Partial array preparation leaked a global root");
    return;
  }
  Require(roots && globals.size() == 2 && arrays == 2 && promotions == 2,
          "Array pair was not prepared and retained");
  runnable = true;
  void* before = roots->Old(Decode);
  Move();
  Require(roots->Old(Decode) != before && roots->New(Decode) != roots->Old(Decode),
          "A raw object was cached across movement or roots alias");
  Require(arrays == 2 && promotions == 2 && decodes == 4,
          "Runnable decoding called JNI or allocated arrays");
  runnable = false;
  auto second = ClassArrayRoots::Create(jni, &vm, element, 7);
  Require(second && globals.size() == 4, "Nested group lost earlier roots");
  second.reset(); roots.reset();
  Require(globals.empty() && deletions == 4, "Group lifetime leaked roots");
}
int main() {
  try {
    JNINativeInterface_ table{};
    table.NewObjectArray = NewArray; table.NewGlobalRef = Global;
    table.DeleteLocalRef = DeleteLocal; table.DeleteGlobalRef = DeleteGlobal;
    table.ExceptionCheck = Exception; table.ExceptionClear = Clear;
    JNIEnv jni{&table};
    Check(&jni, 0, 0);
    Check(&jni, 1, 0); Check(&jni, 2, 0);
    Check(&jni, 0, 1); Check(&jni, 0, 2);
    std::cout << "Native interface array roots: 5 cases PASS\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
'''


def main():
    helper = runpy.run_path(str(ROOT / 'tools/test-structural-policy-host.py'))
    include = helper['java_home'](None) / 'include'
    platform = 'darwin' if (include / 'darwin').is_dir() else 'linux'
    evidence = new_evidence_directory(ROOT / 'build/evidence/host/interface-array-roots')
    with tempfile.TemporaryDirectory(prefix='cases-', dir=evidence) as directory:
        work = Path(directory)
        source, binary = work / 'test.cc', work / 'test'
        source.write_text(SOURCE)
        result = subprocess.run([
            shutil.which('clang++') or shutil.which('c++'), '-std=c++17',
            '-Wall', '-Wextra', '-Werror', '-I' + str(ROOT / 'artdcevm/src/main/cpp'),
            '-I' + str(include), '-I' + str(include / platform),
            str(source), '-o', str(binary)], capture_output=True, text=True)
        (evidence / 'compiler-raw.log').write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError('Interface array compilation failed; inspect ignored evidence')
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        (evidence / 'cases.log').write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError('Interface array cases failed; inspect ignored evidence')
        print(result.stdout.strip())
    print('Evidence: ' + str(evidence.relative_to(ROOT)))


if __name__ == '__main__':
    main()
