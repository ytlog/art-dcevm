#pragma once
// Fixed AOSP runtime/thread_pool.h interface. Keep the actual native type name
// and Thread argument: callers borrow an ART-owned object, never construct one.
namespace art {
class Thread;
class Closure {
 public:
  virtual ~Closure() = default;
  virtual void Run(Thread* self) = 0;
};
}
