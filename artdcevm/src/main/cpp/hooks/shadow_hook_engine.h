#pragma once
#include "api/hook_engine.h"
namespace artdcevm {
class ShadowHookEngine final : public HookEngine {
 public:
  bool Initialize(std::string* error) override;
  void* FindSymbol(const char* library, const char* symbol) override;
  void* Install(void* target, void* replacement, void** original, std::string* error) override;
  bool Remove(void* token, std::string* error) override;
};
}
