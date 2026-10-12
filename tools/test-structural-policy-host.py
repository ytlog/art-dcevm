#!/usr/bin/env python3
"""Exercise Phase 2/method-only Phase 3 policy and bounded InnerClass DEX decoding."""
import argparse
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "platform/common/structural_policy.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
using namespace artdcevm;
using evolution::ClassSchema;
using evolution::Member;
namespace acc = evolution;
namespace policy = structural_policy;

void Require(bool condition, const char* description) {
  if (!condition) throw std::runtime_error(description);
}
ClassSchema Base() {
  ClassSchema schema;
  schema.descriptor = "Lfixture/Subject;";
  schema.superclass = "Ljava/lang/Object;";
  schema.access = acc::kAccPublic;
  schema.fields = {{"value", "I", acc::kAccPublic}};
  schema.methods = {{"work", "()I", acc::kAccPublic},
                    {"<init>", "()V", acc::kAccPublic}};
  return schema;
}
void Accept(const ClassSchema& live, const ClassSchema& next,
            bool fields, bool methods, const policy::SubclassMethods& subclasses = {}) {
  const auto result = policy::Evaluate(live, next, subclasses);
  Require(result.error == JVMTI_ERROR_NONE, "expected accepted evolution");
  Require(result.fields_structural == fields && result.methods_structural == methods,
          "wrong structural path decision");
}
void Reject(const ClassSchema& live, const ClassSchema& next, jvmtiError expected,
            const policy::SubclassMethods& subclasses = {}) {
  const auto result = policy::Evaluate(live, next, subclasses);
  Require(result.error == expected && !result.reason.empty(), "wrong rejection code or missing reason");
}
int main(int argc, char** argv) {
  try {
    const auto live = Base();
    Accept(live, live, false, false);  // Method bodies do not alter declarations.
    auto next = live;
    next.fields.push_back({"added", "I", 0});
    next.methods.push_back({"addedMethod", "()I", acc::kAccPublic});
    Accept(live, next, true, true);
    for (uint32_t flags : {acc::kAccPrivate, acc::kAccProtected, uint32_t{0},
                          acc::kAccPublic | acc::kAccFinal,
                          acc::kAccPublic | acc::kAccVolatile,
                          acc::kAccPublic | uint32_t{0x80}}) {
      next = live; next.fields[0].access = flags; Accept(live, next, true, false);
    }
    for (uint32_t flags : {acc::kAccPrivate, acc::kAccProtected, uint32_t{0},
                          acc::kAccPublic | acc::kAccFinal,
                          acc::kAccPublic | acc::kAccDeclaredSynchronized,
                          acc::kAccPrivate | acc::kAccNative}) {
      next = live; next.methods[0].access = flags; Accept(live, next, false, true);
    }
    auto native = live; native.methods[0].access |= acc::kAccNative;
    Reject(native, live, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED);
    for (uint32_t flags : {acc::kAccAbstract, acc::kAccStatic, uint32_t{0x80}, uint32_t{0x800}}) {
      next = live; next.methods[0].access |= flags;
      Reject(live, next, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED);
    }
    next = live; next.fields.clear();
    Reject(live, next, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED);
    next = live; next.fields[0].descriptor = "J";
    Reject(live, next, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED);
    next = live; next.fields[0].access |= acc::kAccStatic;
    Reject(live, next, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED);
    next = live; next.methods.erase(next.methods.begin());
    Reject(live, next, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_DELETED);
    next = live; next.methods[0].descriptor = "(I)I";
    Reject(live, next, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_DELETED);
    for (bool signature : {false, true}) {
      next = live;
      if (signature) next.methods[0].descriptor = "(I)I";
      else next.methods.erase(next.methods.begin());
      const auto retirement = policy::Evaluate(live, next, {}, true);
      Require(retirement.error == JVMTI_ERROR_NONE && retirement.methods_structural &&
              !retirement.fields_structural, "method-only retirement did not take structural path");
    }
    next = live; next.fields[0].descriptor = "J";
    Require(policy::Evaluate(live, next, {}, true).error ==
            JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED,
            "method retirement incorrectly enabled field type change");
    next = live; next.methods.erase(next.methods.begin() + 1);
    Require(policy::Evaluate(live, next, {}, true).error ==
            JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_DELETED,
            "constructor retirement must remain rejected");
    for (uint32_t unsupported : {acc::kAccNative, acc::kAccAbstract}) {
      auto restricted = live; restricted.methods[0].access |= unsupported;
      next = restricted; next.methods.erase(next.methods.begin());
      Require(policy::Evaluate(restricted, next, {}, true).error ==
              JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_DELETED,
              "native or abstract method retirement must remain rejected");
    }
    auto interface_schema = live;
    interface_schema.access |= acc::kAccInterface;
    next = interface_schema; next.methods.erase(next.methods.begin());
    Require(policy::Evaluate(interface_schema, next, {}, true).error ==
            JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_DELETED,
            "interface declaration retirement must remain rejected");
    auto initializer = live;
    initializer.methods.push_back({"<clinit>", "()V", acc::kAccStatic});
    next = initializer; next.methods.pop_back();
    Require(policy::Evaluate(initializer, next, {}, true).error ==
            JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_DELETED,
            "class initializer retirement must remain rejected");
    next = live; next.superclass = "Lfixture/Parent;";
    Reject(live, next, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED);
    next = live; next.interfaces.push_back("Lfixture/Interface;");
    Reject(live, next, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED);
    for (uint32_t flags : {uint32_t{0}, acc::kAccPublic | acc::kAccFinal,
                          acc::kAccPublic | acc::kAccAbstract, acc::kAccPublic | uint32_t{0x1000}}) {
      next = live; next.access = flags;
      Reject(live, next, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED);
    }
    policy::SubclassMethods subclasses;
    subclasses.visible.insert({"work", "()I"});
    next = live; next.methods[0].access |= acc::kAccFinal;
    Reject(live, next, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED, subclasses);
    auto package = live; package.methods[0].access = 0;
    next = package; next.methods[0].access |= acc::kAccFinal;
    Accept(package, next, false, true, subclasses);  // Different runtime package.
    subclasses.same_runtime_package = subclasses.visible;
    Reject(package, next, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED, subclasses);
    auto private_method = live; private_method.methods[0].access = acc::kAccPrivate;
    next = private_method; next.methods[0].access |= acc::kAccFinal;
    Accept(private_method, next, false, true, subclasses);
    next.methods[0].access = acc::kAccPublic | acc::kAccFinal;
    Reject(private_method, next, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED, subclasses);
    Require(policy::MethodModifiers(acc::kAccAbstract | acc::kAccNative | acc::kAccConstructor) ==
            acc::kAccAbstract, "abstract/native/constructor flags not normalized");
    Require(policy::MethodModifiers(acc::kAccSynchronized) == 0 &&
            policy::MethodModifiers(acc::kAccDeclaredSynchronized) == acc::kAccSynchronized,
            "declared synchronized not normalized");

    for (int argument = 1; argument < argc; ++argument) {
      std::ifstream input(argv[argument], std::ios::binary);
      const std::vector<uint8_t> dex((std::istreambuf_iterator<char>(input)), {});
      Require(!dex.empty(), "missing DEX case");
      ClassSchema schema; schema.descriptor = "unchanged";
      std::string error;
      const bool valid = evolution::ReadClassDeclarations(
          {dex.data(), dex.size(), dex.data(), dex.size()}, &schema, &error);
      const std::string path = argv[argument];
      if (path.find("invalid") != std::string::npos) {
        Require(!valid && schema.descriptor == "unchanged" && !error.empty(),
                "invalid DEX did not fail atomically");
      } else if (path.find("absent") != std::string::npos) {
        Require(valid && !schema.reflection_access, "non-system InnerClass annotation used");
      } else {
        Require(valid && schema.access == acc::kAccPublic &&
                schema.reflection_access == (acc::kAccPrivate | acc::kAccStatic),
                "InnerClass flags lost or raw ClassDef flags overwritten");
        auto nested_live = schema;
        nested_live.access = *schema.reflection_access;
        Accept(nested_live, schema, false, false);
        schema.reflection_access = acc::kAccPublic | acc::kAccStatic;
        Reject(nested_live, schema, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED);
        schema.reflection_access = nested_live.access;
        schema.access |= acc::kAccFinal;
        Reject(nested_live, schema, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED);
      }
    }
    std::cout << "Phase 2/method-only Phase 3 policy and bounded InnerClass DEX decoding PASS\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
'''


def dex_case(visibility=2):
    """Small declaration-only DEX with the real InnerClass annotation shape."""
    strings = ['Lfixture/Outer$Nested;', 'Ljava/lang/Object;',
               'Ldalvik/annotation/InnerClass;', 'accessFlags', 'name', 'Nested']
    string_ids = 0x70
    type_ids = string_ids + 4 * len(strings)
    class_def = type_ids + 3 * 4
    data = bytearray(class_def + 32)
    data[:8] = b'dex\n035\0'
    def put(offset, *values):
        struct.pack_into('<' + 'I' * len(values), data, offset, *values)
    put(0x24, 0x70, 0x12345678)
    put(0x38, len(strings), string_ids, 3, type_ids)
    put(0x60, 1, class_def)
    for index, text in enumerate(strings):
        put(string_ids + index * 4, len(data))
        data.extend(bytes([len(text)]) + text.encode('ascii') + b'\0')
    put(type_ids, 0, 1, 2)
    while len(data) % 4:
        data.append(0)
    directory = len(data); data.extend(bytes(16))
    annotation_set = len(data); data.extend(bytes(8))
    annotation = len(data)
    # system visibility, type index, element count, name index, VALUE_INT, private|static,
    # name index, VALUE_STRING, name string index.
    data.extend(bytes([visibility, 2, 2, 3, 0x04, 0x0a, 4, 0x17, 5]))
    put(directory, annotation_set)
    put(annotation_set, 1, annotation)
    put(class_def, 0, 1, 1, 0, 0xffffffff, directory, 0, 0)
    put(0x20, len(data))
    return data, directory, annotation_set, annotation


def java_home(requested):
    candidates = [requested, os.environ.get('JAVA_HOME')]
    if Path('/usr/libexec/java_home').exists():
        result = subprocess.run(['/usr/libexec/java_home'], capture_output=True, text=True)
        if result.returncode == 0:
            candidates.append(result.stdout.strip())
    javac = shutil.which('javac')
    if javac:
        candidates.append(str(Path(javac).resolve().parent.parent))
    for candidate in candidates:
        if candidate and (Path(candidate) / 'include/jvmti.h').is_file():
            return Path(candidate)
    raise RuntimeError('Set JAVA_HOME or --java to a JDK with JNI/JVMTI headers')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--java')
    args = parser.parse_args()
    java = java_home(args.java)
    include = java / 'include'
    platform = 'darwin' if (include / 'darwin').is_dir() else 'linux'
    evidence = ROOT / 'build/evidence/host/structural-policy'
    evidence.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='cases-', dir=evidence) as directory:
        work = Path(directory)
        source = work / 'test.cc'; source.write_text(SOURCE)
        binary = work / 'test'
        native = ROOT / 'artdcevm/src/main/cpp'
        command = [shutil.which('clang++') or shutil.which('c++'), '-std=c++17',
                   '-Wall', '-Wextra', '-Werror', '-I' + str(native),
                   '-I' + str(include), '-I' + str(include / platform), str(source),
                   str(native / 'platform/common/structural_policy.cc'),
                   str(native / 'evolution/dex_schema.cc'), '-o', str(binary)]
        build = subprocess.run(command, capture_output=True, text=True)
        (evidence / 'compile.log').write_text(build.stdout + build.stderr)
        if build.returncode:
            raise RuntimeError('Host compilation failed; see build/evidence/host/structural-policy/compile.log')
        valid, directory_offset, set_offset, annotation_offset = dex_case()
        cases = {'nested.dex': valid, 'absent.dex': dex_case(1)[0]}
        for count in range(1, len(valid) - annotation_offset + 1):
            truncated = valid[:-count]
            struct.pack_into('<I', truncated, 0x20, len(truncated))
            cases[f'invalid-truncated-{count}.dex'] = truncated
        malformed = bytearray(valid); struct.pack_into('<I', malformed, set_offset, 0xffffffff)
        cases['invalid-set-count.dex'] = malformed
        malformed = bytearray(valid); struct.pack_into('<I', malformed, directory_offset, 0xffffffff)
        cases['invalid-set-offset.dex'] = malformed
        malformed = bytearray(valid); malformed[annotation_offset + 4] = 0x84
        cases['invalid-integer-width.dex'] = malformed
        paths = []
        for name, contents in cases.items():
            path = work / name; path.write_bytes(contents); paths.append(str(path))
        test = subprocess.run([str(binary), *paths], capture_output=True, text=True)
        (evidence / 'test.log').write_text(test.stdout + test.stderr)
        if test.returncode:
            raise RuntimeError('Host assertions failed; see build/evidence/host/structural-policy/test.log')
        print(test.stdout.strip())


if __name__ == '__main__':
    main()
