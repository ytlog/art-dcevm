#pragma once
#include "platform/common/interpreter_events.h"
namespace artdcevm::continuation_profile {
#if defined(ARTDCEVM_INTERFACE_ABORT_TESTS) && defined(ARTDCEVM_CONTINUATION_PROFILE)
bool Pc(interpreter_events::Pc original, void*, const void*, const void*, uint32_t, const void*, void*);
bool RetiredLookup(const void*, bool (*)(const void*));
void MarkerStart();
void MarkerEnd();
#else
inline bool Pc(interpreter_events::Pc original, void* self, const void* a, const void* f, uint32_t pc, const void* i, void* r) { return original(self,a,f,pc,i,r); }
inline bool RetiredLookup(const void* m, bool (*lookup)(const void*)) { return lookup(m); }
inline void MarkerStart() {}
inline void MarkerEnd() {}
#endif
}
