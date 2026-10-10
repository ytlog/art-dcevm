#pragma once
#include "platform/abi/arm64/android13/heap_reference_abi.h"
#include <cstddef>
#include <cstdint>

// libdexfile/dex/dex_instruction.h. Use ART's actual virtual GetOperand; the
// fixed declaration preserves Range/VarArgs/NoReceiver operand semantics.
namespace art {
class InstructionOperands {
 public:
  explicit InstructionOperands(size_t num_operands) : num_operands_(num_operands) {}
  virtual ~InstructionOperands() {}
  virtual uint32_t GetOperand(size_t index) const = 0;
  size_t GetNumberOfOperands() const { return num_operands_; }
 private:
  const size_t num_operands_;
};
}
namespace artdcevm::android13::member_access_abi {
struct RunnableAccess { void* self; void* env; void* vm; };
#pragma pack(push, 4)
struct MethodHandle {
  heap_reference_abi::ObjectPrefix object;
  uint32_t as_type_cache, cached_spread_invoker, method_type, kind;
  uint64_t target;
};
struct MethodType {
  heap_reference_abi::ObjectPrefix object;
  uint32_t form, descriptor, parameters, return_type, wrap_alt;
};
struct FieldVarHandle {
  heap_reference_abi::ObjectPrefix object;
  uint32_t coordinate0, coordinate1, variable_type;
  int32_t access_modes;
  uint64_t field;
};
#pragma pack(pop)
struct ShadowFrame {
  void* link; void* method; void* result; void* dex_pc; void* instructions;
  void* monitor_vector;
  uint32_t registers, pc;
  int16_t cached_hotness, hotness;
  uint32_t flags;
};
enum HandleKind : uint32_t { kVirtual=0, kSuper, kDirect, kStatic, kInterface,
  kTransform, kVarHandle, kVarHandleExact, kInstanceGet, kInstancePut, kStaticGet, kStaticPut };
inline constexpr uint32_t kStaticFlag=0x8, kFinalFlag=0x10, kInterfaceFlag=0x200;
inline constexpr uint32_t kProxyClass=0x40000, kObsoleteMethod=0x40000, kObsoleteClass=0x200000;
inline constexpr int kLastAccessMode=30;
inline constexpr size_t kArrayLength=8, kArrayData=12;
static_assert(sizeof(RunnableAccess)==24 && offsetof(RunnableAccess,env)==8 && offsetof(RunnableAccess,vm)==16);
static_assert(sizeof(art::InstructionOperands)==16 && alignof(art::InstructionOperands)==8);
static_assert(offsetof(MethodHandle,method_type)==16 && offsetof(MethodHandle,kind)==20);
static_assert(offsetof(FieldVarHandle,coordinate0)==8 && offsetof(FieldVarHandle,coordinate1)==12 &&
              offsetof(FieldVarHandle,variable_type)==16 && offsetof(FieldVarHandle,access_modes)==20);
static_assert(offsetof(ShadowFrame,link)==0 && offsetof(ShadowFrame,method)==8 && offsetof(ShadowFrame,result)==16 &&
              offsetof(ShadowFrame,dex_pc)==24 && offsetof(ShadowFrame,instructions)==32 && offsetof(ShadowFrame,monitor_vector)==40);
static_assert(offsetof(ShadowFrame,pc)==52 && offsetof(ShadowFrame,cached_hotness)==56 &&
              offsetof(ShadowFrame,hotness)==58 && offsetof(ShadowFrame,flags)==60);
static_assert(sizeof(MethodHandle)==32 && offsetof(MethodHandle,target)==24);
static_assert(sizeof(MethodType)==28 && offsetof(MethodType,parameters)==16);
static_assert(sizeof(FieldVarHandle)==32 && offsetof(FieldVarHandle,field)==24);
static_assert(sizeof(ShadowFrame)==64 && offsetof(ShadowFrame,registers)==48);
}
