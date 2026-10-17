#!/usr/bin/env python3
"""Exercise the production public hierarchy index with JNI/JVMTI table fakes."""
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "platform/common/hierarchy_snapshot.h"
#include "platform/common/schema_snapshot.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace artdcevm;
struct Type : _jclass {
  Type* parent;
  bool interface;
  jint status;
  jint hash;
  jobject loader = nullptr;
  jclass implemented = nullptr;
  Type(Type* p = nullptr, bool itf = false, jint state = JVMTI_CLASS_STATUS_PREPARED,
       jint identity_hash = 7) : parent(p), interface(itf), status(state), hash(identity_hash) {}
};
Type object;
int superclass_reads = 0, assignability_reads = 0, status_reads = 0, method_reads = 0, hash_reads = 0;
bool fail_hash = false;
jclass fail_status = nullptr;
void Require(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
jclass JNICALL Super(JNIEnv*, jclass c) { ++superclass_reads; return static_cast<Type*>(c)->parent; }
jclass JNICALL Find(JNIEnv*, const char*) { return &object; }
jboolean JNICALL Same(JNIEnv*, jobject a, jobject b) { return a == b; }
jboolean JNICALL Exception(JNIEnv*) { return false; }
void JNICALL Delete(JNIEnv*, jobject) {}
jboolean JNICALL Assign(JNIEnv*, jclass c, jclass target) {
  ++assignability_reads;
  for (auto* t = static_cast<Type*>(c); t; t = t->parent)
    if (t == target || t->implemented == target) return true;
  return static_cast<Type*>(c)->interface && target == &object;
}
jvmtiError JNICALL Hash(jvmtiEnv*, jobject c, jint* hash) {
  ++hash_reads;
  if (fail_hash) return JVMTI_ERROR_INTERNAL;
  *hash = static_cast<Type*>(c)->hash; return JVMTI_ERROR_NONE;
}
jvmtiError JNICALL Interface(jvmtiEnv*, jclass c, jboolean* value) {
  *value = static_cast<Type*>(c)->interface; return JVMTI_ERROR_NONE;
}
jvmtiError JNICALL Status(jvmtiEnv*, jclass c, jint* status) {
  ++status_reads;
  if (c == fail_status) return JVMTI_ERROR_INTERNAL;
  *status = static_cast<Type*>(c)->status; return JVMTI_ERROR_NONE;
}
jvmtiError JNICALL Loader(jvmtiEnv*, jclass c, jobject* loader) {
  *loader = static_cast<Type*>(c)->loader; return JVMTI_ERROR_NONE;
}
jvmtiError JNICALL Signature(jvmtiEnv*, jclass, char** signature, char**) {
  *signature = static_cast<char*>(std::malloc(20));
  std::strcpy(*signature, "Lsame/Name;"); return JVMTI_ERROR_NONE;
}
jvmtiError JNICALL Interfaces(jvmtiEnv*, jclass, jint* n, jclass** classes) {
  *n = 0; *classes = nullptr; return JVMTI_ERROR_NONE;
}
jvmtiError JNICALL Deallocate(jvmtiEnv*, unsigned char* p) { std::free(p); return JVMTI_ERROR_NONE; }
namespace artdcevm::live_schema {
int ReadLiveMethods(jvmtiEnv*, jclass, std::vector<evolution::Member>* out, std::string*) {
  ++method_reads; out->push_back({"work", "()I", evolution::kAccPublic}); return JVMTI_ERROR_NONE;
}
}
int main() {
  try {
    JNINativeInterface_ jt{};
    jt.GetSuperclass = Super; jt.FindClass = Find; jt.IsSameObject = Same;
    jt.ExceptionCheck = Exception; jt.DeleteLocalRef = Delete; jt.IsAssignableFrom = Assign;
    JNIEnv jni{&jt};
    jvmtiInterface_1 vt{};
    vt.GetObjectHashCode = Hash; vt.IsInterface = Interface; vt.GetClassStatus = Status;
    vt.GetClassLoader = Loader; vt.GetClassSignature = Signature;
    vt.GetImplementedInterfaces = Interfaces; vt.Deallocate = Deallocate;
    jvmtiEnv env{&vt};
    Type first(&object), second(&object), child(&first), grandchild(&child);
    Type unprepared(&first, false, 0), error(&first, false, JVMTI_CLASS_STATUS_PREPARED | JVMTI_CLASS_STATUS_ERROR);
    Type contract(nullptr, true), primitive(nullptr, false, JVMTI_CLASS_STATUS_PRIMITIVE);
    Type array(&object, false, JVMTI_CLASS_STATUS_ARRAY);
    // Reverse hierarchy order, same descriptors and colliding identity hashes.
    jclass loaded[] = {&grandchild, &unprepared, &second, &child, &contract, &primitive,
                       &error, &first, &array, &object};
    first.loader = child.loader = grandchild.loader = &object;
    second.loader = &primitive;  // Same class name, different defining loader.
    hierarchy_snapshot::LoadedIndex index;
    std::string reason;
    auto build = [&](std::vector<jclass> classes, std::vector<jclass> interfaces = {}) {
      return hierarchy_snapshot::BuildIndex(&env, &jni, 10, loaded, classes, interfaces, &index, &reason);
    };
    Require(build({&first, &child, &second}) == JVMTI_ERROR_NONE, "index rejected valid hierarchy");
    Require(superclass_reads == 0 && hash_reads == 0 && assignability_reads == 30 && status_reads == 6,
            "scan builds a graph, assigns hashes, repeats assignability or rereads overlapping status");
    Require(index.subtypes[0].size() == 5 && index.subtypes[1].size() == 2 &&
        index.subtypes[2].size() == 1, "loader identity, overlapping targets or target inclusion changed");
    Require(index.subtypes[0][0].klass == &grandchild && !index.subtypes[0][1].prepared,
            "loaded order or unprepared status changed");
    Require(index.affected.size() == 6, "retention union omitted unprepared/error types or duplicated overlaps");
    hierarchy_snapshot::Snapshot snapshot;
    snapshot.live.descriptor = "Lsame/Name;";
    Require(hierarchy_snapshot::CollectSubclasses(&env, &jni, &first, index.subtypes[0],
        &snapshot, &reason) == JVMTI_ERROR_NONE, "fact collection failed");
    Require(method_reads == 4 && snapshot.hierarchy.loaded_subclasses,
            "policy read an unprepared/target class or dropped a prepared error class");
    Require(build({&object}) == JVMTI_ERROR_NONE && index.affected.size() == 9,
            "Object lost interface/array assignability or gained primitive types");
    Require(build({&second}, {&contract}) == JVMTI_ERROR_NONE && index.affected.size() == 2,
            "mixed interface/class retention changed");
    unprepared.implemented = error.implemented = &contract;
    const int previous = superclass_reads;
    Require(build({}, {&contract}) == JVMTI_ERROR_NONE && index.affected.size() == 3 &&
            superclass_reads == previous, "interface-only retention lost unprepared/error types or builds an unused class graph");
    // Hash service failure cannot affect this scan or assign identity hashes to
    // any loaded class, even with overlapping ordinary and interface targets.
    fail_hash = true;
    Require(build({&first, &child}, {&contract}) == JVMTI_ERROR_NONE && hash_reads == 0 &&
            index.affected.size() == 6, "scan requested identity hashes");
    const auto previous_affected = index.affected;
    const auto previous_subtypes = index.subtypes;
    fail_status = &error;
    Require(build({&first}) == JVMTI_ERROR_INTERNAL && reason == "GetClassStatus failed" &&
            index.affected == previous_affected && index.subtypes.size() == previous_subtypes.size() &&
            index.subtypes[0].size() == previous_subtypes[0].size(),
            "status failure was lost or published a partial index");
    fail_status = nullptr;
    Require(build({}) == JVMTI_ERROR_NONE && index.affected.empty() && index.subtypes.empty(),
            "empty targets retained a previous index");
    std::cout << "Hierarchy snapshot identity, status, retention and traversal PASS\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
'''

def main():
    spec = importlib.util.spec_from_file_location('policy_host', ROOT / 'tools/test-structural-policy-host.py')
    policy = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(policy)
    include = policy.java_home(None) / 'include'
    platform = 'darwin' if (include / 'darwin').is_dir() else 'linux'
    native = ROOT / 'artdcevm/src/main/cpp'
    evidence = ROOT / 'build/evidence/host/hierarchy-snapshot'
    evidence.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / 'test.cc'
        binary = Path(directory) / 'test'
        source.write_text(SOURCE)
        command = [shutil.which('clang++') or shutil.which('c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                   '-I' + str(native), '-I' + str(include), '-I' + str(include / platform), str(source),
                   str(native / 'platform/common/hierarchy_snapshot.cc'),
                   str(native / 'evolution/dex_schema.cc'), '-o', str(binary)]
        for name, args in [('compile', command), ('run', [str(binary)])]:
            result = subprocess.run(args, capture_output=True, text=True)
            (evidence / (name + '.log')).write_text(result.stdout + result.stderr)
            if result.returncode:
                raise RuntimeError('Hierarchy host ' + name + ' failed; see build/evidence/host/hierarchy-snapshot')
        print(result.stdout.strip())

if __name__ == '__main__':
    main()
