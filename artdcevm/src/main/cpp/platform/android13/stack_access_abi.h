#pragma once
#include <cstddef>
#include <cstdint>
#include "platform/android13/validation_metadata_abi.h"
namespace artdcevm::android13::stack_abi {
// Fixed release declarations from stack.h, stack_map.h and base/bit_table.h.
// These are borrowed POD storage; device WalkStack owns all decoding.
struct BitRegion { void* data; size_t bit_start; size_t bit_size; };
template<size_t Columns> struct BitTable {
  BitRegion region; uint32_t rows; uint16_t columns[Columns+1];
};
struct CodeInfo {
  uint32_t flags, code_size, frame_size, core_mask, fp_mask, dex_regs, table_flags;
  BitTable<8> stack_maps;
  BitTable<2> register_masks;
  BitTable<1> stack_masks;
  BitTable<6> inline_infos;
  BitTable<3> method_infos;
  BitTable<1> dex_masks;
  BitTable<1> dex_maps;
  BitTable<2> dex_catalog;
};
struct TableIterator { void* table; uint32_t row; };
struct InlineRange { TableIterator begin, end; };
struct CodeInfoPair { void* header; CodeInfo info; };
struct StackMapPair { uintptr_t pc; TableIterator map; };
enum class WalkKind : uint32_t { kIncludeInlined, kSkipInlined };
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
