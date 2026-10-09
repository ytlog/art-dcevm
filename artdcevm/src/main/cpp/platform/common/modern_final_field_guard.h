#pragma once
#include <cstdint>
namespace artdcevm::final_field_guard {
// These are ART permission paths. JNI and accessible reflection are intentional
// escape hatches in ART, and must not acquire Java bytecode permissions.
enum class Path { Bytecode, MethodHandleSetter, VarHandle, Jni, Reflection };
enum class Decision { Delegate, IllegalAccessError, IllegalAccessException,
                      UnsupportedOperationException, Unavailable };
struct Access {
  Path path=Path::Bytecode;
  uint32_t field_flags=0;
  bool same_declaring_owner=false; // Includes a source-proven obsolete owner.
  bool reflection_override=false;
  bool var_mode_valid=false;
  uint32_t var_mode_template=0; // Named VarHandle::GetAccessModeTemplate.
};
Decision Check(const Access&) noexcept;
// The outer caller already owns Runnable/shared-mutator access and has read
// its target through the calibrated original native visitor. Never obtain JNI
// access or resolve a target inside this dispatch.
struct Boundary {
  void* owner=nullptr;
  bool (*read)(void* owner,void* field,uint32_t* flags)=nullptr;
  void (*raise)(void* owner,void* self,const char* type,const char* text)=nullptr;
};
// Returns true only after raising the appropriate exception. Caller skips its
// original write in this case. The original result is preserved on delegation.
bool Reject(const Boundary&,void* field,void* self,const Access&,void* result=nullptr) noexcept;
} // namespace artdcevm::final_field_guard
