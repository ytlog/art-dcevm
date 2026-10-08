#pragma once
#include <cstdint>
namespace artdcevm::abi::arm64 {
// ART managed object with compressed 32-bit Class reference.
struct ObjectPrefix {
  uint32_t klass;
  uint32_t monitor;
};
}
