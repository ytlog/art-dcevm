#pragma once
#include "platform/common/native_receiver_guard.h"
namespace artdcevm::receiver_guard::episode {
struct Snapshot {uint64_t body_calls=0;int64_t field_value=0;uint64_t sentinel=0;};
struct Prepared {Oracle oracle;Snapshot before;int64_t write_value=0;int64_t expected_result=0;};
struct Completion {bool succeeded=false;bool incompatible_class_change=false;int64_t value=0;};
// Adapter owns strong Java roots and cached ORIGINAL native member identities.
// Prepare must obtain the oracle from fresh roots, including the current Class
// of the SAME original receiver. A rejection requires a real source alias /
// hierarchy migration episode; fabricating an assignability leaf is forbidden.
// Run invokes the original Java/JNI/native entry, never Guard::Check directly.
// expected_result is independently prepared from the public method/read oracle;
// void writes are proved by field readback, not an invented return value.
class Source {
 public:
 virtual ~Source()=default;
 virtual bool Prepare(Path,bool rejection,Prepared*) noexcept=0;
 virtual bool Run(Path,Completion*) noexcept=0;
 virtual bool Observe(Snapshot*) noexcept=0;
};
enum class Failure {None,Prepare,Identity,Begin,Original,Readback,Oracle,End,Incomplete};
struct Result {Failure failure=Failure::None;Path path=Path::BytecodeInvoke;bool rejection=false;unsigned completed=0;};
// One ordinary synchronous driver, no hook ownership or activation. Even a
// completed driver cannot replace the permanent bytecode/frame/source proofs.
Result Calibrate(Guard&,Source&) noexcept;
}
