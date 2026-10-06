#pragma once
#include <mutex>
namespace artdcevm {
inline std::mutex& TransactionMutex() {
  static std::mutex mutex;
  return mutex;
}
inline thread_local bool internal_redefinition = false;
class InternalRedefinitionScope {
 public:
  InternalRedefinitionScope() : previous_(internal_redefinition) { internal_redefinition = true; }
  ~InternalRedefinitionScope() { internal_redefinition = previous_; }
 private:
  bool previous_;
};
}
