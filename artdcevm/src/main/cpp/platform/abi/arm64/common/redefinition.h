#pragma once
#include <cstddef>
#include <cstdint>
#include <jni.h>

// Fixed release arm64 declarations shared by Android 11--17, checked against
// openjdkjvmti/ti_redefine.h and libdexfile/dex/dex_file.h of android-11.0.0_r1,
// android-13.0.0_r1, android-15.0.0_r1, android16-release and main. Only the
// leading members are declared; nothing here constructs or destroys ART objects.
namespace artdcevm::abi::arm64 {

// art::DexFile: vptr, begin_, size_ (unused_size_ since 15, kept for layout),
// then data_ (begin/size). The file size is read from the DEX header instead.
struct DexFilePrefix {
  const void* vtable;
  const uint8_t* begin;
  size_t size_or_unused;
  const uint8_t* data_begin;
  size_t data_size;
};
static_assert(offsetof(DexFilePrefix, begin) == 8 && offsetof(DexFilePrefix, data_begin) == 24 &&
              offsetof(DexFilePrefix, data_size) == 32);

// openjdkjvmti::Redefiner, a non-polymorphic class: env_, result_, ...
// RecordFailure only formats *error_msg_ and stores result_.
struct RedefinerPrefix {
  void* env;
  int32_t result;
};
static_assert(offsetof(RedefinerPrefix, result) == 8);

// Redefiner::ClassRedefinition: driver_, klass_, unique_ptr<const DexFile> dex_file_, ...
struct ClassRedefinitionPrefix {
  RedefinerPrefix* driver;
  jclass klass;
  const DexFilePrefix* dex_file;
};
static_assert(offsetof(ClassRedefinitionPrefix, klass) == 8 &&
              offsetof(ClassRedefinitionPrefix, dex_file) == 16);

// DexFile::Header::file_size_.
inline constexpr size_t kDexHeaderFileSizeOffset = 0x20;

}  // namespace artdcevm::abi::arm64
