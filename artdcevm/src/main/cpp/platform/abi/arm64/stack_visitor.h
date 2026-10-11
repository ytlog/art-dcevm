#pragma once
#include <cstring>
#include "platform/abi/arm64/android11/stack_storage.h"
#include "platform/abi/arm64/android12/stack_storage.h"
#include "platform/abi/arm64/android13/stack_storage.h"
#include "platform/abi/arm64/android17/stack_storage.h"

namespace artdcevm::abi::arm64::stack {
enum class WalkKind : uint32_t { kIncludeInlined, kSkipInlined };
// Fixed runtime/stack.h member order and virtual prefix, shared by 11–17.
// The embedded decoded CodeInfo pair selects the actual storage family.
// This is a live C++ callback object, not an untyped byte allocation.
template<class InfoPair> class Visitor {
 public:
  virtual ~Visitor()=default;
  virtual bool VisitFrame()=0;
  void* thread;
  WalkKind kind;
  void* shadow_frame;
  void** quick_frame;
  uintptr_t pc;
  const void* header;
  size_t num_frames;
  size_t depth;
  android11::InlineRange inline_frames;
  InfoPair inline_info;
  android11::StackMapPair stack_map;
  void* context;
  bool check_suspended;
};
using Visitor11=Visitor<android11::CodeInfoPair>;
using Visitor12=Visitor<android12::CodeInfoPair>;
using Visitor13=Visitor<android13::CodeInfoPair>;
using Visitor17=Visitor<android17::CodeInfoPair>;
static_assert(sizeof(Visitor11)==520 && sizeof(Visitor12)==528);
static_assert(sizeof(Visitor13)==496 && sizeof(Visitor17)==520);
static_assert(alignof(Visitor11)==8 && alignof(Visitor17)==8);
// ART uses Clang's Itanium C++ ABI. Its single polymorphic base has the vptr
// first; these supported compiler offsets lock every borrowed native field.
// offsetof on this polymorphic source declaration is a supported Clang
// extension. Suppress only that diagnostic while retaining every ABI assertion.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
#define ARTDCEVM_CHECK_VISITOR(type,pair) \
  static_assert(offsetof(type,thread)==8 && offsetof(type,kind)==16); \
  static_assert(offsetof(type,shadow_frame)==24 && offsetof(type,quick_frame)==32); \
  static_assert(offsetof(type,pc)==40 && offsetof(type,header)==48); \
  static_assert(offsetof(type,num_frames)==56 && offsetof(type,depth)==64); \
  static_assert(offsetof(type,inline_frames)==72 && offsetof(type,inline_info)==104); \
  static_assert(offsetof(type,stack_map)==104+sizeof(pair)); \
  static_assert(offsetof(type,context)==128+sizeof(pair)); \
  static_assert(offsetof(type,check_suspended)==136+sizeof(pair));
ARTDCEVM_CHECK_VISITOR(Visitor11,android11::CodeInfoPair)
ARTDCEVM_CHECK_VISITOR(Visitor12,android12::CodeInfoPair)
ARTDCEVM_CHECK_VISITOR(Visitor13,android13::CodeInfoPair)
ARTDCEVM_CHECK_VISITOR(Visitor17,android17::CodeInfoPair)
#undef ARTDCEVM_CHECK_VISITOR
inline constexpr size_t kThreadOffset=offsetof(Visitor13,thread);
inline constexpr size_t kShadowOffset=offsetof(Visitor13,shadow_frame);
inline constexpr size_t kQuickOffset=offsetof(Visitor13,quick_frame);
inline constexpr size_t kQuickPcOffset=offsetof(Visitor13,pc);
// Diagnostic only: compare the constructor's independently supplied Context at
// each complete fixed source declaration. This never selects a new geometry or
// permits walking a failed candidate; it identifies which declaration needs
// source investigation without scanning or inferring an offset.
inline uint32_t ConstructorContextEchoMask(const void* storage,size_t extent,const void* expected) {
  if(!storage || !expected)return 0;
  const size_t offsets[]={offsetof(Visitor11,context),offsetof(Visitor12,context),
                          offsetof(Visitor13,context),offsetof(Visitor17,context)};
  uint32_t mask=0;
  for(size_t i=0;i<4;++i) {
    if(offsets[i]>extent || extent-offsets[i]<sizeof(void*))continue;
    const void* value=nullptr;
    std::memcpy(&value,static_cast<const unsigned char*>(storage)+offsets[i],sizeof(value));
    if(value==expected)mask|=1u<<i;
  }
  return mask;
}
#pragma clang diagnostic pop

// The opaque pointer arguments are the source-declared this/Thread/Context
// pointers. They neither decode objects nor remove the required ART locks.
using Construct=void (*)(void*,void*,void*,WalkKind,bool);
using Walk=void (*)(void*,bool);
using Method=void* (*)(const void*);
using DexPc=uint32_t (*)(const void*,bool);
inline constexpr char kConstruct[]=
    "_ZN3art12StackVisitorC2EPNS_6ThreadEPNS_7ContextENS0_13StackWalkKindEb";
inline constexpr char kWalk[]=
    "_ZN3art12StackVisitor9WalkStackILNS0_16CountTransitionsE0EEEvb";
// Android 17 adds a template parameter, not an extra runtime argument.
// This is CountTransitions::kYes, kTolerateNullMethods=false.
inline constexpr char kWalk17[]=
    "_ZN3art12StackVisitor9WalkStackILNS0_16CountTransitionsE0ELb0EEEvb";
// Fixed runtime/stack.h: GetDexPc(bool abort_on_failure) const.
inline constexpr char kDexPc[]="_ZNK3art12StackVisitor8GetDexPcEb";
inline constexpr char kMethod[]="_ZNK3art12StackVisitor9GetMethodEv";
}
