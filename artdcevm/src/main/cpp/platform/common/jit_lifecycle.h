#pragma once
#include <string>
#include <vector>
#include "platform/common/art_hook.h"
namespace artdcevm::jit_lifecycle {
bool Prepare(std::vector<Hook>* hooks, std::string* failure);
}  // namespace artdcevm::jit_lifecycle
