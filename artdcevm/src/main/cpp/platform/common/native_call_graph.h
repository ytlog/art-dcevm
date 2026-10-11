#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace artdcevm::native_call_graph {
// Ordinary-phase immutable input facts only. This planner does not read ART,
// retain Classes, authenticate DEX/callsite/domain facts, or authorize any frame.
// The production inventory owner must bind the namespace and loader token to
// rooted identities and revalidate the same generation before native mutation.
struct Identity {
 const void* dex_namespace=nullptr;
 uint64_t loader_token=0; // Zero denotes the independently rooted boot namespace.
 void* native_method=nullptr;
 bool operator==(const Identity&other)const noexcept {
  return dex_namespace==other.dex_namespace&&loader_token==other.loader_token&&native_method==other.native_method;
 }
};
enum class MethodKind:uint8_t {Managed,Native};
struct Node {Identity identity;MethodKind kind=MethodKind::Managed;};
enum class InvokeKind:uint8_t {Direct,Static,Super,Virtual,Interface,Custom,Polymorphic};
// ClosedDispatch is a producer's declaration of complete loaded-class target
// coverage, not permission minted by this planner. Every target at that actual
// caller PC must appear; partial virtual/interface domains remain unresolved.
enum class Coverage:uint8_t {Exact,ClosedDispatch,Unresolved};
struct Edge {
 size_t caller=SIZE_MAX,target=SIZE_MAX;
 uint32_t dex_pc=UINT32_MAX;
 InvokeKind kind=InvokeKind::Direct;
 Coverage coverage=Coverage::Exact;
};
enum class Failure:uint8_t {None,MissingIdentity,DuplicateIdentity,InvalidKind,InvalidEdge,InvalidSeed,NativeCaller};
enum class GapKind:uint8_t {UnresolvedTarget,IncompleteDispatch,CustomInvoke,PolymorphicInvoke};
struct Gap {size_t edge=SIZE_MAX,caller=SIZE_MAX;uint32_t dex_pc=UINT32_MAX;GapKind kind=GapKind::UnresolvedTarget;};
struct Plan {
 Failure failure=Failure::None;
 bool complete=false;
 // Stable source node ordinals, sorted in inventory order; names never merge.
 std::vector<size_t> ancestors,interpret,native_boundaries;
 std::vector<Gap> gaps;
};
// O(V+E) graph traversals, no recursion. Identity indexing is expected O(V).
// Unresolved/custom/poly edges anywhere prevent a complete declaration: an
// unknown target may be an omitted caller of a seed even when not yet reachable.
// Native callees terminate graph traversal and never enter the interpreter set.
Plan Build(const std::vector<Node>&,const std::vector<Edge>&,const std::vector<size_t>&seeds);
}
