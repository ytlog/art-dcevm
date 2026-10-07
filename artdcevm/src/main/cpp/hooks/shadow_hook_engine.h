#pragma once
#include "api/hook_engine.h"
#include <mutex>
#include <unordered_map>
namespace artdcevm {
class ShadowHookEngine final : public HookEngine {
 public:
  bool Initialize(std::string* error) override;
  void* FindSymbol(const char* library, const char* symbol) override;
  void* Install(void* target, void* replacement, void** original, std::string* error) override;
  bool Remove(void* token, std::string* error) override;
 private:
  std::mutex symbol_names_mutex_;
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>> symbol_names_;
};
}
