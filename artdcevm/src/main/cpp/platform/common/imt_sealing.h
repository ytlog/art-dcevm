#pragma once
#include <string>
#include "platform/common/class_layout.h"
#include "platform/common/runtime_thread.h"
#include "platform/abi/arm64/common/redefinition.h"

// Interface dispatch for classes that stop implementing an interface
// (docs/STRUCTURAL_REDEFINE_ENGINE.md, "移除接口"). The verifier never checks
// an invoke-interface receiver, and compiled code dispatches through a direct
// IMT entry without checking it either. A sealed class gets its own IMT in
// which every slot is a fresh conflict method: ART's conflict trampoline then
// looks every call up in the receiver's IfTable, fills the slot's table on a
// hit, and throws IncompatibleClassChangeError for an interface the class no
// longer implements. A later subclass that copies this IMT sees a runtime
// method in every slot, so ART never places a direct entry there either.
namespace artdcevm::imt_sealing {
// Resolves the named runtime services; failure leaves interface removal off.
bool Prepare(const class_layout::Services& services, std::string* failure);
bool Available();
// "unavailable: <reason>" or "available".
std::string State();
// A sealed IMT for one class, not yet installed (null for an abstract class),
// and its installation. Publish is a single release store of the class's IMT
// pointer, so a live class may switch to it while other threads dispatch.
bool Build(const runtime_thread::VerifiedThread& thread, abi::arm64::ClassRedefinitionPrefix* self,
           void* klass, int selected, void*** table, std::string* why);
void Publish(void* klass, int selected, void** table);
// Before ART's no-failure point: seals every concrete replacement class of this
// redefinition (the target and its loaded subclasses) under the selected layout.
bool Seal(const runtime_thread::VerifiedThread& thread, abi::arm64::ClassRedefinitionPrefix* self,
          const abi::arm64::RedefinitionDataIter& data, int selected, std::string* why);
}  // namespace artdcevm::imt_sealing
