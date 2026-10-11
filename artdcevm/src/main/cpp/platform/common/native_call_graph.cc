#include "platform/common/native_call_graph.h"
#include <unordered_set>
#include <functional>
namespace artdcevm::native_call_graph {
namespace {
struct Hash {
 size_t operator()(const Identity&id)const noexcept {
  size_t h=std::hash<const void*>{}(id.dex_namespace);
  const auto mix=[&](size_t n){h^=n+size_t(0x9e3779b9u)+(h<<6)+(h>>2);};
  mix(std::hash<uint64_t>{}(id.loader_token));mix(std::hash<void*>{}(id.native_method));return h;
 }
};
struct Index {
 std::vector<size_t> offsets,targets;
 Index(size_t nodes,const std::vector<Edge>&edges,bool reverse):offsets(nodes+1) {
  for(const auto&e:edges)if(e.target!=SIZE_MAX)++offsets[(reverse?e.target:e.caller)+1];
  for(size_t i=1;i<offsets.size();++i)offsets[i]+=offsets[i-1];
  targets.resize(offsets.back());auto next=offsets;
  for(const auto&e:edges)if(e.target!=SIZE_MAX){const auto source=reverse?e.target:e.caller;targets[next[source]++]=reverse?e.caller:e.target;}
 }
};
}
Plan Build(const std::vector<Node>&nodes,const std::vector<Edge>&edges,const std::vector<size_t>&seeds){
 Plan result;
 const auto fail=[&](Failure why){Plan denied;denied.failure=why;return denied;};
 std::unordered_set<Identity,Hash> identities;identities.reserve(nodes.size());
 for(const auto&node:nodes){
  if(!node.identity.dex_namespace||!node.identity.native_method)return fail(Failure::MissingIdentity);
  if(node.kind!=MethodKind::Managed&&node.kind!=MethodKind::Native)return fail(Failure::InvalidKind);
  if(!identities.insert(node.identity).second)return fail(Failure::DuplicateIdentity);
 }
 for(size_t i=0;i<edges.size();++i){const auto&e=edges[i];
  if(e.caller>=nodes.size()||(e.target!=SIZE_MAX&&e.target>=nodes.size())||e.dex_pc==UINT32_MAX)return fail(Failure::InvalidEdge);
  if(e.kind>InvokeKind::Polymorphic||e.coverage>Coverage::Unresolved)return fail(Failure::InvalidKind);
  if(nodes[e.caller].kind==MethodKind::Native)return fail(Failure::NativeCaller);
  const auto gap=[&](GapKind kind){result.gaps.push_back({i,e.caller,e.dex_pc,kind});};
  if(e.kind==InvokeKind::Custom)gap(GapKind::CustomInvoke);
  if(e.kind==InvokeKind::Polymorphic)gap(GapKind::PolymorphicInvoke);
  if(e.target==SIZE_MAX||e.coverage==Coverage::Unresolved)gap(GapKind::UnresolvedTarget);
  else if((e.kind==InvokeKind::Virtual||e.kind==InvokeKind::Interface)&&e.coverage!=Coverage::ClosedDispatch)gap(GapKind::IncompleteDispatch);
 }
 std::vector<uint8_t> upstream(nodes.size(),0),selected(nodes.size(),0);
 std::vector<size_t> queue;queue.reserve(nodes.size());
 for(auto seed:seeds){if(seed>=nodes.size())return fail(Failure::InvalidSeed);if(!upstream[seed]){upstream[seed]=1;queue.push_back(seed);}}
 const Index reverse(nodes.size(),edges,true),forward(nodes.size(),edges,false);
 for(size_t at=0;at<queue.size();++at){const size_t node=queue[at];
  for(size_t p=reverse.offsets[node];p<reverse.offsets[node+1];++p){const auto caller=reverse.targets[p];if(!upstream[caller]){upstream[caller]=1;queue.push_back(caller);}}
 }
 queue.clear();
 for(size_t i=0;i<nodes.size();++i)if(upstream[i]){result.ancestors.push_back(i);selected[i]=1;queue.push_back(i);}
 for(size_t at=0;at<queue.size();++at){const size_t node=queue[at];
  if(nodes[node].kind==MethodKind::Native)continue;
  for(size_t p=forward.offsets[node];p<forward.offsets[node+1];++p){const auto target=forward.targets[p];if(!selected[target]){selected[target]=1;queue.push_back(target);}}
 }
 for(size_t i=0;i<nodes.size();++i)if(selected[i]){
  (nodes[i].kind==MethodKind::Native?result.native_boundaries:result.interpret).push_back(i);
 }
 result.complete=result.gaps.empty();return result;
}
}
