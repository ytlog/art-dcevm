#pragma once
#include "api/runtime_backend.h"
namespace artdcevm {
// Each platform owns the entire mutation, including suspension, GC and publication.
// Returning failure must leave live classes unchanged. Callers cannot compose half-transactions.
class ClassRedefiner {
 public:
  virtual ~ClassRedefiner() = default;
  virtual bool Available() const = 0;
  virtual int Apply(const std::vector<Definition>& definitions) = 0;
};
}
