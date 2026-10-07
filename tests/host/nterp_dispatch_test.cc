#include "platform/android13/nterp_dispatch.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>

namespace dispatch = artdcevm::android13;
namespace {
int linker_token, thread_token, caller_token, target_token;
size_t calls = 0;
uint32_t expected_index;
dispatch::DispatchInvokeType expected_type;
bool resolver_accepts = true;
void* Resolver(void* linker, void* thread, uint32_t index, void* caller,
               dispatch::DispatchInvokeType type) {
  assert(linker == &linker_token && thread == &thread_token && caller == &caller_token);
  assert(index == expected_index && type == expected_type);
  ++calls;
  return resolver_accepts ? &target_token : nullptr;
}
}  // namespace

int main() {
  assert(!dispatch::ConfigureNterpDispatch(nullptr, Resolver));
  assert(!dispatch::ConfigureNterpDispatch(&linker_token, nullptr));
  assert(dispatch::ConfigureNterpDispatch(&linker_token, Resolver));
  uint16_t invoke[] = {0x6e, 0xffff, 0};
  assert(!dispatch::NeedsDispatchChecks());
  assert(!dispatch::GuardNterpInvoke(&thread_token, &caller_token, invoke));
  assert(calls == 0);
  dispatch::RequireDispatchChecks();
  assert(dispatch::NeedsDispatchChecks());

  constexpr dispatch::DispatchInvokeType types[] = {
      dispatch::DispatchInvokeType::kVirtual, dispatch::DispatchInvokeType::kSuper,
      dispatch::DispatchInvokeType::kDirect, dispatch::DispatchInvokeType::kStatic,
      dispatch::DispatchInvokeType::kInterface};
  // Exhaust the opcode byte and register-count byte. Non-invoke instructions
  // are passed as a single unit, so ASan also checks we do not read method_idx.
  for (unsigned opcode = 0; opcode < 256; ++opcode) {
    const bool supported = (opcode >= 0x6e && opcode <= 0x72) ||
                           (opcode >= 0x74 && opcode <= 0x78);
    for (unsigned register_byte = 0; register_byte < 256; ++register_byte) {
      const uint16_t first = static_cast<uint16_t>((register_byte << 8) | opcode);
      if (!supported) {
        uint16_t one_unit[] = {first};
        uint32_t index = 0x12345678;
        auto type = dispatch::DispatchInvokeType::kInterface;
        assert(!dispatch::DecodeNterpDispatch(one_unit, &index, &type));
        assert(index == 0x12345678 && type == dispatch::DispatchInvokeType::kInterface);
        const size_t before = calls;
        assert(!dispatch::GuardNterpInvoke(&thread_token, &caller_token, one_unit));
        assert(calls == before);
        continue;
      }
      expected_type = types[opcode - (opcode <= 0x72 ? 0x6e : 0x74)];
      for (uint32_t index : {0u, 1u, 0x1234u, 0xffffu}) {
        expected_index = index;
        uint16_t units[] = {first, static_cast<uint16_t>(index), 0xabcd};
        uint32_t decoded;
        dispatch::DispatchInvokeType type;
        assert(dispatch::DecodeNterpDispatch(units, &decoded, &type));
        assert(decoded == index && type == expected_type);
        resolver_accepts = true;
        assert(!dispatch::GuardNterpInvoke(&thread_token, &caller_token, units));
        resolver_accepts = false;
        assert(dispatch::GuardNterpInvoke(&thread_token, &caller_token, units));
      }
    }
  }
  uint32_t index;
  dispatch::DispatchInvokeType type;
  assert(!dispatch::DecodeNterpDispatch(nullptr, &index, &type));
  assert(!dispatch::DecodeNterpDispatch(invoke, nullptr, &type));
  assert(!dispatch::DecodeNterpDispatch(invoke, &index, nullptr));
  assert(calls == 10 * 256 * 4 * 2);
  std::printf("Nterp dispatch: all 256 opcodes and register bytes, %zu checked calls passed\n", calls);
}
