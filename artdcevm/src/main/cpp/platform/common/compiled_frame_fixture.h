#pragma once
#include "api/hook_engine.h"
namespace artdcevm::compiled_frame_fixture {
// Exact named CurrentThread binding before Java prepare/start. This initializes
// only a blocking fixture bridge; it grants no JIT, geometry or schema ability.
bool Initialize(HookEngine&)noexcept;
// Snapshot only in ordinary phase after Run.start returns and while its Java
// Run remains strongly owned and armed. Never acquire this gate's mutex inside
// an ART exclusive stop. Target is borrowed only until Run.close/release.
void* OrdinaryBlockedTarget()noexcept;
}
