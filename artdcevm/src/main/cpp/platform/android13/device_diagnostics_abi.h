#pragma once
#include <cstddef>
#include <cstdint>
namespace artdcevm::android13::diagnostics_abi {
struct InstrumentationPrefix {uint64_t force_deopt_id;bool stubs_installed;uint32_t level;};
inline constexpr uint32_t kCodeInfoMask=0x40000000,kOffsetMask=0x3fffffff;
// Locked platform std::__1::__function::__base<void(const void*,ArtMethod*)>.
class CallbackBase {public:virtual ~CallbackBase()=default;virtual CallbackBase* Clone()const=0;virtual void Clone(CallbackBase*)const=0;virtual void Destroy()noexcept=0;virtual void DestroyDeallocate()noexcept=0;virtual void Invoke(const void*&&,void*&&)=0;};
struct alignas(16) BorrowedFunction {unsigned char buffer[32]{};CallbackBase* function;};
static_assert(offsetof(InstrumentationPrefix,level)==12);
static_assert(sizeof(BorrowedFunction)==48&&offsetof(BorrowedFunction,function)==32);
}
