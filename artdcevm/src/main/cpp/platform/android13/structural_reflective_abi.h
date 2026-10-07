#pragma once
#include <cstddef>
#include <cstdint>
namespace artdcevm::android13::structural_reflective_abi {
// Locked runtime/reflective_value_visitor.h, Itanium virtual declaration.
struct SourceInfo { const void* vptr; uint32_t type; };
struct HeapSourceInfo { SourceInfo base; void* object; };
class Visitor { public: virtual ~Visitor()=default;
 virtual void* VisitMethod(void*,const SourceInfo&)=0;
 virtual void* VisitField(void*,const SourceInfo&)=0;
};
struct SourceView {uint32_t type;void* object;};
inline constexpr uint32_t kExecutable=1,kJniMethod=7,kDexMethod=8,kDexField=9;
inline constexpr size_t kExecutableFlags=32;
static_assert(offsetof(SourceInfo,type)==8 && offsetof(HeapSourceInfo,object)==16);
}
