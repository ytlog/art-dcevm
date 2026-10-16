#pragma once
#include <jni.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "evolution/class_schema.h"
#include "platform/abi/arm64/common/redefinition.h"
#include "platform/abi/arm64/common/runtime_objects.h"

// Runtime probes that select one source-declared class layout per transaction
// (class_layout_table.h). The selection lives in the transaction context.
namespace artdcevm::class_layout {

// ART lookups the probes call, resolved by name with the member mapping hooks.
struct Services {
  abi::arm64::ArtField* (*find_static_field)(void*, std::string_view, std::string_view);
  abi::arm64::ArtField* (*find_declared_instance_field)(void*, std::string_view, std::string_view);
  void* (*mirror_class)(abi::arm64::ClassRedefinitionPrefix*);
  jclass class_type;  // java.lang.Class, a global reference.
  void* (*read_reference)(uint32_t);  // Named ART read barrier when the runtime is probed.
};
// Called once the member mapping hooks are prepared; until then no layout verifies.
void Enable(const Services& services);

// Read a heap reference through the configured runtime read barrier.
void* ReadReference(uint32_t reference);

// Source releases in candidate order; verified_only intersects the current
// transaction's VerifyTarget probes. Native callers use false for ordering.
std::vector<int> CandidateReleases(bool verified_only);
// Probe the source-declared class layouts on a redefinition target against
// ART's own lookups, the class's current DEX and the JVMTI view (`live`). Runs
// in a Runnable validation/admission hook, before ART can no longer fail.
// True when a layout matches every target probed in the current transaction.
bool VerifyTarget(abi::arm64::ClassRedefinitionPrefix* self, const evolution::ClassSchema& live);
// Java bits of another loaded class's runtime flags (its ClassDef flags), read
// in CheckClass with every layout VerifyTarget accepted for this transaction.
// False when none is verified or the accepted layouts disagree.
bool ClassAccess(abi::arm64::ClassRedefinitionPrefix* self, jclass klass, uint32_t* access);
// After ART allocated a redefinition's replacement classes: drop the layouts
// whose holder slots do not name the target and a class of the incoming DEX.
// Returns the selected layout's table index, or -1 with `why`.
int VerifyReplacements(abi::arm64::ClassRedefinitionPrefix* self,
                       const abi::arm64::RedefinitionDataIter& data, std::string* why);
// The table index of the current transaction's selected layout, or -1.
int Selected();
// Later redefinitions of this transaction use only `index`; -1 selects none.
void Select(int index);

// Reported as memberMapping: "unavailable", "unverified",
// "verified: <layout>" or "rejected: <reason>".
void Report(std::string state);
std::string State();
}  // namespace artdcevm::class_layout
