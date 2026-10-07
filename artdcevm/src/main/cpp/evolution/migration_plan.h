#pragma once
#include "evolution/class_schema.h"
#include <cstddef>
#include <limits>

namespace artdcevm::evolution {
enum Feature : uint32_t {
  kAddField = 1u << 0, kRemoveField = 1u << 1,
  kAddMethod = 1u << 2, kRemoveMethod = 1u << 3,
  kChangeFieldType = 1u << 4, kChangeMethodSignature = 1u << 5,
  kChangeSuperclass = 1u << 6, kChangeInterfaces = 1u << 7,
  kChangeModifiers = 1u << 8, kChangeStorage = 1u << 9,
};
inline constexpr size_t kAbsent = std::numeric_limits<size_t>::max();
enum class MemberAction { kRetain, kInitialize, kRetire };
struct MemberMapping {
  MemberAction action;
  size_t old_index = kAbsent;
  size_t new_index = kAbsent;
};
struct MigrationPlan {
  ClassSchema before;
  ClassSchema after;
  std::vector<MemberMapping> fields;
  std::vector<MemberMapping> methods;
  uint32_t required_features = 0;
  bool ChangesLayout() const {
    return required_features & (kAddField | kRemoveField | kChangeSuperclass | kChangeStorage);
  }
};
// A changed type/signature produces separate retire and initialize actions. Never reinterpret
// primitive bits as references, copy between unrelated types or guess overloaded member identity.
bool PlanMigration(const ClassSchema& before, const ClassSchema& after, MigrationPlan* output, std::string* error);
std::string DescribeMigration(const MigrationPlan& plan);
}
