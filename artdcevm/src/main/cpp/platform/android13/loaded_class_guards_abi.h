#pragma once
#include <cstdint>
namespace artdcevm::android13::loaded_class_abi {
// runtime/class_linker.h ClassVisitor, Itanium ABI. Release ObjPtr<Class>
// is a trivial uintptr_t; no STL types cross this virtual call boundary.
class ClassVisitor {
 public:
  virtual ~ClassVisitor() {}
  virtual bool operator()(uintptr_t klass) = 0;
};
using VisitClasses = void (*)(void*, ClassVisitor*);
using SamePackage = bool (*)(void*, uintptr_t);
using ReadRoot = void* (*)(void*);
}
