#pragma once
#include "api/runtime_backend.h"
#include <memory>
namespace artdcevm {
std::unique_ptr<RuntimeBackend> CreateBackend();
}
