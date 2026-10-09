#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
namespace artdcevm::receiver_guard {
class FrameReader;class InstructionOperands;
// Paths are independently witnessed. The bytecode interpreter paths also cover
// former compiled/AOT callers only after the permanent process deopt owner seals.
enum class Path:uint8_t {BytecodeInvoke,BytecodeRead,BytecodeWrite,JniMethod,JniRead,JniWrite,ReflectMethod,ReflectRead,ReflectWrite,HandleMethod,HandleRead,HandleWrite,HandleSpecial,VarHandle,NativeInvoke,Count};
struct Invocation {Path path;void* thread=nullptr;void* member=nullptr;void* receiver=nullptr;void* special_caller=nullptr;void* result=nullptr;bool field=false;};
struct Access {
 void* owner=nullptr;
 bool (*current)(void*,void*)noexcept=nullptr;
 bool (*member)(void*,void*,bool,uint32_t*,void**)noexcept=nullptr;
 bool (*object_class)(void*,void*,void**)noexcept=nullptr;
 // Native barrier/weak-root access only. These callbacks may not suspend, invoke
 // JNI, allocate or retain borrowed managed values across this invocation.
 bool (*normalize)(void*,void*,void*,void*,void**)noexcept=nullptr;
 bool (*assignable)(void*,void*,void*,bool*)noexcept=nullptr;
 bool (*special)(void*,void*,void*,bool*)noexcept=nullptr;
 bool (*reference)(void*,void*,void*,void**)noexcept=nullptr;
 bool (*compressed)(void*,void*,uint32_t,void**)noexcept=nullptr;
 void (*raise)(void*,void*,const char*,const char*)noexcept=nullptr;
 bool (*field_id)(void*,void*,void*,void**)noexcept=nullptr;
 const FrameReader* frames=nullptr;
 bool (*handle)(void*,void*,void*,uint32_t*,void**)noexcept=nullptr;
 // Resolve only real invoke instructions from the source caller/PC. A null
 // member means a proven non-invoke; false means unavailable source identity.
 bool (*bytecode)(void*,void*,void*,uint32_t,void**,uint32_t*)noexcept=nullptr;
};
struct Oracle {Invocation invocation;void* declaring=nullptr;void* receiver_class=nullptr;bool rejection=false;};
class Guard final {
 public:
 explicit Guard(Access);
 // Exact rooted fixture/member/thread/path, plus independent declaring/receiver
 // identities. Both original success and guarded ICCE must be proven for every
 // path. No installation bit or caller-supplied coverage mask grants readiness.
 bool BeginProof(const Oracle&)noexcept;
 bool EndProof(bool original_succeeded,bool incompatible_class_change,bool unchanged_sentinel)noexcept;
 bool Proven()const noexcept;
 bool Active()const noexcept{return active_.load(std::memory_order_acquire);}
 void Activate()noexcept;
 bool Check(const Invocation&)noexcept;
 bool InUse()const noexcept;
 bool CheckBytecode(void*thread,void*caller,uint32_t pc)noexcept;
 bool CheckFrame(Path,void* thread,void* member,const void* frame,const InstructionOperands*,void* result,bool field,void* special_caller=nullptr)noexcept;
 bool CheckHandle(void* thread,void* object,void* member,bool field,const void* frame,const InstructionOperands*,void* result)noexcept;
 bool CheckFieldId(Path,void* thread,void* id,void* handle)noexcept;
 bool CheckReference(Path,void* thread,void* member,void* handle,void* result=nullptr,bool field=false)noexcept;
 bool CheckCompressed(Path,void* thread,void* member,uint32_t reference,void* result=nullptr,bool field=false)noexcept;
 private:
 bool Interested(Path,void*,void*)const noexcept;
 bool Entry(Path,void*,void*,void*,uint32_t,bool,void*,bool)noexcept;
 public:
 private:
 Access access_;mutable std::mutex lock_;Oracle oracle_{};bool probing_=false,seen_=false,rejected_=false;
 uint32_t passed_=0,refused_=0;std::atomic<bool> active_{false};
};
// One optional process-lifetime consumer, published before any aliases or
// narrowed Class. Entry owners share this dispatch; no second hook stack.
inline std::atomic<Guard*> consumer{nullptr};
inline bool Bind(Guard& owner)noexcept {Guard* expected=nullptr;return consumer.compare_exchange_strong(expected,&owner,std::memory_order_acq_rel)||expected==&owner;}
inline bool Check(const Invocation& invocation)noexcept {auto* owner=consumer.load(std::memory_order_acquire);return owner&&owner->Check(invocation);}
inline bool CheckFrame(Path path,void* thread,void* member,const void*frame,const InstructionOperands*operands,void*result,bool field)noexcept {auto* owner=consumer.load(std::memory_order_acquire);return owner&&owner->CheckFrame(path,thread,member,frame,operands,result,field);}
inline bool CheckHandle(void*thread,void*object,void*member,bool field,const void*frame,const InstructionOperands*operands,void*result)noexcept {auto* owner=consumer.load(std::memory_order_acquire);return owner&&owner->CheckHandle(thread,object,member,field,frame,operands,result);}
inline bool InUse()noexcept {auto* owner=consumer.load(std::memory_order_acquire);return owner&&owner->InUse();}
inline bool CheckFieldId(Path path,void* thread,void* id,void* handle)noexcept {auto* owner=consumer.load(std::memory_order_acquire);return owner&&owner->CheckFieldId(path,thread,id,handle);}
inline bool CheckReference(Path path,void* thread,void* member,void* handle,void* result=nullptr,bool field=false)noexcept {auto* owner=consumer.load(std::memory_order_acquire);return owner&&owner->CheckReference(path,thread,member,handle,result,field);}
inline bool CheckCompressed(Path path,void* thread,void* member,uint32_t reference,void* result=nullptr,bool field=false)noexcept {auto* owner=consumer.load(std::memory_order_acquire);return owner&&owner->CheckCompressed(path,thread,member,reference,result,field);}
}
