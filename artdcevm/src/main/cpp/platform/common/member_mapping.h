#pragma once
#include <string>
#include <vector>
#include "platform/common/art_hook.h"
#include "platform/common/class_layout.h"

// Member mapping plans and their commit hooks. The engine owns the last
// fallible ART hook, its checks and the installation of all module hooks.
namespace artdcevm::member_mapping {
bool Prepare(const class_layout::Services& services, std::vector<Hook>* hooks, std::string* failure);
// Save a candidate plan in this transaction. Only ART's later commit hooks
// apply it; rejection destroys it with the context.
bool Plan(const runtime_thread::VerifiedThread& thread,
          abi::arm64::ClassRedefinitionPrefix* self, const abi::arm64::RedefinitionDataIter& data,
          int selected, std::string* why);
}  // namespace artdcevm::member_mapping
