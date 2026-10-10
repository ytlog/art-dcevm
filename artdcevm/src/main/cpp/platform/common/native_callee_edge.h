#pragma once
#include <cstddef>
#include <cstdint>
namespace artdcevm::native_callee_edge {
// Facts from consecutive callbacks of ONE original include-transitions walk.
// This is adjacency evidence only: it grants no stop, receiver or dispatch
// permission. The production owner must reread cursor/Method/this under its
// actual stopped read context before using any borrowed pointer.
enum class Kind { ManagedPhysical, NativePhysical, Inline, Runtime, Transition, Unknown };
struct Frame {
 void* thread=nullptr;void* method=nullptr;void* receiver=nullptr;
 void** quick=nullptr;uintptr_t pc=0;Kind kind=Kind::Unknown;
};
class Adjacent final {
 public:
  // Call for EVERY original callback, including null methods and runtime
  // slowpaths. Skipping one would turn an arbitrary deeper method into a callee.
  void Observe(const Frame& f)noexcept {
   current_=f;
   valid_=f.thread&&f.method&&f.quick&&f.pc&&
    (f.kind==Kind::ManagedPhysical||f.kind==Kind::NativePhysical);
  }
  bool Younger(const Frame&caller,Frame*out)const noexcept {
   if(!out||!valid_||caller.kind!=Kind::ManagedPhysical||
      caller.thread!=current_.thread||!caller.method||!caller.quick||!caller.pc||
      caller.quick==current_.quick||uintptr_t(caller.quick)<=uintptr_t(current_.quick))return false;
   *out=current_;return true;
  }
  void Clear()noexcept {current_={};valid_=false;}
 private:Frame current_{};bool valid_=false;
};
}
