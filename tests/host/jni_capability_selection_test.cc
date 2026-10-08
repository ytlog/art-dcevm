#include "platform/jni_access/capability_selection.h"
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace artdcevm::jni_access;
namespace {
constexpr const char* kNames[] = {
  "_ZNK3art6Thread13DecodeJObjectEP8_jobject",
  "_ZNK3art6Thread19DecodeGlobalJObjectEP8_jobject",
  "_ZNK3art6Thread22GetPeerFromOtherThreadEv",
  "_ZN3art6Thread22GetPeerFromOtherThreadEv"
};
struct Symbols { unsigned mask; unsigned requests = 0; };
void* Resolve(void* context, const char* name) {
  auto& symbols = *static_cast<Symbols*>(context);
  for (unsigned i = 0; i < 4; ++i) {
    if (std::strcmp(name, kNames[i]) == 0) {
      symbols.requests |= 1u << i;
      // The selector may only observe addresses; these are never callable.
      return (symbols.mask & (1u << i)) ? &symbols : nullptr;
    }
  }
  assert(false && "Selector requested an unexpected symbol");
  return nullptr;
}
void FailsPreserving(SymbolResolver resolver, void* context) {
  const char* sentinel = "sentinel";
  RuntimeCandidate candidate{sentinel, DecoderModel::kGlobalOnly, PeerModel::kConst};
  std::string error;
  assert(!SelectRuntimeCandidate(resolver, context, &candidate, &error));
  assert(!error.empty());
  assert(candidate.id == sentinel && candidate.decoder == DecoderModel::kGlobalOnly &&
         candidate.peer == PeerModel::kConst);
  assert(!SelectRuntimeCandidate(resolver, context, &candidate, nullptr));
  assert(candidate.id == sentinel);
}
void* StatelessResolve(void*, const char* name) {
  static unsigned sentinel;
  return (std::strcmp(name, kNames[1]) == 0 || std::strcmp(name, kNames[3]) == 0)
      ? &sentinel : nullptr;
}
}
int main() {
  unsigned successes = 0;
  for (unsigned mask = 0; mask < 16; ++mask) {
    Symbols symbols{mask};
    const unsigned peers = mask & 12;
    const bool valid = (mask & 3) && (peers == 4 || peers == 8);
    if (!valid) {
      FailsPreserving(Resolve, &symbols);
      continue;
    }
    RuntimeCandidate candidate{};
    std::string error = "old failure";
    assert(SelectRuntimeCandidate(Resolve, &symbols, &candidate, &error));
    assert(error.empty() && candidate.id != nullptr);
    assert(candidate.decoder == ((mask & 2) ? DecoderModel::kGlobalOnly : DecoderModel::kLegacyAny));
    assert(candidate.peer == ((mask & 4) ? PeerModel::kConst : PeerModel::kNonconst));
    // When both decoders exist, global is chosen without inspecting the broader
    // legacy decoder: the caller's strong global reference is its only input.
    assert(((symbols.requests & 1) != 0) == ((mask & 2) == 0));
    assert(SelectRuntimeCandidate(Resolve, &symbols, &candidate, nullptr));
    ++successes;
  }
  FailsPreserving(nullptr, nullptr);
  std::string error;
  Symbols symbols{10};
  assert(!SelectRuntimeCandidate(Resolve, &symbols, nullptr, &error) && !error.empty());
  assert(symbols.requests == 0);
  assert(!SelectRuntimeCandidate(nullptr, nullptr, nullptr, nullptr));
  RuntimeCandidate candidate{};
  // A stateless resolver is valid; context is caller-owned and may be null.
  assert(SelectRuntimeCandidate(StatelessResolve, nullptr, &candidate, &error));
  assert(candidate.decoder == DecoderModel::kGlobalOnly && candidate.peer == PeerModel::kNonconst);
  assert(successes == 6);
  puts("JNI capability selection: all 16 symbol sets, global preference, peer ambiguity and failure preservation passed");
}
