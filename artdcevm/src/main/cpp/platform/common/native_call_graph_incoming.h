#pragma once
#include "platform/common/native_call_graph_code.h"
#include <memory>

namespace artdcevm::native_call_graph_incoming {
// Immutable incoming half of one schema operation. Captures the actual input
// DEX rather than borrowing the caller buffer or deriving bodies from BEFORE
// ArtMethods. Native/abstract declarations have no interpreter body. Call sites
// retain exact incoming MethodIds; cross-DEX/loader and dynamic resolution must
// subsequently join these facts to the rooted replacement graph.
struct Call {
 native_call_graph::CallSite site{};
 evolution::MappedDexMethodReference reference{};
};
struct Method {
 evolution::Member declaration{};
 uint32_t code_offset=0;
 bool direct=false;
 std::vector<Call> calls;
};
class Inventory final {
 public:
 Inventory();~Inventory();
 Inventory(const Inventory&)=delete;Inventory&operator=(const Inventory&)=delete;
 bool Capture(const std::vector<unsigned char>&,uint32_t class_def_index,
              const std::string&expected_descriptor,std::string*error);
 const evolution::ClassSchema* Schema()const noexcept;
 const std::vector<Method>& Methods()const noexcept;
 // Borrowed views are stable only until this operation's Inventory dies.
 // Unsupported custom/poly invokes explicitly refuse capture until their
 // original typed bridge is covered; this is not a complete dispatch proof.
 bool Code(uint32_t method_index,receiver_guard::CodeView*)const noexcept;
 // Bounded same-content comparison, allocation-free. Runtime mappings must be
 // independently attested live/immutable by the original transaction owner.
 // Relocation is legal; namespace, ClassDef, code offset and all body geometry
 // must still agree. These predicates confer no roots, stop or commit rights.
 bool MatchesNamespace(const evolution::MappedDexMetadata&)const noexcept;
 bool MatchesCode(uint32_t method_index,const receiver_guard::CodeView&)const noexcept;
 private:
 struct State;std::unique_ptr<State> state_;
};
}
