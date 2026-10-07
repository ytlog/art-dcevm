#define ARTDCEVM_SOURCE_CALIBRATION
#include "platform/android13/member_access_guards_abi.h"
#include "platform/android13/art_method_abi.h"
#include "platform/android13/field_copy_metadata_abi.h"
#include "art_field-inl.h"
#include "art_method-inl.h"
#include "gc_root-inl.h"
#include "jvalue.h"
#include "mirror/method_handle_impl-inl.h"
#include "mirror/method_type-inl.h"
#include "mirror/var_handle.h"
#include "mirror/array-inl.h"
#include "mirror/object_array-inl.h"
#include "interpreter/shadow_frame-inl.h"
#include "scoped_thread_state_change-inl.h"
#include "dex/dex_instruction-inl.h"
#include "dex/dex_instruction_utils.h"
#include <cstddef>
#include <cstring>

namespace art {
namespace fixed=artdcevm::android13::member_access_abi;
struct MethodHandleOffsets {
  static void VerifyGuards() {
    static_assert(sizeof(mirror::MethodHandle)==sizeof(fixed::MethodHandle));
    static_assert(offsetof(mirror::MethodHandle,method_type_)==offsetof(fixed::MethodHandle,method_type));
    static_assert(offsetof(mirror::MethodHandle,handle_kind_)==offsetof(fixed::MethodHandle,kind));
    static_assert(offsetof(mirror::MethodHandle,art_field_or_method_)==offsetof(fixed::MethodHandle,target));
  }
};
struct MethodTypeOffsets {
  static void VerifyGuards() {
    static_assert(sizeof(mirror::MethodType)==sizeof(fixed::MethodType));
    static_assert(offsetof(mirror::MethodType,p_types_)==offsetof(fixed::MethodType,parameters));
  }
};
struct FieldVarHandleOffsets {
  static void VerifyGuards() {
    static_assert(sizeof(mirror::FieldVarHandle)==sizeof(fixed::FieldVarHandle));
    static_assert(offsetof(mirror::FieldVarHandle,art_field_)==offsetof(fixed::FieldVarHandle,field));
  }
};
}
extern "C" int ArtDcevmSourceProbeMemberAccess(JNIEnv* env,jobject sample,
    void* (*read_root)(void*),uint32_t (*operand)(const void*)) {
  namespace fixed=artdcevm::android13::member_access_abi;
  art::MethodHandleOffsets::VerifyGuards(); art::MethodTypeOffsets::VerifyGuards();
  art::FieldVarHandleOffsets::VerifyGuards();
  static_assert(art::ArtField::DeclaringClassOffset().Int32Value()==0);
  static_assert(sizeof(art::ArtField)==sizeof(artdcevm::android13::field_copy_abi::ArtField));
  static_assert(sizeof(art::StackReference<art::mirror::Object>)==4);
  static_assert(sizeof(art::JValue)==sizeof(uint64_t));
  static_assert(art::ShadowFrame::NumberOfVRegsOffset()==offsetof(fixed::ShadowFrame,registers));
  static_assert(art::ShadowFrame::VRegsOffset()==sizeof(fixed::ShadowFrame));
  static_assert(art::mirror::Array::LengthOffset().Int32Value()==fixed::kArrayLength);
  static_assert(art::mirror::Array::DataOffset(4).Int32Value()==fixed::kArrayData);
  static_assert(sizeof(art::ScopedObjectAccessAlreadyRunnable)==sizeof(fixed::RunnableAccess));
  static_assert(sizeof(art::InstructionOperands)==sizeof(void*)+sizeof(size_t));
  static_assert(art::kAccObsoleteObject==fixed::kObsoleteClass);
  static_assert(art::kAccObsoleteMethod==fixed::kObsoleteMethod);
  static_assert(art::kAccClassIsProxy==fixed::kProxyClass);
  static_assert(static_cast<uint32_t>(art::mirror::MethodHandle::kInvokeSuper)==fixed::kSuper);
  static_assert(static_cast<uint32_t>(art::mirror::MethodHandle::kInvokeDirect)==fixed::kDirect);
  static_assert(static_cast<uint32_t>(art::mirror::MethodHandle::kInvokeStatic)==fixed::kStatic);
  static_assert(static_cast<uint32_t>(art::mirror::MethodHandle::kInvokeInterface)==fixed::kInterface);
  static_assert(static_cast<uint32_t>(art::mirror::MethodHandle::kInstanceGet)==fixed::kInstanceGet);
  static_assert(static_cast<uint32_t>(art::mirror::MethodHandle::kInstancePut)==fixed::kInstancePut);
  static_assert(static_cast<uint32_t>(art::mirror::MethodHandle::kStaticGet)==fixed::kStaticGet);
  static_assert(static_cast<uint32_t>(art::mirror::MethodHandle::kStaticPut)==fixed::kStaticPut);
  static_assert(static_cast<int>(art::mirror::VarHandle::AccessMode::kLast)==fixed::kLastAccessMode);
  static_assert(static_cast<int>(art::mirror::VarHandle::AccessModeTemplate::kGet)==0);
  static_assert(art::Instruction::IPUT==0x59 && art::Instruction::IPUT_SHORT==0x5f);
  static_assert(art::Instruction::SPUT==0x67 && art::Instruction::SPUT_SHORT==0x6d);
  if (!read_root || !operand || art::kIsDebugBuild || !art::kUseBakerReadBarrier || art::kPoisonHeapReferences) return 1;
  art::ScopedObjectAccess soa(env);
  fixed::RunnableAccess access;
  std::memcpy(&access,&soa,sizeof(access));
  if (access.self!=soa.Self() || access.env!=soa.Env() || access.vm!=soa.Vm()) return 2;
  art::GcRoot<art::mirror::Object> root(soa.Decode<art::mirror::Object>(sample));
  if (read_root(&root)!=root.Read()) return 3;
  art::RangeInstructionOperands range(3,2);
  uint32_t regs[art::Instruction::kMaxVarArgRegs]={7,2,5,1,0};
  art::VarArgsInstructionOperands varargs(regs,5);
  art::NoReceiverInstructionOperands no_receiver(&range);
  if (operand(&range)!=3 || operand(&varargs)!=7 || operand(&no_receiver)!=4) return 4;
  return 0;
}
