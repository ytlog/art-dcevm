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
#include "platform/common/field_array_probe.h"
#include "platform/abi/arm64/android13/class.h"
#include "platform/abi/arm64/android16/class.h"
#include "platform/abi/arm64/android11/art_method.h"
#include "platform/abi/arm64/common/runtime_objects.h"
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
void MappingRequirements() {
  size_t accepted = 0;
  for (bool interface_target : {false, true}) {
    ClassSchema live = Base();
    if (interface_target) {
      live.access |= acc::kAccInterface | acc::kAccAbstract;
    }
    for (uint32_t method_access : {acc::kAccPublic, acc::kAccPrivate,
         acc::kAccPublic | acc::kAccStatic, acc::kAccPublic | acc::kAccAbstract}) {
      live.methods[0].access = method_access;
      for (int change = 0; change < 25; ++change) {
        auto before = live;
        auto next = live;
        policy::LoadedHierarchy hierarchy;
        bool mapping_change = false;
        switch (change) {
          case 0: break;  // Only method bodies differ, outside declaration input.
          case 1: next.fields.clear(); mapping_change = true; break;
          case 2: next.fields[0].descriptor = "J"; mapping_change = true; break;
          case 3: next.fields[0].access |= acc::kAccStatic; mapping_change = true; break;
          case 4: next.methods.erase(next.methods.begin()); mapping_change = true; break;
          case 5: next.methods[0].descriptor = "()J"; mapping_change = true; break;
          case 6:
            before.methods[0].access &= ~acc::kAccAbstract;
            before.methods[0].access |= acc::kAccNative;
            mapping_change = true; break;
          case 7: next.methods.push_back({"added", "()I", acc::kAccPublic});
            mapping_change = interface_target; break;
          case 8: next.methods.push_back({"added", "()I", acc::kAccPrivate});
            mapping_change = interface_target; break;
          case 9: next.methods.push_back({"added", "()I", acc::kAccPublic | acc::kAccStatic});
            mapping_change = interface_target; break;
          case 10: next.methods.push_back({"added", "()I", acc::kAccPublic | acc::kAccAbstract});
            mapping_change = interface_target; break;
          case 11: next.superclass = "Lfixture/Wider;";
            hierarchy.new_superclass.emplace();
            hierarchy.new_superclass->access = acc::kAccPublic;
            hierarchy.new_superclass->extends_old_superclass = true;
            mapping_change = true; break;
          case 12:
            next.methods.push_back({"copied", "()I", acc::kAccPublic});
            hierarchy.interface_copies.insert({"copied", "()I"});
            mapping_change = true; break;
          case 13:
            before.methods[0].access = acc::kAccPrivate;
            next.methods[0].access = acc::kAccPublic;
            hierarchy.interface_copies.insert({"work", "()I"});
            mapping_change = true; break;
          case 14: next.fields.push_back({"added", "J", acc::kAccPublic}); break;
          case 15: next.fields[0].access |= acc::kAccVolatile; break;
          case 16: next.methods[0].access |= acc::kAccSynchronized; break;
          case 17: next.interfaces.push_back("Lfixture/Added;");
            hierarchy.added_interfaces["Lfixture/Added;"].access = acc::kAccPublic | acc::kAccInterface;
            break;
          case 18: next.access |= acc::kAccAbstract; break;
          case 19: next.methods.push_back({"copied", "()I", acc::kAccPublic});
            hierarchy.interface_copies.insert({"copied", "()I"});
            hierarchy.inherited_public.insert({"copied", "()I"});
            mapping_change = interface_target; break;
          case 20: next.access |= acc::kAccFinal; break;
          case 21: next.access |= acc::kAccSynthetic; break;
          case 22: before.methods[0].access &= ~acc::kAccAbstract;
            next.methods[0].access = before.methods[0].access | acc::kAccNative; break;
          case 23: before.methods[0].access &= ~acc::kAccAbstract;
            before.methods[0].access |= acc::kAccNative;
            next.methods[0].access |= acc::kAccAbstract;
            mapping_change = true; break;
          case 24: next.fields.clear(); next.methods.erase(next.methods.begin());
            mapping_change = true; break;
        }
        // Additional combinations exercise dependency marks surviving multiple
        // accepted branches and disappearing when a later branch rejects.
        for (int combination = 0; combination < 4; ++combination) {
          auto incoming = next;
          if (combination & 1) incoming.fields.push_back({"extra", "I", acc::kAccPublic});
          if (combination & 2) incoming.methods[0].access ^= acc::kAccStatic;
          for (int bits = 0; bits < 64; ++bits) {
            policy::Abilities abilities;
            abilities.member_mapping = bits & 1;
            abilities.field_retirement = bits & 2;
            abilities.superclass_change = bits & 4;
            abilities.interface_members = bits & 8;
            abilities.interface_abstract_methods = bits & 16;
            abilities.instance_check = bits & 32;
            const auto result = policy::Evaluate(before, incoming, hierarchy, abilities);
            if (result.error != JVMTI_ERROR_NONE) {
              Require(!result.requires_member_mapping, "rejected decision carries a requirement");
              continue;
            }
            ++accepted;
            policy::Abilities instance_only;
            instance_only.instance_check = abilities.instance_check;
            const bool legacy_requirement = abilities.member_mapping &&
                policy::Evaluate(before, incoming, hierarchy, instance_only).error != JVMTI_ERROR_NONE;
            Require(result.requires_member_mapping == legacy_requirement,
                    "mapping requirement differs from the legacy two-Evaluate formula");
            Require(result.requires_member_mapping == mapping_change, "wrong mapping requirement");
          }
        }
      }
    }
  }
  Require(accepted > 1000, "too few accepted mapping combinations");
  std::cout << "mapping requirement combinations: " << accepted << " accepted\n";
}
int main(int argc, char** argv) {
  try {
    MappingRequirements();
    {
      // Exercise the same probe used by CheckClass. A failed slot declaration
      // must reject before dereferencing the deliberately invalid array.
      namespace abi = artdcevm::abi::arm64;
      void* type = reinterpret_cast<void*>(uintptr_t{0x1000});
      abi::ArtField slot{0x1000, acc::kAccPrivate, 0, 40};
      const auto lookup = [&](void* owner, std::string_view name, std::string_view descriptor) {
        return owner == type && name == "fields" && descriptor == "J" ? &slot : nullptr;
      };
      abi::LengthPrefixedArray<abi::ArtField> empty{0}, populated{2};
      uint32_t length = 99;
      const auto probe = [&](uint64_t array) {
        return class_layout::ProbeFieldArray(type, "fields", 40, array, lookup, &length);
      };
      Require(probe(reinterpret_cast<uintptr_t>(&empty)) && length == 0,
              "ART shared nonnull empty field array was rejected");
      Require(probe(0) && length == 0, "older ART null field array was rejected");
      Require(probe(reinterpret_cast<uintptr_t>(&populated)) && length == 2,
              "populated field array count differs");
      slot.offset = 48;
      Require(!probe(1), "wrong field slot offset was accepted");
      slot.offset = 40; slot.access_flags |= acc::kAccStatic;
      Require(!probe(1), "static field was accepted as a Class instance slot");
      slot.access_flags = acc::kAccPrivate; slot.declaring_class = 0x2000;
      Require(!probe(1), "field slot from a different class was accepted");
      Require(!class_layout::ProbeFieldArray(type, "missing", 40, 1, lookup, &length),
              "missing Class slot was accepted");
      Require(abi::android13::kFieldArrayNames[0] == std::string_view("iFields") &&
              abi::android13::kFieldArrayNames[1] == std::string_view("sFields") &&
              abi::android16::kFieldArrayNames[0] == std::string_view("fields"),
              "Class.java field array slot names differ");
    }
    {
      // ART's own default-conflict marks, which the member mapping reads after
      // linking. Android 12-17 combine copied/abstract/default; 11 has its own flag.
      namespace abi = artdcevm::abi::arm64;
      const auto modern = [](uint32_t flags) {
        abi::ArtMethod method{};
        method.access_flags = flags;
        return abi::IsDefaultConflicting(method);
      };
      const auto legacy = [](uint32_t flags) {
        abi::android11::ArtMethod method{};
        method.access_flags = flags;
        return abi::android11::IsDefaultConflicting(method);
      };
      constexpr uint32_t kAbstract = 0x0400, kDefault = 0x00400000, kCopied = 0x01000000,
                         kIntrinsic = 0x80000000;
      Require(modern(kCopied | kAbstract | kDefault | acc::kAccPublic),
              "Android 12-17 default conflict was not recognized");
      Require(!modern(kCopied | kAbstract) && !modern(kCopied | kDefault) &&
              !modern(kAbstract | kDefault) && !modern(kIntrinsic | kCopied | kAbstract | kDefault),
              "Android 12-17 miranda, copied default, declared method or intrinsic taken as a conflict");
      Require(legacy(kCopied) && legacy(kCopied | kAbstract) && !legacy(kIntrinsic | kCopied) &&
              !legacy(0x00100000 | kAbstract | kDefault),
              "Android 11 default conflict flag differs");
    }
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
    for (uint32_t flags : {acc::kAccProtected, uint32_t{0},
                          acc::kAccPublic | acc::kAccFinal,
                          acc::kAccPublic | acc::kAccDeclaredSynchronized,
                          acc::kAccPublic | acc::kAccNative}) {
      next = live; next.methods[0].access = flags; Accept(live, next, false, true);
    }
    // Verified callers keep invoking a method as the kind they were verified
    // against: virtual stays virtual and static stays static. Constructors and
    // private methods are invoked directly and may change access.
    for (uint32_t flags : {acc::kAccPrivate, acc::kAccPrivate | acc::kAccNative}) {
      next = live; next.methods[0].access = flags;
      Reject(live, next, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED);
    }
    {
      auto hidden = live; hidden.methods[0].access = acc::kAccPrivate;
      Accept(hidden, live, false, true);
      next = live; next.methods[1].access = acc::kAccPrivate;
      Accept(live, next, false, true);
      auto shared = live; shared.methods[0].access = acc::kAccPublic | acc::kAccStatic;
      next = shared; next.methods[0].access = acc::kAccPrivate | acc::kAccStatic;
      Accept(shared, next, false, true);
    }
    auto native = live; native.methods[0].access |= acc::kAccNative;
    Reject(native, live, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED);
    Require(policy::Evaluate(native, live, {}, {true}).error == JVMTI_ERROR_NONE,
            "native -> Java needs the engine's commit");
    next = live; next.methods[0].access |= acc::kAccAbstract;
    Accept(live, next, false, true);  // Concrete -> abstract.
    Accept(next, live, false, true);  // Abstract -> concrete.
    for (uint32_t flags : {acc::kAccStatic, uint32_t{0x80}, uint32_t{0x800}}) {
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
      // A method retired earlier comes back only as a kind its verified callers expect.
      auto removed = live; removed.methods.erase(removed.methods.begin());
      const policy::RetiredMethods virtual_work{{{"work", "()I"}, acc::kAccPublic}};
      const policy::RetiredMethods static_work{{{"work", "()I"}, acc::kAccPublic | acc::kAccStatic}};
      const policy::RetiredMethods private_work{{{"work", "()I"}, acc::kAccPrivate}};
      for (uint32_t flags : {acc::kAccPublic | acc::kAccStatic, uint32_t{acc::kAccPrivate}}) {
        next = removed; next.methods.push_back({"work", "()I", flags});
        Require(policy::Evaluate(removed, next, {}, {true}, virtual_work).error ==
                JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED,
                "retired virtual method came back with another kind");
      }
      for (uint32_t flags : {acc::kAccPublic, uint32_t{acc::kAccPrivate}}) {
        next = removed; next.methods.push_back({"work", "()I", flags});
        Require(policy::Evaluate(removed, next, {}, {true}, static_work).error ==
                JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED,
                "retired static method came back as an instance method");
      }
      next = removed; next.methods.push_back({"work", "()I", acc::kAccProtected | acc::kAccFinal});
      Require(policy::Evaluate(removed, next, {}, {true}, virtual_work).error == JVMTI_ERROR_NONE,
              "retired virtual method could not come back with another access level");
      next = removed; next.methods.push_back({"work", "()I", acc::kAccPublic});
      Require(policy::Evaluate(removed, next, {}, {true}, private_work).error == JVMTI_ERROR_NONE,
              "retired private method could not come back as a virtual method");
      next = removed; next.methods.push_back({"work", "(I)I", acc::kAccPublic | acc::kAccStatic});
      Require(policy::Evaluate(removed, next, {}, {true}, virtual_work).error == JVMTI_ERROR_NONE,
              "another signature was checked against a retired method");
    }
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
    for (uint32_t kind : {acc::kAccNative, acc::kAccAbstract}) {
      auto declared = live; declared.methods[0].access |= kind;
      next = declared; next.methods.erase(next.methods.begin());
      const auto retired = policy::Evaluate(declared, next, {}, {true});
      Require(retired.error == JVMTI_ERROR_NONE && retired.retired_methods.size() == 1,
              "native or abstract method retirement must be accepted with the mapping");
      Require(policy::Evaluate(declared, next, {}).error ==
              JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_DELETED,
              "retirement still needs the mapping");
    }
    {
      auto contract = live;
      contract.access |= acc::kAccInterface | acc::kAccAbstract;
      for (bool mapped : {false, true}) {
        const policy::Abilities abilities{mapped, mapped, true, mapped};
        auto expect = [&](ClassSchema candidate, jvmtiError code) {
          auto result = policy::Evaluate(contract, candidate, {}, abilities);
          Require(result.error == code && !result.reason.empty(), "interface error code or reason");
        };
        Require(policy::Evaluate(contract, contract, {}, abilities).error == JVMTI_ERROR_NONE,
                "interface body update must be accepted");
        next = contract; next.methods.push_back({"added", "()I", acc::kAccPublic});
        expect(next, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_ADDED);
        next = contract; next.methods.erase(next.methods.begin());
        expect(next, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_DELETED);
        next = contract; next.methods[0].descriptor = "(I)I";
        expect(next, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_DELETED);
        next = contract; next.methods[0].access |= acc::kAccAbstract;
        expect(next, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED);
        next = contract; next.fields.push_back({"added", "I", acc::kAccPublic | acc::kAccStatic});
        expect(next, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED);
        next = contract; next.fields.clear();
        expect(next, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED);
        next = contract; next.fields[0].access |= acc::kAccFinal;
        expect(next, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED);
        next = contract; next.interfaces.push_back("Lfixture/Parent;");
        expect(next, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED);
        auto parent = contract; parent.interfaces.push_back("Lfixture/Parent;");
        Require(policy::Evaluate(parent, contract, {}, abilities).error ==
                JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED, "interface parent removal");
        next = contract; next.access |= acc::kAccSynthetic;
        expect(next, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED);
      }
    }
    {
      // Interface members, shaped as Kotlin 2.3 + D8 emit them: a default
      // whose lambda lives in a private static method with a public static
      // $r8$lambda$ bridge, and a private instance helper.
      ClassSchema contract;
      contract.descriptor = "Lfixture/Contract;";
      contract.superclass = "Ljava/lang/Object;";
      contract.access = acc::kAccPublic | acc::kAccInterface | acc::kAccAbstract;
      const std::string text = "Ljava/lang/String;";
      const std::string lambda = "(Lfixture/Contract;)" + text;
      contract.methods = {{"name", "()" + text, acc::kAccPublic | acc::kAccAbstract},
                          {"label", "()" + text, acc::kAccPublic},
                          {"label$lambda$0", lambda, acc::kAccPrivate | acc::kAccStatic},
                          {"$r8$lambda$old", lambda,
                           acc::kAccPublic | acc::kAccStatic | acc::kAccSynthetic},
                          {"helper", "()I", acc::kAccPrivate}};
      const policy::Abilities members{true, true, true, true, true, true};
      const policy::Abilities body_only{true, true, true, true, false};
      const policy::Abilities unmapped{false, false, true, false, true};
      policy::LoadedHierarchy hierarchy;
      hierarchy.inherited_interface = {{"parent", "()I"}, {"hashCode", "()I"}};
      hierarchy.subtype_shadows[{"hidden", "()I"}] = "Lfixture/Base;";
      const auto accept = [&](const ClassSchema& candidate, size_t retired, jvmtiError former,
                              const char* what) {
        const auto result = policy::Evaluate(contract, candidate, hierarchy, members);
        Require(result.error == JVMTI_ERROR_NONE && result.methods_structural &&
                result.retired_methods.size() == retired, what);
        for (const auto& abilities : {body_only, unmapped}) {
          const auto off = policy::Evaluate(contract, candidate, hierarchy, abilities);
          Require(off.error == former && !off.reason.empty(),
                  "interface member change must keep its rejection without the ability");
        }
      };
      const auto reject = [&](const ClassSchema& candidate, jvmtiError code, const char* what,
                              const policy::RetiredMethods& retired = {}) {
        const auto result = policy::Evaluate(contract, candidate, hierarchy, members, retired);
        Require(result.error == code && !result.reason.empty(), what);
        return result.reason;
      };
      const auto with = [&](std::vector<Member> added) {
        auto candidate = contract;
        candidate.methods.insert(candidate.methods.end(), added.begin(), added.end());
        return candidate;
      };
      constexpr auto kAdded = JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_ADDED;
      constexpr auto kDeleted = JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_DELETED;
      constexpr auto kModifiers = JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED;
      Require(policy::Evaluate(contract, contract, hierarchy, members).error == JVMTI_ERROR_NONE,
              "interface body update must stay accepted");

      // A new function with a body and a lambda appended after the existing one.
      accept(with({{"extra", "()" + text, acc::kAccPublic},
                   {"access$extra$jd", lambda, acc::kAccPublic | acc::kAccStatic | acc::kAccSynthetic},
                   {"label$lambda$1", "()" + text, acc::kAccPrivate | acc::kAccStatic},
                   {"$r8$lambda$new", "()" + text,
                    acc::kAccPublic | acc::kAccStatic | acc::kAccSynthetic},
                   {"helper2", "()I", acc::kAccPrivate}}),
             0, kAdded, "interface default, static and private additions must be accepted");
      // A lambda inserted before the existing one renumbers and re-signatures
      // the direct lambda methods: retire the old ones, add the new ones.
      {
        auto inserted = contract;
        inserted.methods[2].descriptor = "()" + text;
        inserted.methods[3].name = "$r8$lambda$first";
        inserted.methods[3].descriptor = "()" + text;
        inserted.methods.push_back({"label$lambda$1", "(Lfixture/Contract;Lkotlin/jvm/functions/Function0;)" + text,
                                    acc::kAccPrivate | acc::kAccStatic});
        inserted.methods.push_back({"$r8$lambda$second", "(Lfixture/Contract;Lkotlin/jvm/functions/Function0;)" + text,
                                    acc::kAccPublic | acc::kAccStatic | acc::kAccSynthetic});
        accept(inserted, 2, kDeleted, "interface lambda insertion must retire and add direct methods");
      }
      {
        auto removed = contract; removed.methods.pop_back();
        accept(removed, 1, kDeleted, "interface private instance method retirement must be accepted");
      }
      // Virtual interface methods own iftable slots and still cannot retire.
      for (size_t index : {size_t{0}, size_t{1}}) {
        auto removed = contract; removed.methods.erase(removed.methods.begin() + index);
        reject(removed, kDeleted, "interface virtual method retirement must be rejected");
        auto resigned = contract; resigned.methods[index].descriptor = "(I)" + text;
        reject(resigned, kDeleted, "interface virtual method signature change must be rejected");
      }
      for (const auto& name : {"more", "hidden"}) {
        auto candidate = with({{name, "()I", acc::kAccPublic | acc::kAccAbstract}});
        const auto pending = policy::Evaluate(contract, candidate, hierarchy, members);
        Require(pending.error == JVMTI_ERROR_NONE && pending.methods_structural &&
                pending.interface_obligations == std::vector<policy::MemberKey>{{name, "()I"}},
                "abstract addition must carry a full-descriptor obligation, bypassing default shadows");
        auto no_dispatch = members; no_dispatch.interface_abstract_methods = false;
        const auto denied = policy::Evaluate(contract, candidate, hierarchy, no_dispatch);
        Require(denied.error == kAdded && denied.reason.find("IfTable accessor") != std::string::npos,
                "abstract addition must fail closed without the dispatch probe");
      }
      reject(with({{"packagedAbstract", "()I", acc::kAccAbstract}}), kAdded,
             "nonpublic abstract instance method must be rejected");
      reject(with({{"bound", "()I", acc::kAccPublic | acc::kAccStatic | acc::kAccNative}}), kAdded,
             "native interface method addition must be rejected");
      reject(with({{"<clinit>", "()V", acc::kAccStatic}}), kAdded,
             "interface initializer addition must be rejected");
      reject(with({{"packaged", "()I", 0}}), kAdded,
             "package-private interface instance method addition must be rejected");
      // Direct methods must not hide inherited interface or Object methods.
      reject(with({{"parent", "()I", acc::kAccPublic | acc::kAccStatic}}), kAdded,
             "static method hiding a superinterface method must be rejected");
      reject(with({{"hashCode", "()I", acc::kAccPrivate}}), kAdded,
             "private method hiding an Object method must be rejected");
      accept(with({{"parent", "()I", acc::kAccPublic}}), 0, kAdded,
             "default overriding a superinterface method must be accepted");
      // A default must not take over calls resolved by a static or non-public
      // method of a subtype; a direct addition cannot be resolved through them.
      const auto shadow = reject(with({{"hidden", "()I", acc::kAccPublic}}), kAdded,
                                 "default shadowing a subtype method must be rejected");
      Require(shadow.find("Lfixture/Base;") != std::string::npos,
              "shadow rejection must name the declaring class");
      accept(with({{"hidden", "()I", acc::kAccPublic | acc::kAccStatic}}), 0, kAdded,
             "static addition unrelated to virtual dispatch must be accepted");
      // Kept methods still keep their modifiers, and a retired identity keeps its kind.
      {
        auto widened = contract; widened.methods[2].access = acc::kAccPublic | acc::kAccStatic;
        reject(widened, kModifiers, "interface method modifier change must be rejected");
        const policy::RetiredMethods retired{{{"gone", "()I"}, acc::kAccPrivate | acc::kAccStatic}};
        reject(with({{"gone", "()I", acc::kAccPrivate}}), kModifiers,
               "retired static interface method must not return as an instance method", retired);
        Require(policy::Evaluate(contract, with({{"gone", "()I", acc::kAccPrivate | acc::kAccStatic}}),
                                 hierarchy, members, retired).error == JVMTI_ERROR_NONE,
                "retired static interface method may return as static");
      }
    }
    auto initializer = live;
    initializer.methods.push_back({"<clinit>", "()V", acc::kAccStatic});
    next = initializer; next.methods.pop_back();
    Require(policy::Evaluate(initializer, next, {}, {true}).error == JVMTI_ERROR_NONE,
            "class initializer retirement must be accepted");
    next = live; next.methods.pop_back();  // <init>()V
    Require(policy::Evaluate(live, next, {}, {true}).retired_methods.size() == 1,
            "constructor retirement must be accepted");
    // Superclass: only a resolved, accessible, non-final class extending the
    // old superclass, with the engine's member mapping.
    {
      next = live; next.superclass = "Lfixture/Parent;";
      Reject(live, next, JVMTI_ERROR_FAILS_VERIFICATION);  // Not resolved.
      policy::LoadedHierarchy widened;
      auto& parent = widened.new_superclass.emplace();
      policy::Abilities mapping;
      mapping.member_mapping = true;
      mapping.superclass_change = true;
      auto expect = [&](jvmtiError code, const char* what) {
        Require(policy::Evaluate(live, next, widened, mapping).error == code, what);
      };
      expect(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED, "unchecked superclass flags");
      parent.access = acc::kAccPublic;
      parent.extends_old_superclass = true;
      const auto accepted = policy::Evaluate(live, next, widened, mapping);
      Require(accepted.error == JVMTI_ERROR_NONE && accepted.superclass_changed &&
              accepted.methods_structural, "widening superclass change must be accepted");
      Require(policy::Evaluate(live, next, widened, {}).error ==
              JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED, "needs member mapping");
      parent.extends_old_superclass = false;
      expect(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED, "narrowing must be rejected");
      parent.extends_old_superclass = true;
      parent.extends_target = true;
      expect(JVMTI_ERROR_CIRCULAR_CLASS_DEFINITION, "circular superclass");
      parent.extends_target = false;
      parent.access = acc::kAccPublic | acc::kAccFinal;
      expect(JVMTI_ERROR_FAILS_VERIFICATION, "final superclass");
      parent.access = acc::kAccPublic | acc::kAccInterface | acc::kAccAbstract;
      expect(JVMTI_ERROR_FAILS_VERIFICATION, "interface as superclass");
      parent.access = 0;
      expect(JVMTI_ERROR_FAILS_VERIFICATION, "inaccessible superclass");
      parent.same_runtime_package = true;
      expect(JVMTI_ERROR_NONE, "package-private superclass in the same runtime package");
      parent.final_methods.insert({"work", "()I"});
      expect(JVMTI_ERROR_FAILS_VERIFICATION, "override of a final method");
      parent.final_methods.clear();
      widened.visible.insert({"other", "()V"});
      parent.final_methods.insert({"other", "()V"});
      expect(JVMTI_ERROR_FAILS_VERIFICATION, "subclass override of a final method");
      parent.final_methods.clear();
      parent.related_in_batch = true;
      expect(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED, "related class in batch");
    }
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
          std::vector<std::string> classes;
          Require(!evolution::ReadReferencedClasses(view, &classes, &error) && classes.empty(),
                  "truncated DEX type list accepted");
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
        std::vector<std::string> classes;
        Require(evolution::ReadReferencedClasses(view, &classes, &error) &&
                classes == std::vector<std::string>{"LA;", "LB;", "LC;"},
                "referenced classes not read");
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
    std::cout << "Structural policy and bounded DEX decoding PASS\n";
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
    """Two classes, one field_id A.value:I, one method_id B.work(I)J and a [[LC; type."""
    strings = ['I', 'J', 'LA;', 'LB;', 'value', 'work', 'JI', '[[LC;']
    string_ids, type_ids, proto_ids = 0x70, 0x70 + 4 * 8, 0x70 + 4 * 8 + 4 * 5
    field_ids = proto_ids + 12
    method_ids = field_ids + 8
    class_defs = method_ids + 8
    main = bytearray(class_defs + 2 * 32)
    data = bytearray() if compact else main
    main[:8] = b'cdex001\0' if compact else b'dex\n035\0'
    def put(buffer, offset, *values):
        struct.pack_into('<' + 'I' * len(values), buffer, offset, *values)
    put(main, 0x24, 0x70, 0x12345678)
    put(main, 0x38, 8, string_ids, 5, type_ids, 1, proto_ids, 1, field_ids, 1, method_ids,
        2, class_defs)
    for index, text in enumerate(strings):
        put(main, string_ids + 4 * index, len(data))
        data.extend(bytes([len(text)]) + text.encode('ascii') + b'\0')
    put(main, type_ids, 0, 1, 2, 3, 7)
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
