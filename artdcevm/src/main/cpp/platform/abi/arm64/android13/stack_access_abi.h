#pragma once
#include <cstddef>
#include <cstdint>
#include "platform/abi/arm64/android13/validation_metadata_abi.h"
#include "platform/abi/arm64/android13/code_info.h"
#include "platform/abi/arm64/stack_visitor.h"
namespace artdcevm::android13::stack_abi {
// Explicit matched CAS decoded-object declaration. Physical offsets are also
// present in fixed Android 13--16 sources; this does not select a device by SDK.
using BitRegion=abi::arm64::android13::BitRegion;
template<size_t Columns> using BitTable=abi::arm64::android13::BitTable<Columns>;
using CodeInfo=abi::arm64::android13::CodeInfo;
using TableIterator=abi::arm64::android13::TableIterator;
using InlineRange=abi::arm64::android13::InlineRange;
using CodeInfoPair=abi::arm64::android13::CodeInfoPair;
using StackMapPair=abi::arm64::android13::StackMapPair;
static_assert(sizeof(CodeInfo)==344 && offsetof(CodeInfo,stack_maps)==32 && offsetof(CodeInfo,inline_infos)==152);
using WalkKind=abi::arm64::stack::WalkKind;
class Visitor {
 public:
  Visitor(void* thread, void* context, WalkKind kind);
  virtual ~Visitor() = default;
  virtual bool VisitFrame() = 0;
  void Walk(bool transitions = false);
  void* Method() const;
  void* thread;
  WalkKind kind;
  void* shadow_frame;
  void** quick_frame;
  uintptr_t pc;
  const void* header;
  size_t num_frames;
  size_t depth;
  InlineRange inline_frames;
  CodeInfoPair inline_info;
  StackMapPair stack_map;
  void* context;
  bool check_suspended;
};
struct DexPrefix {
  validation_abi::DexMappingPrefix mapping;
  unsigned char location[24];
  uint32_t checksum;
  const void* header;
  const void* string_ids; const void* type_ids; const void* field_ids;
  const void* method_ids; const void* proto_ids; const void* class_defs;
  const void* method_handles; size_t num_method_handles;
  const void* call_sites; size_t num_call_sites;
  const void* hiddenapi;
  const void* oat_dex;
};
struct OatDexPrefix { const void* oat; };
inline constexpr size_t kCodeOffset = sizeof(uint32_t);
}
