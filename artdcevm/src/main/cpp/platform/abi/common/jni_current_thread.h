#pragma once
namespace artdcevm::abi::common {
// Fixed thread.h/thread.cc 11--17: static pointer return, no this/structure ABI.
using CurrentThread=void* (*)();
inline constexpr char kCurrentThread[]="_ZN3art6Thread14CurrentFromGdbEv";
}
