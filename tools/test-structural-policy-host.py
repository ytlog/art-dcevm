#!/usr/bin/env python3
"""Exercise the structural policy and bounded InnerClass DEX decoding."""
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
#include <utility>
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
            bool fields, bool methods, const policy::LoadedHierarchy& subclasses = {}) {
  const auto result = policy::Evaluate(live, next, subclasses);
  Require(result.error == JVMTI_ERROR_NONE, "expected accepted evolution");
  Require(result.fields_structural == fields && result.methods_structural == methods,
          "wrong structural path decision");
}
void Reject(const ClassSchema& live, const ClassSchema& next, jvmtiError expected,
            const policy::LoadedHierarchy& subclasses = {}) {
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
      const auto retirement = policy::Evaluate(live, next, {}, {true});
      Require(retirement.error == JVMTI_ERROR_NONE && retirement.methods_structural &&
              !retirement.fields_structural, "method-only retirement did not take structural path");
      Require(retirement.retired_methods.size() == 1 &&
              retirement.retired_methods[0].name == "work" &&
              retirement.retired_methods[0].descriptor == "()I",
              "policy did not report the retired declaration");
    }
    Require(policy::Evaluate(live, live, {}, {true}).retired_methods.empty(),
            "unchanged declarations reported as retired");
    {
      // A loaded subclass may own a copied default/miranda for copied()I.
      policy::LoadedHierarchy copies;
      copies.interface_copies.insert({"copied", "()I"});
      next = live; next.methods.push_back({"copied", "()I", acc::kAccPublic});
      Reject(live, next, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_ADDED, copies);
      Require(policy::Evaluate(live, next, copies, {true}).error == JVMTI_ERROR_NONE,
              "owned member mapping must remap a vanished copied method");
      for (uint32_t flags : {uint32_t{acc::kAccPrivate}, uint32_t{acc::kAccStatic}}) {
        next = live; next.methods.push_back({"copied", "()I", flags});
        Accept(live, next, false, true, copies);  // No vtable slot.
      }
      auto hidden = live; hidden.methods.push_back({"copied", "()I", acc::kAccPrivate});
      next = hidden; next.methods.back().access = acc::kAccPublic;
      Reject(hidden, next, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED, copies);
      auto exposed = live; exposed.methods.push_back({"copied", "()I", acc::kAccPublic});
      next = exposed; next.methods.back().access = acc::kAccPublic | acc::kAccFinal;
      Accept(exposed, next, false, true, copies);  // Same access level keeps the slot owner.
      copies.inherited_public.insert({"copied", "()I"});
      next = live; next.methods.push_back({"copied", "()I", acc::kAccPublic});
      Accept(live, next, false, true, copies);  // A superclass already implements the slot.
    }
    next = live; next.fields[0].descriptor = "J";
    Require(policy::Evaluate(live, next, {}, {true}).error ==
            JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED,
            "method retirement incorrectly enabled field type change");
    next = live; next.methods.erase(next.methods.begin() + 1);
    Require(policy::Evaluate(live, next, {}, {true}).error ==
            JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_DELETED,
            "constructor retirement must remain rejected");
    {
      // Field retirement needs both the owned mapping and the field guards.
      auto fields = live;
      fields.fields.push_back({"total", "J", acc::kAccStatic | acc::kAccPrivate});
      for (int change = 0; change < 3; ++change) {
        next = fields;
        if (change == 0) next.fields.erase(next.fields.begin());          // Removed.
        if (change == 1) next.fields[0].descriptor = "J";                 // Type changed.
        if (change == 2) next.fields[0].access |= acc::kAccStatic;        // Storage changed.
        Require(policy::Evaluate(fields, next, {}, {true, false}).error ==
                JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED,
                "field retirement enabled without field guards");
        Require(policy::Evaluate(fields, next, {}, {false, true}).error ==
                JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED,
                "field retirement enabled without the owned mapping");
        const auto retired = policy::Evaluate(fields, next, {}, {true, true});
        Require(retired.error == JVMTI_ERROR_NONE && retired.fields_structural &&
                retired.retired_fields.size() == 1 && retired.retired_fields[0].name == "value" &&
                retired.retired_fields[0].descriptor == "I" && retired.retired_methods.empty(),
                "field retirement not reported");
      }
      next = fields; next.fields.clear();
      Require(policy::Evaluate(fields, next, {}, {true, true}).retired_fields.size() == 2,
              "static field retirement not reported");
      Require(policy::Evaluate(fields, fields, {}, {true, true}).retired_fields.empty(),
              "unchanged fields reported as retired");
    }
    for (uint32_t unsupported : {acc::kAccNative, acc::kAccAbstract}) {
      auto restricted = live; restricted.methods[0].access |= unsupported;
      next = restricted; next.methods.erase(next.methods.begin());
      Require(policy::Evaluate(restricted, next, {}, {true}).error ==
              JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_DELETED,
              "native or abstract method retirement must remain rejected");
    }
    auto interface_schema = live;
    interface_schema.access |= acc::kAccInterface;
    next = interface_schema; next.methods.erase(next.methods.begin());
    Require(policy::Evaluate(interface_schema, next, {}, {true}).error ==
            JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_DELETED,
            "interface declaration retirement must remain rejected");
    auto initializer = live;
    initializer.methods.push_back({"<clinit>", "()V", acc::kAccStatic});
    next = initializer; next.methods.pop_back();
    Require(policy::Evaluate(initializer, next, {}, {true}).error ==
            JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_DELETED,
            "class initializer retirement must remain rejected");
    next = live; next.superclass = "Lfixture/Parent;";
    Reject(live, next, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED);
    // Interfaces: additions need a type resolved through the target's loader
    // and its runtime flags; removals and duplicates stay rejected.
    {
      next = live; next.interfaces.push_back("Lfixture/Interface;");
      Reject(live, next, JVMTI_ERROR_FAILS_VERIFICATION);  // Not resolved.
      policy::LoadedHierarchy resolved;
      auto& added = resolved.added_interfaces["Lfixture/Interface;"];
      Reject(live, next, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED, resolved);
      added.access = acc::kAccPublic | acc::kAccInterface | acc::kAccAbstract;
      Accept(live, next, false, true, resolved);
      added.access = acc::kAccPublic;
      Reject(live, next, JVMTI_ERROR_FAILS_VERIFICATION, resolved);  // A class.
      added.access = acc::kAccInterface | acc::kAccAbstract;
      Reject(live, next, JVMTI_ERROR_FAILS_VERIFICATION, resolved);  // Package-private.
      added.same_runtime_package = true;
      Accept(live, next, false, true, resolved);
      auto implementing = next;
      Reject(implementing, live, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED);
      policy::LoadedHierarchy checked;
      checked.narrowing_checked = live.interfaces;
      policy::Abilities mapping;
      mapping.member_mapping = true;
      Require(policy::Evaluate(implementing, live, checked, {}).error ==
              JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,
              "interface removal needs the member mapping");
      Require(policy::Evaluate(implementing, live, {}, mapping).error ==
              JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,
              "interface removal needs the narrowing check");
      const auto removal = policy::Evaluate(implementing, live, checked, mapping);
      Require(removal.error == JVMTI_ERROR_NONE && removal.methods_structural,
              "checked interface removal must take the structural path");
      checked.narrowing_checked = next.interfaces;
      Require(policy::Evaluate(implementing, live, checked, mapping).error ==
              JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,
              "a check of another interface list must not count");
      auto duplicated = next; duplicated.interfaces.push_back("Lfixture/Interface;");
      Reject(live, duplicated, JVMTI_ERROR_FAILS_VERIFICATION, resolved);
      implementing.interfaces.push_back("Lfixture/Other;");
      auto reordered = implementing;
      std::swap(reordered.interfaces[0], reordered.interfaces[1]);
      Accept(implementing, reordered, false, true);  // Order alone: no resolution needed.
    }
    // Class modifiers: visibility, final, synthetic and (with an instance
    // check) abstract may change; every change takes the structural path.
    for (uint32_t flags : {uint32_t{0}, acc::kAccPublic | acc::kAccFinal,
                          acc::kAccPublic | uint32_t{0x1000}}) {
      next = live; next.access = flags;
      Accept(live, next, false, true);
    }
    {
      policy::LoadedHierarchy extended;
      extended.loaded_subclasses = true;
      next = live; next.access |= acc::kAccFinal;
      Reject(live, next, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED, extended);
      auto final_live = live; final_live.access |= acc::kAccFinal;
      Accept(final_live, live, false, true, extended);  // Dropping final is always allowed.
      next = live; next.access |= acc::kAccAbstract;
      Reject(live, next, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED);
      policy::Abilities checks;
      checks.instance_check = true;
      const auto abstract = policy::Evaluate(live, next, {}, checks);
      Require(abstract.error == JVMTI_ERROR_NONE && abstract.requires_no_instances &&
              abstract.methods_structural, "abstract class change without instance requirement");
      Require(!policy::Evaluate(live, live, {}, checks).requires_no_instances,
              "unchanged class requires an instance check");
      for (uint32_t kind : {acc::kAccInterface | acc::kAccAbstract, uint32_t{0x4000}}) {
        next = live; next.access |= kind;
        Reject(live, next, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED);
      }
    }
    policy::LoadedHierarchy subclasses;
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
      const std::string name = argv[argument];
      if (name.find("identity") != std::string::npos) {
        // CompactDex keeps string data and type lists in a separate section.
        std::ifstream data_input(name + ".data", std::ios::binary);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(data_input)), {});
        if (data.empty()) data = dex;
        const evolution::DexView view{dex.data(), dex.size(), data.data(), data.size()};
        Member identity;
        std::string error;
        if (name.find("invalid") != std::string::npos) {
          Require(!evolution::ReadFieldIdentity(view, 0, &identity, &error) && !error.empty() &&
                  !evolution::ReadMethodIdentity(view, 0, &identity, &error),
                  "truncated DEX identity accepted");
          continue;
        }
        Require(evolution::ReadFieldIdentity(view, 0, &identity, &error) &&
                identity.name == "value" && identity.descriptor == "I",
                "field identity not read");
        Require(evolution::ReadMethodIdentity(view, 0, &identity, &error) &&
                identity.name == "work" && identity.descriptor == "(I)J",
                "method identity not read");
        Require(!evolution::ReadFieldIdentity(view, 1, &identity, &error) && !error.empty() &&
                !evolution::ReadMethodIdentity(view, 1, &identity, &error),
                "out-of-range identity accepted");
        ClassSchema schema;
        Require(!evolution::ReadClassDeclarations(view, &schema, &error),
                "multi-class or compact DEX accepted as a redefinition class");
        continue;
      }
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
        Accept(nested_live, schema, false, true);  // InnerClass visibility may change.
        schema.reflection_access = nested_live.access;
        schema.access |= acc::kAccFinal;
        Accept(nested_live, schema, false, true);
        schema.access |= acc::kAccInterface;
        Reject(nested_live, schema, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED);
      }
    }
    std::cout << "Phase 2-4 policy and bounded DEX decoding PASS\n";
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


def identity_case(compact=False):
    """Two classes, one field_id A.value:I and one method_id B.work(I)J."""
    strings = ['I', 'J', 'LA;', 'LB;', 'value', 'work', 'JI']
    string_ids, type_ids, proto_ids = 0x70, 0x70 + 4 * 7, 0x70 + 4 * 7 + 4 * 4
    field_ids = proto_ids + 12
    method_ids = field_ids + 8
    class_defs = method_ids + 8
    main = bytearray(class_defs + 2 * 32)
    data = bytearray() if compact else main
    main[:8] = b'cdex001\0' if compact else b'dex\n035\0'
    def put(buffer, offset, *values):
        struct.pack_into('<' + 'I' * len(values), buffer, offset, *values)
    put(main, 0x24, 0x70, 0x12345678)
    put(main, 0x38, 7, string_ids, 4, type_ids, 1, proto_ids, 1, field_ids, 1, method_ids,
        2, class_defs)
    for index, text in enumerate(strings):
        put(main, string_ids + 4 * index, len(data))
        data.extend(bytes([len(text)]) + text.encode('ascii') + b'\0')
    put(main, type_ids, 0, 1, 2, 3)
    while len(data) % 4:
        data.append(0)
    type_list = len(data)
    data.extend(struct.pack('<IH', 1, 0) + bytes(2))
    put(main, proto_ids, 6, 1, type_list)
    struct.pack_into('<HHI', main, field_ids, 2, 0, 4)
    struct.pack_into('<HHI', main, method_ids, 3, 0, 5)
    put(main, class_defs, 2)
    put(main, class_defs + 32, 3)
    put(main, 0x20, len(main))
    return main, (data if compact else None)


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
        for compact in (False, True):
            main, data = identity_case(compact)
            name = 'identity-compact.dex' if compact else 'identity-standard.dex'
            cases[name] = main
            if data is not None:
                (work / (name + '.data')).write_bytes(data)
            truncated = bytearray(main[:-40]); struct.pack_into('<I', truncated, 0x20, len(truncated))
            cases['invalid-' + name] = truncated
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
