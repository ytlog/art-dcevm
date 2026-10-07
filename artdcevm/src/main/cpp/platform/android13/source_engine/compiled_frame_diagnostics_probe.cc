#include "platform/android13/compiled_frame_diagnostics_abi.h"
#include "platform/android13/member_decode_abi.h"
#include "mirror/executable.h"
#include "oat_quick_method_header.h"
#include "suspend_reason.h"
#include "platform/android13/suspend_deopt_art_abi.h"
#include "jvalue.h"
#include "obj_ptr.h"
#include "thread.h"
#include <cstring>
#include <new>
namespace suspend_abi = artdcevm::android13::suspend_abi;
static_assert(sizeof(suspend_abi::JValue) == sizeof(art::JValue));
static_assert(alignof(suspend_abi::JValue) == alignof(art::JValue));
static_assert(sizeof(suspend_abi::ThrowablePtr) == sizeof(art::ObjPtr<art::mirror::Throwable>));
static_assert(alignof(suspend_abi::ThrowablePtr) == alignof(art::ObjPtr<art::mirror::Throwable>));
static_assert(static_cast<int>(suspend_abi::MethodType::kDefault) ==
              static_cast<int>(art::DeoptimizationMethodType::kDefault));
static_assert(sizeof(suspend_abi::MethodType) == sizeof(art::DeoptimizationMethodType));
namespace fixed=artdcevm::android13::compiled_frame_abi;
static_assert(sizeof(fixed::SuspendReason)==sizeof(art::SuspendReason));
static_assert(static_cast<char>(fixed::SuspendReason::kInternal)==static_cast<char>(art::SuspendReason::kInternal));

extern "C" bool ArtDcevmSourceProbeCompiledFrameDiagnostics(bool(*contains)(const void*,uintptr_t)){
 if(!contains||art::kRuntimePointerSize!=art::PointerSize::k64||
    offsetof(artdcevm::android13::member_decode_abi::Executable,art_method)!=art::mirror::Executable::ArtMethodOffset().Int32Value())return false;
 alignas(4) unsigned char bytes[64]{};auto* header=new(bytes+16)art::OatQuickMethodHeader(0);
 uint32_t size=20;std::memcpy(header,&size,4);uintptr_t code=reinterpret_cast<uintptr_t>(header->GetCode());
 for(uintptr_t pc:{code-1,code,code+20,code+21})if(contains(header,pc)!=header->Contains(pc))return false;
 return true;
}
