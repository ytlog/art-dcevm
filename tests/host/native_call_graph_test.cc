#include "platform/common/native_call_graph.h"
#include <cassert>
#include <algorithm>
using namespace artdcevm::native_call_graph;
namespace {
Node Method(size_t id,uint64_t loader=0,size_t dex=1,MethodKind kind=MethodKind::Managed){return {{reinterpret_cast<void*>(dex),loader,reinterpret_cast<void*>(id+1)},kind};}
Edge Call(size_t a,size_t b,uint32_t pc=0,InvokeKind kind=InvokeKind::Direct,Coverage coverage=Coverage::Exact){return {a,b,pc,kind,coverage};}
bool Is(const std::vector<size_t>&actual,std::initializer_list<size_t>want){return actual==std::vector<size_t>(want);}
}
int main(){
 // seed B; upstream A/Entry; their other descendants C/D must interpret too.
 // An unrelated component remains compiled, native N is an explicit boundary.
 std::vector<Node> nodes;for(size_t i=0;i<8;++i)nodes.push_back(Method(i));nodes[6].kind=MethodKind::Native;
 std::vector<Edge> edges{Call(0,1,4),Call(1,2,8),Call(1,3,12),Call(3,4,16),Call(4,3,20),Call(0,6,24),Call(5,7,28)};
 auto p=Build(nodes,edges,{2});assert(p.failure==Failure::None&&p.complete);
 assert(Is(p.ancestors,{0,1,2})&&Is(p.interpret,{0,1,2,3,4})&&Is(p.native_boundaries,{6}));
 p=Build(nodes,edges,{2,2,3});assert(p.complete&&Is(p.ancestors,{0,1,2,3,4}));
 // Identity namespaces and loaders remain separate even with equal native keys.
 std::vector<Node> isolated{Method(0,1,1),Method(0,2,1),Method(0,1,2)};
 p=Build(isolated,{}, {1});assert(p.complete&&Is(p.interpret,{1}));
 isolated.push_back(isolated[0]);assert(Build(isolated,{},{}).failure==Failure::DuplicateIdentity);
 isolated.pop_back();isolated[0].identity.dex_namespace=nullptr;assert(Build(isolated,{},{}).failure==Failure::MissingIdentity);
 // Multiple independently covered dynamic targets at one real PC are retained.
 edges.push_back(Call(3,7,30,InvokeKind::Interface,Coverage::ClosedDispatch));
 edges.push_back(Call(3,5,30,InvokeKind::Interface,Coverage::ClosedDispatch));
 p=Build(nodes,edges,{2});assert(p.complete&&p.interpret.size()==7&&Is(p.native_boundaries,{6}));
 auto partial=edges;partial.back().coverage=Coverage::Exact;p=Build(nodes,partial,{2});assert(!p.complete&&p.failure==Failure::None&&p.gaps.size()==1&&p.gaps[0].kind==GapKind::IncompleteDispatch&&p.gaps[0].dex_pc==30);
 // Unknown targets anywhere invalidate reverse-closure completeness, including
 // callers not in the presently known component. Partial sets grant nothing.
 auto unresolved=edges;unresolved.push_back(Call(5,SIZE_MAX,40));p=Build(nodes,unresolved,{2});assert(!p.complete&&p.gaps.back().caller==5&&p.gaps.back().kind==GapKind::UnresolvedTarget);
 for(auto kind:{InvokeKind::Custom,InvokeKind::Polymorphic}){auto unsupported=edges;unsupported.push_back(Call(5,7,42,kind));p=Build(nodes,unsupported,{2});assert(!p.complete&&!p.gaps.empty());}
 auto invalid=edges;invalid.push_back(Call(6,7,46));assert(Build(nodes,invalid,{2}).failure==Failure::NativeCaller);
 invalid=edges;invalid[0].dex_pc=UINT32_MAX;assert(Build(nodes,invalid,{2}).failure==Failure::InvalidEdge);
 invalid=edges;invalid[0].target=nodes.size();assert(Build(nodes,invalid,{2}).failure==Failure::InvalidEdge);
 assert(Build(nodes,edges,{nodes.size()}).failure==Failure::InvalidSeed);
 p=Build(nodes,{}, {6});assert(p.complete&&p.interpret.empty()&&Is(p.native_boundaries,{6}));
 assert(Build({}, {}, {}).complete);
 // Long chain plus back edge: iterative closure, linear-sized outputs, no
 // recursive stack growth or quadratic membership scans.
 constexpr size_t count=100000;std::vector<Node> deep;std::vector<Edge> chain;deep.reserve(count);chain.reserve(count);
 for(size_t i=0;i<count;++i){deep.push_back(Method(i,1,3));if(i)chain.push_back(Call(i-1,i,uint32_t(i)));}
 chain.push_back(Call(count-1,0,0));p=Build(deep,chain,{count/2});assert(p.complete&&p.ancestors.size()==count&&p.interpret.size()==count&&p.native_boundaries.empty());
}
