#pragma once
#include "platform/common/native_final_write_listener.h"
#include "platform/common/native_receiver_frame.h"
namespace artdcevm::receiver_guard {
// A concrete process-lifetime dispatch lease, never hierarchy publication
// permission by itself. The narrowing owner additionally needs exact Plan,
// stopped active-frame recovery and retained typed-storage/array proofs.
class PermanentDispatch final {
 public:
 PermanentDispatch(Guard&guard,const FrameReader&frames):guard_(guard),frames_(frames){}
 bool Prepare(std::string*error);
 // Final stopped window: no JNI, allocation, symbol lookup or C++ mutex.
 bool Validate()const noexcept;
 // Call only after every rejectable same-Plan guard has succeeded, before any
 // narrowed Class/alias publication. Activates the existing consumer and lease.
 void BeginCommit()const noexcept;
 private:
 Guard&guard_;const FrameReader&frames_;final_write_guard::ReceiverProofWitness proof_;
 bool prepared_=false;mutable bool validated_=false;
};
}
