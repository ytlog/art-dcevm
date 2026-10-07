#pragma once
#include "platform/android13/stack_access_abi.h"
#include "platform/android13/member_access_guards_abi.h"
#include <type_traits>
namespace artdcevm::android13::listener_abi {
// Android 13 instrumentation.h and libc++ optional/reference_wrapper, release
// arm64 ABI: the optional is trivial and passed in two general registers.
struct OptionalFrame { const void* frame; bool engaged; };
struct Handle { uint32_t* slot; };
class Listener {
 public:
  virtual ~Listener() = default;
  virtual void MethodEntered(void*,void*) = 0;
  virtual void MethodExited(void*,void*,OptionalFrame,Handle&) = 0;
  virtual void MethodExited(void*,void*,OptionalFrame,uint64_t&) = 0;
  virtual void MethodUnwind(void*,Handle,void*,uint32_t) = 0;
  virtual void DexPcMoved(void*,Handle,void*,uint32_t) = 0;
  virtual void FieldRead(void*,Handle,void*,uint32_t,void*) = 0;
  virtual void FieldWritten(void*,Handle,void*,uint32_t,void*,Handle) = 0;
  virtual void FieldWritten(void*,Handle,void*,uint32_t,void*,const uint64_t&) = 0;
  virtual void ExceptionThrown(void*,Handle) = 0;
  virtual void ExceptionHandled(void*,Handle) = 0;
  virtual void Branch(void*,void*,uint32_t,int32_t) = 0;
  virtual void WatchedFramePop(void*,const member_access_abi::ShadowFrame&) = 0;
};
struct ManagedStack { uintptr_t quick; void* link; void* shadow; };
#pragma pack(push,4)
struct DexCache {
  validation_abi::DexCachePrefix prefix;
  uint64_t pre_strings,call_sites,fields,method_types,methods,types,strings;
  uint32_t num_pre_strings,num_call_sites,num_fields,num_method_types,num_methods,num_types,num_strings;
};
#pragma pack(pop)
struct DexFile { stack_abi::DexPrefix prefix; void* container; bool compact; };
struct alignas(8) TypePair { uint32_t object,index; };
struct InstructionView { const void* dex; const uint16_t* instructions; uint32_t count; };
struct MethodId { uint16_t class_index,proto_index; uint32_t name_index; };
inline constexpr size_t kManagedStackOffset=168;
inline constexpr uint32_t kEvents=0x38,kTypeSlots=1024;
inline constexpr uint32_t kPrimitiveTypeMask=0xffff,kNonPrimitiveEncoding=2u<<16;
inline constexpr uint16_t kCompactCountShift=5,kCompactCountPreheader=0x10;
static_assert(sizeof(OptionalFrame)==16 && alignof(OptionalFrame)==8);
static_assert(std::is_trivially_copyable_v<OptionalFrame>);
static_assert(sizeof(Handle)==8 && sizeof(ManagedStack)==24 && alignof(ManagedStack)==8);
static_assert(offsetof(DexCache,types)==64 && offsetof(DexCache,num_types)==100);
static_assert(sizeof(TypePair)==8 && alignof(TypePair)==8 && sizeof(MethodId)==8);
}
