#pragma once
#include <cstdint>
namespace artdcevm::var_handle_access_mode {
// Fixed runtime/mirror/var_handle.{h,cc}: Android11 keeps this switch in
// an anonymous namespace; Android12 moves the identical operation to VarHandle.
// The public AccessMode values and template values are unchanged.
// https://android.googlesource.com/platform/art/+/refs/tags/android-11.0.0_r1/runtime/mirror/var_handle.cc
// https://android.googlesource.com/platform/art/+/refs/tags/android-12.0.0_r1/runtime/mirror/var_handle.cc
using NativeTemplate=uint32_t(*)(int);
inline constexpr char kNamedTemplate[]=
    "_ZN3art6mirror9VarHandle21GetAccessModeTemplateENS1_10AccessModeE";
inline constexpr uint32_t kInvalid=UINT32_MAX;
inline constexpr uint32_t Template(int mode) noexcept {
  // kGet/kGetVolatile/kGetAcquire/kGetOpaque are the only read-only modes.
  if(mode<0||mode>30)return kInvalid;
  if(mode<8)return static_cast<uint32_t>(mode&1);
  if(mode==8||(mode>=12&&mode<=15))return 2; // compare-and-set
  if(mode<=11)return 3; // compare-and-exchange
  return 4; // get-and-update, including arithmetic/bitwise writes
}
// If ART exports its classification, every supported mode must agree before
// publication. Missing named body uses the same fixed source algorithm, never
// a null callback which silently disables the current final-field permission.
inline bool Bind(NativeTemplate native,NativeTemplate* output) noexcept {
  if(!output)return false;
  if(native)for(int mode=0;mode<=30;++mode)if(native(mode)!=Template(mode))return false;
  *output=native?native:Template;return true;
}
}
