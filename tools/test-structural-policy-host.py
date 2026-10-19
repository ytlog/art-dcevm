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
#include "evolution/modifiers.h"
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
      for (int change = 0; change < 27; ++change) {
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
          case 14: next.fields.push_back({"added", "J", acc::kAccPublic});
            mapping_change = interface_target; break;
          case 15: next.fields[0].access |= acc::kAccVolatile; break;
          case 16: next.methods[0].access |= acc::kAccSynchronized; break;
          case 17: next.interfaces.push_back("Lfixture/Added;");
            hierarchy.added_interfaces["Lfixture/Added;"].access = acc::kAccPublic | acc::kAccInterface;
            mapping_change = interface_target; break;
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
          case 25: next.fields[0].access |= acc::kAccSynthetic;
            mapping_change = interface_target; break;
          case 26: next.methods[0].access |= acc::kAccSynthetic;
            mapping_change = interface_target; break;
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
            // An interface accepts a field change only with the engine's mapping.
            Require(result.requires_member_mapping == (mapping_change || (interface_target && (combination & 1))),
                    "wrong mapping requirement");
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
    next = live; next.methods[0].access |= acc::kAccStatic;
    Reject(live, next, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED);
    {
      // With the loaded-caller check, the old kind retires and a scan is pending.
      policy::Abilities kinds;
      kinds.member_mapping = true;
      kinds.kind_changes = true;
      const auto changed = policy::Evaluate(live, next, {}, kinds);
      Require(changed.error == JVMTI_ERROR_NONE && changed.methods_structural && changed.requires_member_mapping &&
              changed.retired_methods.size() == 1 && changed.kind_changes.size() == 1 &&
              changed.kind_changes[0].second == live.methods[0].access,
              "invoke-kind change must retire the old kind and request a caller scan");
      auto hidden = live; hidden.methods[0].access = acc::kAccPrivate;
      Require(policy::Evaluate(live, hidden, {}, kinds).kind_changes.size() == 1,
              "virtual made private must request a caller scan");
      auto readded = live; readded.methods.push_back({"gone", "()I", acc::kAccPublic});
      const policy::RetiredMethods gone{{{"gone", "()I"}, acc::kAccPublic | acc::kAccStatic}};
      Require(policy::Evaluate(live, readded, {}, kinds, gone).kind_changes.size() == 1,
              "re-adding a retired identity as another kind must request a caller scan");
      kinds.member_mapping = false;
      Require(policy::Evaluate(live, next, {}, kinds).error == JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED,
              "invoke-kind change needs the engine's mapping");
    }
    {
      // An interface target retires the old kind the same way; the new kind's
      // declaration is checked like any interface addition.
      ClassSchema contract;
      contract.descriptor = "Lfixture/KindContract;";
      contract.superclass = "Ljava/lang/Object;";
      contract.access = acc::kAccPublic | acc::kAccInterface | acc::kAccAbstract;
      contract.methods = {{"value", "()I", acc::kAccPublic}};
      policy::Abilities kinds;
      kinds.member_mapping = true;
      kinds.kind_changes = true;
      kinds.interface_members = true;
      auto made_static = contract; made_static.methods[0].access = acc::kAccPublic | acc::kAccStatic;
      auto made_private = contract; made_private.methods[0].access = acc::kAccPrivate;
      auto made_abstract = contract; made_abstract.methods[0].access = acc::kAccPublic | acc::kAccAbstract;
      for (const auto* candidate : {&made_static, &made_private}) {
        const auto changed = policy::Evaluate(contract, *candidate, {}, kinds);
        Require(changed.error == JVMTI_ERROR_NONE && changed.methods_structural && changed.requires_member_mapping &&
                changed.retired_methods.size() == 1 && changed.kind_changes.size() == 1 &&
                changed.kind_changes[0].second == contract.methods[0].access,
                "interface default made static or private must retire it and request a caller scan");
      }
      const auto restored = policy::Evaluate(made_static, contract, {}, kinds);
      Require(restored.error == JVMTI_ERROR_NONE && restored.kind_changes.size() == 1,
              "interface static made default must request a caller scan");
      const auto abstracted = policy::Evaluate(made_static, made_abstract, {}, kinds);
      Require(abstracted.error == JVMTI_ERROR_NONE && abstracted.kind_changes.size() == 1,
              "interface static made abstract must be accepted like an added abstract, with a caller scan");
      const auto private_restored = policy::Evaluate(made_private, contract, {}, kinds);
      Require(private_restored.error == JVMTI_ERROR_NONE && private_restored.methods_structural &&
                  private_restored.requires_member_mapping && private_restored.kind_changes.empty(),
              "private interface method restored as default must relink without a caller scan");
      Require(policy::Evaluate(made_private, made_abstract, {}, kinds).error == JVMTI_ERROR_NONE,
              "private interface method restored as abstract must relink a miranda");
      policy::LoadedHierarchy inherited;
      inherited.inherited_interface = {{"value", "()I"}};
      Require(policy::Evaluate(contract, made_static, inherited, kinds).error ==
                  JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_ADDED,
              "interface method made static must not hide an inherited interface method");
      policy::LoadedHierarchy shadowed;
      shadowed.subtype_shadows[{"value", "()I"}] = "Lfixture/Impl;";
      Require(policy::Evaluate(made_static, contract, shadowed, kinds).error ==
                  JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_ADDED,
              "interface static made default must not shadow a subtype method");
      Require(policy::Evaluate(made_private, contract, shadowed, kinds).error ==
                  JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_ADDED,
              "private interface method restored as default must check subtype shadows");
      kinds.interface_members = false;
      Require(policy::Evaluate(made_private, contract, {}, kinds).error ==
                  JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED,
              "private interface restoration needs interface member mapping");
      Require(policy::Evaluate(contract, made_static, {}, kinds).error ==
                  JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED,
              "interface invoke-kind change needs interface member mapping");
    }
    // Bridge, varargs, strict and synthetic describe the method to reflection.
    for (uint32_t flags : {uint32_t{0x40}, uint32_t{0x80}, uint32_t{0x800}, uint32_t{0x1000}}) {
      next = live; next.methods[0].access |= flags;
      Accept(live, next, false, true);
      Accept(next, live, false, true);
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
        auto sealing = abilities; sealing.interface_removal = true;
        Require(policy::Evaluate(parent, contract, {}, sealing).error ==
                JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED, "interface parent removal with sealing");
        if (mapped) {
          // With the interface engine, an interface may drop a parent for its
          // whole implementation group, whose dispatch the engine seals.
          auto members = sealing; members.interface_members = true;
          const auto dropped = policy::Evaluate(parent, contract, {}, members);
          Require(dropped.error == JVMTI_ERROR_NONE && dropped.interfaces_removed &&
                  dropped.requires_member_mapping, "interface parent removal with sealing and members");
          members.interface_removal = false;
          Require(policy::Evaluate(parent, contract, {}, members).error ==
                  JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED, "interface parent removal needs sealing");
        }
        // An interface's class modifiers evolve like a class's; its kind does not.
        next = contract; next.access |= acc::kAccSynthetic;
        Require(policy::Evaluate(contract, next, {}, abilities).error == JVMTI_ERROR_NONE,
                "interface synthetic flag change must be accepted");
        next = contract; next.access &= ~acc::kAccPublic;
        Require(policy::Evaluate(contract, next, {}, abilities).error == JVMTI_ERROR_NONE,
                "interface access change must be accepted");
        next = contract; next.access |= 0x2000;
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
      const policy::Abilities members{true, true, true, true, true};
      const policy::Abilities body_only{true, true, true, true, false};
      const policy::Abilities unmapped{false, false, true, false, true};
      policy::LoadedHierarchy hierarchy;
      hierarchy.inherited_interface = {{"parent", "()I"}};
      hierarchy.inherited_public = {{"hashCode", "()I"}};
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
      // A retired abstract/default declaration takes its implementations'
      // copies with it; a new signature is an addition with its own rules.
      for (size_t index : {size_t{0}, size_t{1}}) {
        auto removed = contract; removed.methods.erase(removed.methods.begin() + index);
        accept(removed, 1, kDeleted, "interface virtual method retirement must be accepted");
        auto resigned = contract; resigned.methods[index].descriptor = "(I)" + text;
        accept(resigned, 1, kDeleted, "interface virtual method signature change must be accepted");
      }
      // abstract <-> default keeps the identity. As on JBR, an implementation
      // without a new abstract method throws AbstractMethodError at the call;
      // a new default follows the added-default shadow rule.
      {
        auto abstracted = contract; abstracted.methods[1].access |= acc::kAccAbstract;
        const auto pending = policy::Evaluate(contract, abstracted, hierarchy, members);
        Require(pending.error == JVMTI_ERROR_NONE && pending.methods_structural &&
                pending.requires_member_mapping,
                "default made abstract must be accepted");
        auto defaulted = contract; defaulted.methods[0].access &= ~acc::kAccAbstract;
        accept(defaulted, 0, kModifiers, "abstract made default must be accepted");
        auto hidden = with({{"hidden", "()I", acc::kAccPublic | acc::kAccAbstract}});
        auto shadowed = hidden; shadowed.methods.back().access = acc::kAccPublic;
        const auto shadow = policy::Evaluate(hidden, shadowed, hierarchy, members);
        Require(shadow.error == kModifiers && shadow.reason.find("Lfixture/Base;") != std::string::npos,
                "abstract made default must not shadow a subtype method");
        auto selection = members; selection.interface_selection = true;
        Require(policy::Evaluate(hidden, shadowed, hierarchy, selection).error == JVMTI_ERROR_NONE,
                "verified JBR selection must accept an abstract-to-default transition");
        auto packaged = contract; packaged.methods[1].access = acc::kAccAbstract;
        reject(packaged, kModifiers, "default made package-private abstract must be rejected");
      }
      // An added parent brings its virtual methods to every implementation.
      {
        auto parented = contract; parented.interfaces.push_back("Lfixture/Parent;");
        auto facts = hierarchy;
        facts.added_interfaces["Lfixture/Parent;"].access = acc::kAccPublic | acc::kAccInterface;
        facts.parent_methods["Lfixture/Parent;"] = {{{"required", "()I"}, true}, {{"provided", "()I"}, false}};
        facts.parent_methods["Lfixture/Grand;"] = {{{"grand", "()I"}, true}};
        const auto added = policy::Evaluate(contract, parented, facts, members);
        Require(added.error == JVMTI_ERROR_NONE && added.methods_structural && added.requires_member_mapping,
                "added parent interface with abstract methods must be accepted");
        constexpr auto kHierarchy = JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED;
        auto hiding = parented; hiding.methods.push_back({"provided", "()I", acc::kAccPrivate | acc::kAccStatic});
        Require(policy::Evaluate(contract, hiding, facts, members).error == kHierarchy,
                "a direct method hiding an added parent needs symbolic resolution");
        auto symbolic = members; symbolic.interface_symbols = true;
        for (auto own : {acc::kAccPrivate, acc::kAccPublic | acc::kAccStatic})
          for (bool abstract_parent : {false, true}) {
            auto before = contract; before.methods.push_back({"provided", "()I", own});
            auto after = before; after.interfaces.push_back("Lfixture/Parent;");
            auto parents = facts;
            parents.parent_methods["Lfixture/Parent;"] = {{{"provided", "()I"}, abstract_parent}};
            const auto inverse = policy::Evaluate(before, after, parents, symbolic);
            Require(inverse.error == JVMTI_ERROR_NONE && inverse.methods_structural && inverse.requires_member_mapping,
                    "existing private/static declaration may acquire a default/abstract parent");
            Require(policy::Evaluate(before, after, parents, members).error == kHierarchy,
                    "inverse parent collision requires the symbolic service");
          }
        auto shadowing = facts; shadowing.parent_methods["Lfixture/Parent;"].push_back({{"hidden", "()I"}, false});
        const auto shadow = policy::Evaluate(contract, parented, shadowing, members);
        Require(shadow.error == kHierarchy && shadow.reason.find("Lfixture/Base;") != std::string::npos,
                "a parent default shadowing a subtype method must be rejected");
        auto selection = members; selection.interface_selection = true;
        Require(policy::Evaluate(contract, parented, shadowing, selection).error == JVMTI_ERROR_NONE,
                "verified JBR selection must accept an inherited parent default");
        auto batched = facts; batched.added_interface_in_batch = "Lfixture/Parent;";
        Require(policy::Evaluate(contract, parented, batched, members).error == kHierarchy,
                "a parent redefined in the same batch must be rejected");
        const auto klass = Base();
        auto implementing = klass; implementing.interfaces.push_back("Lfixture/Parent;");
        Require(policy::Evaluate(klass, implementing, facts, members).error == JVMTI_ERROR_NONE &&
                policy::Evaluate(klass, implementing, batched, members).error == kHierarchy,
                "a class adding an interface redefined in the same batch needs the linked rebuild");
        auto linking = members; linking.batch_interfaces = true;
        Require(policy::Evaluate(klass, implementing, batched, linking).error == JVMTI_ERROR_NONE &&
                policy::Evaluate(contract, parented, batched, linking).error == JVMTI_ERROR_NONE,
                "an added interface the batch rebuilds must link as its replacement");
        Require(policy::Evaluate(parented, contract, facts, members).error == kHierarchy,
                "parent interface removal must be rejected");
        for (const auto& abilities : {body_only, unmapped})
          Require(policy::Evaluate(contract, parented, facts, abilities).error == kHierarchy,
                  "parent addition must keep its rejection without the ability");
      }
      // Interface statics evolve like class statics with the engine's mapping.
      {
        auto field = contract; field.fields.push_back({"TAG", text, acc::kAccPublic | acc::kAccStatic | acc::kAccFinal});
        const auto added = policy::Evaluate(contract, field, hierarchy, members);
        Require(added.error == JVMTI_ERROR_NONE && added.fields_structural && added.requires_member_mapping,
                "interface field addition must be accepted with the mapping");
        const auto retired = policy::Evaluate(field, contract, hierarchy, members);
        Require(retired.error == JVMTI_ERROR_NONE && retired.retired_fields.size() == 1,
                "interface field retirement must be accepted with the mapping");
        auto changed = field; changed.fields[0].access &= ~acc::kAccFinal;
        Require(policy::Evaluate(field, changed, hierarchy, members).error ==
                JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED, "interface field modifier change must be rejected");
        auto descriptive = field; descriptive.fields[0].access |= acc::kAccSynthetic;
        const auto described = policy::Evaluate(field, descriptive, hierarchy, members);
        Require(described.error == JVMTI_ERROR_NONE && described.fields_structural && described.requires_member_mapping,
                "interface field synthetic must use structural mapping");
        Require(policy::Evaluate(descriptive, field, hierarchy, members).error == JVMTI_ERROR_NONE,
                "interface field synthetic must restore");
        for (const auto& abilities : {body_only, unmapped})
          Require(policy::Evaluate(field, descriptive, hierarchy, abilities).error ==
                  JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED,
                  "interface field synthetic requires interface mapping");
        for (const auto& abilities : {body_only, unmapped})
          Require(policy::Evaluate(contract, field, hierarchy, abilities).error ==
                  JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED,
                  "interface field change must keep its rejection without the ability");
      }
      for (const auto& name : {"more", "hidden"}) {
        auto candidate = with({{name, "()I", acc::kAccPublic | acc::kAccAbstract}});
        const auto pending = policy::Evaluate(contract, candidate, hierarchy, members);
        Require(pending.error == JVMTI_ERROR_NONE && pending.methods_structural &&
                pending.requires_member_mapping,
                "abstract addition must be accepted, bypassing default shadows");
      }
      reject(with({{"packagedAbstract", "()I", acc::kAccAbstract}}), kAdded,
             "nonpublic abstract instance method must be rejected");
      reject(with({{"bound", "()I", acc::kAccPublic | acc::kAccStatic | acc::kAccNative}}), kAdded,
             "native interface method addition must be rejected");
      accept(with({{"<clinit>", "()V", acc::kAccStatic}}), 0, kAdded,
             "interface initializer addition must be accepted");
      reject(with({{"packaged", "()I", 0}}), kAdded,
             "package-private interface instance method addition must be rejected");
      // Direct methods must not hide inherited interface or Object methods.
      reject(with({{"parent", "()I", acc::kAccPublic | acc::kAccStatic}}), kAdded,
             "static method hiding a superinterface method must be rejected");
      reject(with({{"hashCode", "()I", acc::kAccPrivate}}), kAdded,
             "private method hiding an Object method must be rejected");
      for (uint32_t access : {acc::kAccPrivate, acc::kAccPublic | acc::kAccStatic}) {
        auto guarded = members; guarded.interface_symbols = true;
        const auto allowed = policy::Evaluate(contract, with({{"parent", "()I", access}}), hierarchy, guarded);
        Require(allowed.error == JVMTI_ERROR_NONE && allowed.requires_member_mapping,
                "interface symbolic guards must admit inherited private/static hiding");
        Require(policy::Evaluate(contract, with({{"hashCode", "()I", access}}), hierarchy, guarded).error == kAdded,
                "Object hiding remains outside the verified interface matrix");
      }
      accept(with({{"parent", "()I", acc::kAccPublic}}), 0, kAdded,
             "default overriding a superinterface method must be accepted");
      // A default must not take over calls resolved by a static or non-public
      // method of a subtype; a direct addition cannot be resolved through them.
      const auto shadow = reject(with({{"hidden", "()I", acc::kAccPublic}}), kAdded,
                                 "default shadowing a subtype method must be rejected");
      Require(shadow.find("Lfixture/Base;") != std::string::npos,
              "shadow rejection must name the declaring class");
      auto selection = members;
      selection.interface_selection = true;
      const auto selected = policy::Evaluate(contract, with({{"hidden", "()I", acc::kAccPublic}}), hierarchy, selection);
      Require(selected.error == JVMTI_ERROR_NONE && selected.methods_structural && selected.requires_member_mapping,
              "verified JBR selection must accept a default with a non-public class declaration");
      accept(with({{"hidden", "()I", acc::kAccPublic | acc::kAccStatic}}), 0, kAdded,
             "static addition unrelated to virtual dispatch must be accepted");
      // Kept methods still keep their modifiers, and a retired identity keeps its kind.
      {
        for (uint32_t before : {acc::kAccPublic, acc::kAccPrivate, acc::kAccPublic | acc::kAccStatic,
                                acc::kAccPublic | acc::kAccAbstract}) {
          for (uint32_t flag : {0x1000u, 0x40u, 0x80u, 0x800u, 0x10c0u}) {
            if ((before & acc::kAccAbstract) && (flag & 0x800u)) continue;
            auto original = contract; original.methods = {{"flags", "([I)I", before}};
            auto changed = original; changed.methods[0].access |= flag;
            for (const auto& pair : {std::make_pair(original, changed), std::make_pair(changed, original)}) {
              const auto result = policy::Evaluate(pair.first, pair.second, hierarchy, members);
              Require(result.error == JVMTI_ERROR_NONE && result.methods_structural && result.requires_member_mapping &&
                      result.kind_changes.empty() && result.retired_methods.empty(),
                      "descriptive flags must preserve method identity and invoke kind");
              Require(policy::Evaluate(pair.first, pair.second, hierarchy, body_only).error == kModifiers,
                      "interface descriptive flags require interface mapping");
            }
          }
        }
        auto original = contract; original.methods = {{"flags", "([I)I", acc::kAccPublic | acc::kAccAbstract}};
        auto changed = original; changed.methods[0].access = acc::kAccPublic | 0x10c0u;
        Require(policy::Evaluate(original, changed, hierarchy, members).error == JVMTI_ERROR_NONE &&
                policy::Evaluate(changed, original, hierarchy, members).error == JVMTI_ERROR_NONE,
                "descriptive bits must compose with abstract/default changes");
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
      // s1: the old ancestor adds members while C moves under B.
      auto ancestor = Base(), ancestor_next = ancestor;
      ancestor_next.fields.push_back({"added", "J", acc::kAccPublic});
      ancestor_next.methods.push_back({"extra", "()I", acc::kAccPublic});
      Require(policy::Evaluate(ancestor, ancestor_next, {}, mapping).error == JVMTI_ERROR_NONE &&
              policy::Evaluate(live, next, widened, mapping).superclass_changed, "s1 structural old ancestor");
      // s3/s5/s6: a rebuilt interface composes with a moving target or
      // a moving ancestor of an unsubmitted implementation. Loaded subtypes
      // do not prohibit these moves; only s2's moving-descendant fact does.
      auto contract = Base();
      contract.access = acc::kAccPublic | acc::kAccInterface | acc::kAccAbstract;
      contract.fields.clear(); contract.methods = {{"i", "()I", acc::kAccPublic}};
      auto contract_next = contract;
      contract_next.methods.push_back({"j", "()I", acc::kAccPublic});
      mapping.interface_members = true;
      for (int combination : {3, 5, 6}) {
        auto before = live, after = next;
        auto hierarchy = widened;
        if (combination == 5) before.interfaces = after.interfaces = {"Lfixture/I;"};
        hierarchy.loaded_subclasses = combination == 6;
        Require(policy::Evaluate(contract, contract_next, {}, mapping).error == JVMTI_ERROR_NONE &&
                policy::Evaluate(before, after, hierarchy, mapping).error == JVMTI_ERROR_NONE,
                "s3/s5/s6 interface and moving implementation chain");
      }
      // s4: the new-chain B moves too, but is outside C's old subtree.
      auto parent_before = live, parent_after = live;
      parent_before.descriptor = parent_after.descriptor = "Lfixture/Parent;";
      parent_after.superclass = "Lfixture/B2;";
      Require(policy::Evaluate(parent_before, parent_after, widened, mapping).superclass_changed &&
              policy::Evaluate(live, next, widened, mapping).error == JVMTI_ERROR_NONE,
              "s4 two moves in different old subtrees");
      // s2 alone preserves the explicit moving-descendant restriction.
      parent.moving_subclass_in_batch = true;
      expect(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED, "moving subclass in batch");
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
      // Removal: an ordinary class may drop an interface only when the engine
      // owns the mapping (vanishing copies) and seals interface dispatch.
      policy::Abilities removal;
      removal.member_mapping = true;
      Require(policy::Evaluate(implementing, live, {}, removal).error ==
              JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED, "removal needs dispatch sealing");
      removal.interface_removal = true;
      const auto dropped = policy::Evaluate(implementing, live, {}, removal);
      Require(dropped.error == JVMTI_ERROR_NONE && dropped.interfaces_removed &&
              dropped.requires_member_mapping && dropped.methods_structural, "interface removal accepted");
      Require(!policy::Evaluate(implementing, implementing, {}, removal).interfaces_removed,
              "unchanged interfaces are not removed");
      removal.member_mapping = false;
      Require(policy::Evaluate(implementing, live, {}, removal).error ==
              JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED, "removal needs member mapping");
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
      policy::Abilities kinds;
      kinds.member_mapping = kinds.interface_members = kinds.class_kinds = kinds.instance_check = true;
      auto iface = live;
      iface.access = acc::kAccPublic | acc::kAccInterface | acc::kAccAbstract;
      const auto conversion = policy::Evaluate(live, iface, {}, kinds);
      Require(conversion.error == JVMTI_ERROR_NONE && conversion.interface_kind_changed &&
              conversion.requires_no_instances && conversion.requires_instance_precheck &&
              conversion.requires_member_mapping && conversion.methods_structural,
              "class-to-interface must require a new layout, mapping and pre-allocation instance check");
      auto facts = policy::LoadedHierarchy{}; facts.loaded_subclasses = true;
      Require(policy::Evaluate(live, iface, facts, kinds).error == JVMTI_ERROR_FAILS_VERIFICATION &&
              policy::Evaluate(iface, live, facts, kinds).error == JVMTI_ERROR_FAILS_VERIFICATION,
              "class/interface conversion must not invalidate loaded subtypes");
      auto array_class = live; array_class.self_array_reference = true;
      Require(policy::Evaluate(iface, array_class, {}, kinds).error == JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,
              "self arrays must not mix old components with a different class/interface kind");
      auto annotation = iface; annotation.access |= acc::kAccAnnotation;
      const auto annotated = policy::Evaluate(iface, annotation, facts, kinds);
      Require(annotated.error == JVMTI_ERROR_NONE && !annotated.interface_kind_changed &&
              !annotated.requires_no_instances && annotated.requires_member_mapping,
              "annotation conversion must retain existing implementations");
      auto ordinary_enum = live; ordinary_enum.access |= acc::kAccEnum;
      Require(policy::Evaluate(live, ordinary_enum, {}, kinds).error == JVMTI_ERROR_NONE &&
              !policy::Evaluate(live, ordinary_enum, {}, kinds).requires_no_instances &&
              policy::Evaluate(live, ordinary_enum, {}, kinds).published_verification,
              "an enum-bit round trip with unchanged superclass must preserve live objects");
      auto changed_enum = ordinary_enum;
      changed_enum.methods[0].descriptor = "(I)I";
      Require(!policy::Evaluate(live, changed_enum, {}, kinds).published_verification,
              "changed declarations must never verify against the published class graph");
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
    Require(evolution::ClassModifiers(0xffffffff) == 0xffff &&
            evolution::FieldModifiers(0xffffffff) == 0xffff,
            "class/field flags not normalized");
    Require(evolution::MethodModifiers(acc::kAccAbstract | acc::kAccNative | acc::kAccConstructor) ==
            acc::kAccAbstract, "abstract/native/constructor flags not normalized");
    Require(evolution::MethodModifiers(acc::kAccSynchronized) == 0 &&
            evolution::MethodModifiers(acc::kAccDeclaredSynchronized) == acc::kAccSynchronized,
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
        std::vector<std::string> interfaces;
        Require(evolution::ReadClassInterfaces(view, 0, &interfaces, &error) &&
                interfaces == std::vector<std::string>{"LB;"}, "direct interface declarations not read");
        Require(evolution::ReadClassInterfaces(view, 1, &interfaces, &error) && interfaces.empty(),
                "interface declarations leaked between ClassDefs");
        interfaces = {"unchanged"};
        Require(!evolution::ReadClassInterfaces(view, 2, &interfaces, &error) &&
                interfaces == std::vector<std::string>{"unchanged"} && !error.empty(),
                "out-of-range ClassDef interface read was not atomic");
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
    interfaces = len(data)
    data.extend(struct.pack('<IH', 1, 3) + bytes(2))
    put(main, class_defs + 12, interfaces)
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
