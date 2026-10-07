#include "evolution/migration_plan.h"
#include "support/json.h"
#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

namespace artdcevm::evolution {
namespace {
using Key = std::tuple<std::string, std::string, bool>;
Key KeyOf(const Member& member) { return {member.name, member.descriptor, member.IsStatic()}; }
bool ValidateMembers(const std::vector<Member>& members, bool fields, std::string* error) {
  std::set<std::pair<std::string, std::string>> seen;
  for (const auto& member : members) {
    if (member.name.empty() || !seen.emplace(member.name, member.descriptor).second ||
        !(fields ? IsTypeDescriptor(member.descriptor, false) : IsMethodDescriptor(member.descriptor))) {
      *error = "Invalid or duplicate declared member in schema";
      return false;
    }
  }
  return true;
}
void MapMembers(const std::vector<Member>& before, const std::vector<Member>& after,
                bool fields, std::vector<MemberMapping>* mappings, uint32_t* features) {
  std::map<Key, size_t> new_index;
  for (size_t i = 0; i < after.size(); ++i) new_index.emplace(KeyOf(after[i]), i);
  std::vector<bool> matched(after.size());
  std::vector<size_t> removed, added;
  for (size_t i = 0; i < before.size(); ++i) {
    auto found = new_index.find(KeyOf(before[i]));
    if (found != new_index.end()) {
      size_t index = found->second;
      mappings->push_back({MemberAction::kRetain, i, index});
      matched[index] = true;
      if (before[i].access != after[index].access) *features |= kChangeModifiers;
    } else {
      mappings->push_back({MemberAction::kRetire, i, kAbsent});
      removed.push_back(i);
      *features |= fields ? kRemoveField : kRemoveMethod;
    }
  }
  for (size_t i = 0; i < after.size(); ++i) {
    if (matched[i]) continue;
    mappings->push_back({MemberAction::kInitialize, kAbsent, i});
    added.push_back(i);
    *features |= fields ? kAddField : kAddMethod;
  }
  for (size_t old : removed) for (size_t next : added) {
    if (before[old].name != after[next].name) continue;
    if (before[old].descriptor != after[next].descriptor)
      *features |= fields ? kChangeFieldType : kChangeMethodSignature;
    if (before[old].IsStatic() != after[next].IsStatic()) *features |= kChangeStorage;
  }
}
std::string Json(const std::string& value) { return QuoteJsonMutf8(value); }
void DescribeMembers(std::ostringstream& out, const std::vector<Member>& before,
                     const std::vector<Member>& after, const std::vector<MemberMapping>& mappings) {
  out << '[';
  bool first = true;
  for (const auto& mapping : mappings) {
    if (!first) out << ',';
    first = false;
    const auto& member = mapping.old_index != kAbsent ? before[mapping.old_index] : after[mapping.new_index];
    const char* action = mapping.action == MemberAction::kRetain ? "retain" :
                         mapping.action == MemberAction::kRetire ? "retire" : "initialize";
    out << "{\"action\":" << Json(action) << ",\"name\":" << Json(member.name)
        << ",\"descriptor\":" << Json(member.descriptor) << ",\"static\":" << (member.IsStatic() ? "true" : "false") << '}';
  }
  out << ']';
}
}
bool PlanMigration(const ClassSchema& before, const ClassSchema& after, MigrationPlan* output, std::string* error) {
  if (before.descriptor != after.descriptor || !IsTypeDescriptor(before.descriptor, false) || before.descriptor.front() != 'L') {
    *error = "Class identity changed"; return false;
  }
  if (!ValidateMembers(before.fields, true, error) || !ValidateMembers(after.fields, true, error) ||
      !ValidateMembers(before.methods, false, error) || !ValidateMembers(after.methods, false, error)) return false;
  MigrationPlan result;
  result.before = before; result.after = after;
  MapMembers(before.fields, after.fields, true, &result.fields, &result.required_features);
  MapMembers(before.methods, after.methods, false, &result.methods, &result.required_features);
  if (before.superclass != after.superclass) result.required_features |= kChangeSuperclass;
  auto old_interfaces = before.interfaces, new_interfaces = after.interfaces;
  std::sort(old_interfaces.begin(), old_interfaces.end()); std::sort(new_interfaces.begin(), new_interfaces.end());
  if (old_interfaces != new_interfaces) result.required_features |= kChangeInterfaces;
  if (before.access != after.access) result.required_features |= kChangeModifiers;
  *output = std::move(result); error->clear(); return true;
}
std::string DescribeMigration(const MigrationPlan& plan) {
  std::ostringstream out;
  out << "{\"class\":" << Json(plan.before.descriptor) << ",\"requiredFeatures\":" << plan.required_features
      << ",\"changesLayout\":" << (plan.ChangesLayout() ? "true" : "false")
      << ",\"oldSuperclass\":" << Json(plan.before.superclass) << ",\"newSuperclass\":" << Json(plan.after.superclass)
      << ",\"fields\":";
  DescribeMembers(out, plan.before.fields, plan.after.fields, plan.fields);
  out << ",\"methods\":";
  DescribeMembers(out, plan.before.methods, plan.after.methods, plan.methods);
  return out.str() + '}';
}
}
