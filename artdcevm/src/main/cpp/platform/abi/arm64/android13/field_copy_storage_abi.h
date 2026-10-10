#pragma once

#include <cstddef>
#include <cstdint>

namespace artdcevm::android13::field_copy_storage_abi {
// Locked Android 13 release arm64 source declarations, never binary-derived.
// thread.h Thread::CardTableOffset<PointerSize::k64>(); mirror/object.h;
// gc/accounting/card_table.h; dex/modifiers.h; lock_word-inl.h.
inline constexpr size_t kThreadCardTableOffset = 144;
inline constexpr size_t kMonitorOffset = 4;
inline constexpr uint32_t kDefaultLockWord = 0;
inline constexpr uint32_t kVolatileFlag = 0x40;
inline constexpr size_t kCardShift = 10;
inline constexpr uint8_t kCardDirty = 0x70;
inline constexpr char kCurrentThread[] = "_ZN3art6Thread14CurrentFromGdbEv";
using CurrentThread = void* (*)();
}
