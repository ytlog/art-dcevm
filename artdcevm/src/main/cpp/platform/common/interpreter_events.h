#pragma once
#include <cstdint>
namespace artdcevm::interpreter_events {
using Pc = bool (*)(void*, const void*, const void*, uint32_t, const void*, void*);
using Move = bool (*)(void*, void*, bool, bool);
// The only owner of named interpreter-event hooks, including ICF aliases.
// Callers prepare while Native, serialized by the engine transaction lock.
bool Prepare();
bool Modern();
bool ExceptionService();
const char* ExceptionAbi();
bool MoveException(void* self, void* frame, bool skip, bool skip_throw);
// The legacy caller must supply its original PC-event instrumentation. This
// overload never invents a pointer or maps a partial skip policy to nullptr.
bool ReplayException(void* self, void* frame, const void* instrumentation);
}
