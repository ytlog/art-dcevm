#pragma once
#include <string>
namespace artdcevm {
// Hook mechanism knows instruction relocation; the platform owns symbol signatures and policy.
class HookEngine {
 public:
  virtual ~HookEngine() = default;
  virtual bool Initialize(std::string* error) = 0;
  virtual void* FindSymbol(const char* library, const char* symbol) = 0;
  virtual void* Install(void* target, void* replacement, void** original, std::string* error) = 0;
  virtual bool Remove(void* token, std::string* error) = 0;
};
}
