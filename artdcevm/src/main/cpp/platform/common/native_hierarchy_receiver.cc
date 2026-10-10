#include "platform/common/native_hierarchy_receiver.h"
#include "platform/common/native_narrowing_authority.h"
#include "platform/common/native_hierarchy_parent.h"
#include "platform/common/rooted_graph_class_identity.h"
#include "platform/common/modern_field_metadata.h"
#include "platform/common/native_interface_graph.h"
#include "platform/common/method_identity_calibration.h"
#include <algorithm>
#include <cassert>
namespace artdcevm::native_hierarchy_receiver {
namespace {
using native_hierarchy_graph::Graph;using native_hierarchy_graph::Input;using native_hierarchy_graph::Node;using native_hierarchy_graph::Prepared;
constexpr size_t absent=SIZE_MAX;
bool Fail(std::string* e,const char* text){if(e)*e=text;return false;}
bool RootIdentity(const Node& node)noexcept{
 if(!node.RootedClass()||(node.loader&&!node.RootedLoader())||(!node.loader&&(node.strong_loader||node.loader_root.owner)))return false;
 void* value=nullptr;
 if(node.class_root.owner&&(!node.class_root.owner->Read(node.class_root.slot,&value)||value!=node.klass))return false;
 if(node.loader_root.owner&&(!node.loader_root.owner->Read(node.loader_root.slot,&value)||value!=node.loader))return false;
 return true;
}
struct Fact {
 jclass root; jobject loader_root; native_hierarchy_graph::RootWitness class_witness,loader_witness,name_witness;std::string proxy_name_text,negative_proxy_name;std::string descriptor;uint32_t flags;std::optional<uint32_t> proposed_flags;
 size_t parent,proposed_parent;std::vector<size_t> interfaces,proposed_interfaces;
 bool initialized,affected;
 std::vector<size_t> before_relations,after_relations,before_order,after_order;
 mutable std::vector<void*> before_native,after_native;
 bool linked=false;
};
// Follow all ancestry and transitive interface edges, including changed batch
// ancestors. Index identity, never descriptor equality, defines a relation.
bool Relations(const Graph& graph,size_t root,bool proposed,std::vector<size_t>* output,std::vector<unsigned char>& color){
 const size_t count=graph.nodes.size();if(!output||root>=count||color.size()!=count)return false;
 struct Frame{size_t n,edge;};std::vector<Frame> stack{{root,0}};output->clear();output->push_back(root);color[root]=1;
 // This ordinary preparation scratch is shared only within one complete graph
 // walk. Restore exactly the discovered nodes on success, refusal or exception.
 struct Reset {std::vector<unsigned char>& color;std::vector<size_t>& discovered;
  ~Reset(){for(auto n:discovered)color[n]=0;}
 } reset{color,*output};
 while(!stack.empty()){auto& f=stack.back();const auto& node=graph.nodes[f.n];size_t next=absent;const auto& interfaces=proposed?node.proposed_interfaces:node.interfaces;
  if(f.edge==0){++f.edge;next=proposed?node.proposed_parent:node.parent;}else if(f.edge<=interfaces.size())next=interfaces[f.edge++-1];else{color[f.n]=2;stack.pop_back();continue;}
  if(next==absent)continue;if(next>=count||color[next]==1)return false;if(!color[next]){output->push_back(next);color[next]=1;stack.push_back({next,0});}
 }return true;
}
std::vector<size_t> Interfaces(const Graph& graph,const std::vector<size_t>& relations,size_t holder){std::vector<size_t> result;for(auto i:relations)if(i!=holder&&(graph.nodes[i].flags&0x200u))result.push_back(i);return result;}
bool Schema(const Graph& graph,const Node& node,const evolution::ClassSchema& schema,bool proposed){
 if(schema.descriptor!=node.descriptor||(schema.access&0x7631u)!=((proposed?node.ProposedFlags():node.flags)&0x7631u))return false;size_t p=proposed?node.proposed_parent:node.parent;const auto& interfaces=proposed?node.proposed_interfaces:node.interfaces;
 if(p==absent){if(schema.superclass!=(node.flags&0x200u?"Ljava/lang/Object;":""))return false;}else if(schema.superclass!=graph.nodes[p].descriptor)return false;if(schema.interfaces.size()!=interfaces.size())return false;for(size_t i=0;i<interfaces.size();++i)if(schema.interfaces[i]!=graph.nodes[interfaces[i]].descriptor)return false;return true;
}
void* Selected(const Node& node){return node.affected?node.replacement:node.klass;}
bool Order(const Graph& graph,const std::vector<void*>& actual,const std::vector<size_t>& expected,bool proposed,std::vector<size_t>* order){
 if(actual.size()!=expected.size())return false;order->clear();order->reserve(actual.size());
 for(auto klass:actual){size_t found=absent;for(auto index:expected)if((proposed?Selected(graph.nodes[index]):graph.nodes[index].klass)==klass){if(found!=absent)return false;found=index;}if(found==absent||std::find(order->begin(),order->end(),found)!=order->end())return false;order->push_back(found);}return true;
}
}
struct Preservation::State {
 explicit State(const modern_field_metadata::FieldMetadata& m):metadata(&m){}
 const modern_field_metadata::FieldMetadata* metadata;modern_field_metadata::ReadContext read_context{};bool contextual=false;
 const modern_field_metadata::ReadContext* Context()const noexcept{return contextual?&read_context:nullptr;}
 bool Extent(const void*p,size_t*out)const noexcept{return contextual?read_context.extent&&read_context.extent(read_context.owner,p,out):method_identity::ReadableMappingExtent(p,out);}
 native_narrowing::Authority*narrowing=nullptr;bool narrows=false;
 const Graph* graph=nullptr;
 std::vector<Fact> facts;std::vector<size_t> inputs;mutable std::vector<size_t> pair_nodes;mutable const source_commit::Plan* plan=nullptr;bool staged=false;
 bool Matches(const Graph& g)const noexcept{
  if(!staged||graph!=&g||g.nodes.size()!=facts.size()||g.inputs!=inputs)return false;
  for(size_t i=0;i<facts.size();++i){const auto& f=facts[i];const auto& n=g.nodes[i];if(n.strong_class!=f.root||n.strong_loader!=f.loader_root||n.class_root!=f.class_witness||n.loader_root!=f.loader_witness||n.proxy_name_root!=f.name_witness||n.proxy_name_text!=f.proxy_name_text||n.negative_proxy_name!=f.negative_proxy_name||!n.RootedClass()||(n.loader&&!n.RootedLoader())||n.descriptor!=f.descriptor||n.flags!=f.flags||n.proposed_flags!=f.proposed_flags||n.parent!=f.parent||n.proposed_parent!=f.proposed_parent||n.interfaces!=f.interfaces||n.proposed_interfaces!=f.proposed_interfaces||n.initialized!=f.initialized||n.affected!=f.affected||!n.klass||(!n.loader&&(n.strong_loader||n.loader_root.owner)))return false;}
  return true;
 }
 bool Identity(void* klass,const Node& node,void* parent,bool proposed=false)const noexcept{
  if(!klass||node.descriptor.empty()||node.descriptor.front()!='L')return false;size_t extent=0;class_metadata::State state;
  const auto& layout=metadata->StorageModel()==modern_field_metadata::Model::kSplit?class_metadata::kLegacy:class_metadata::k17;
  if(!Extent(klass,&extent)||!class_metadata::ReadState(layout,klass,extent,&state)||state.IsPrimitive()||(state.access_flags&0x7631u)!=((proposed?node.ProposedFlags():node.flags)&0x7631u))return false;
  field_copy::CopyClassView view;return native_hierarchy_graph::ClassIdentity(*metadata,node,klass,&view,nullptr,Context())&&view.loader==node.loader&&view.parent==parent&&view.descriptor&&node.descriptor==view.descriptor;
 }
 bool Originals(const Graph& g)const noexcept{
  if(!Matches(g))return false;
  for(size_t i=0;i<facts.size();++i){const auto& n=g.nodes[i];const auto& f=facts[i];void*parent=nullptr;if(!NativeSuperclass(g,n,false,&parent)||!Identity(n.klass,n,parent))return false;for(size_t j=0;j<f.before_order.size();++j)f.before_native[j]=g.nodes[f.before_order[j]].klass;if(!native_interface_graph::MatchesInContext(*metadata,n.klass,f.before_native,nullptr,Context()))return false;}
  return true;
 }
 bool Replacement(const Graph& g,size_t index,void* klass)const noexcept{
  const auto& n=g.nodes[index];const auto& f=facts[index];void* parent=n.proposed_parent==absent?nullptr:Selected(g.nodes[n.proposed_parent]);if(n.proposed_parent!=absent&&!parent)return false;
  if(!f.linked||!Identity(klass,n,parent,true))return false;for(size_t j=0;j<f.after_order.size();++j){void* value=Selected(g.nodes[f.after_order[j]]);if(!value)return false;f.after_native[j]=value;}return native_interface_graph::MatchesInContext(*metadata,klass,f.after_native,nullptr,Context());
 }
};
Preservation::Preservation(const modern_field_metadata::FieldMetadata& m):state_(new State(m)){}
void Preservation::UseReadContext(const modern_field_metadata::ReadContext&c)noexcept{state_->read_context=c;state_->contextual=true;}
bool Preservation::UseNarrowing(native_narrowing::Authority&owner)noexcept{if(state_->staged||state_->narrowing)return false;state_->narrowing=&owner;return true;}
Preservation::~Preservation()=default;
void Preservation::Reset()noexcept{state_->facts.clear();state_->inputs.clear();state_->pair_nodes.clear();state_->plan=nullptr;state_->graph=nullptr;state_->staged=false;state_->contextual=false;state_->narrowing=nullptr;state_->narrows=false;}
bool Preservation::VerifyStaged(const Graph& graph,const std::vector<Input>& inputs,std::string* error)const{
 auto& s=*state_;if(s.staged||!s.metadata->Calibrated()||graph.nodes.empty()||graph.inputs.empty()||inputs.size()!=graph.inputs.size())return Fail(error,"Additive receiver owner requires a fresh actual public graph");
 std::vector<Fact> facts;facts.reserve(graph.nodes.size());
 std::vector<unsigned char> relation_colors(graph.nodes.size());
 for(size_t i=0;i<graph.nodes.size();++i){const auto& n=graph.nodes[i];if(!RootIdentity(n)||!n.klass||n.descriptor.size()<3||n.descriptor.front()!='L'||n.descriptor.back()!=';'||(n.loader&&!n.RootedLoader())||(n.parent!=absent&&n.parent>=graph.nodes.size())||(n.proposed_parent!=absent&&n.proposed_parent>=graph.nodes.size()))return Fail(error,"Receiver relation has no rooted ordinary Class proof");
  if((n.flags&0x200u)?(n.parent!=absent||n.proposed_parent!=absent):((n.parent==absent||n.proposed_parent==absent)&&(n.descriptor!="Ljava/lang/Object;"||n.loader)))return Fail(error,"Receiver role has unproved Object/interface superclass semantics");
  Fact f{n.strong_class,n.strong_loader,n.class_root,n.loader_root,n.proxy_name_root,n.proxy_name_text,n.negative_proxy_name,n.descriptor,n.flags,n.proposed_flags,n.parent,n.proposed_parent,n.interfaces,n.proposed_interfaces,n.initialized,n.affected,{},{},{},{},{},{},false};
  if(((n.flags^n.ProposedFlags())&~0x410u)||(n.ProposedFlags()&0x410u)==0x410u)return Fail(error,"Receiver proposal changes role/access or combines final and abstract");
  if(n.parent!=absent&&(graph.nodes[n.parent].flags&0x210u))return Fail(error,"Original receiver superclass role or finality is invalid");
  if(n.proposed_parent!=absent&&(graph.nodes[n.proposed_parent].ProposedFlags()&0x210u))return Fail(error,"Proposed receiver superclass role or finality is invalid");
  if(!Relations(graph,i,false,&f.before_relations,relation_colors)||!Relations(graph,i,true,&f.after_relations,relation_colors))return Fail(error,"Receiver graph contains cycle or unknown identity");
  for(const auto* edges:{&n.interfaces,&n.proposed_interfaces})for(auto edge:*edges)if(!(graph.nodes[edge].flags&0x200u))return Fail(error,"Receiver interface edge names a class");
  if(n.affected)for(auto old:f.before_relations)if(std::find(f.after_relations.begin(),f.after_relations.end(),old)==f.after_relations.end()){if(!s.narrowing)return Fail(error,"Receiver assignability narrowing lacks concrete permanent dispatch, retained storage and frame recovery owner");s.narrows=true;}
  // Each original native IfTable must independently equal its complete public
  // interface closure; source order is kept for no-allocation final checking.
  void*native_parent=nullptr;if(!NativeSuperclass(graph,n,false,&native_parent)||!s.Identity(n.klass,n,native_parent)||!native_interface_graph::Read(*s.metadata,n.klass,&f.before_native,error)||!Order(graph,f.before_native,Interfaces(graph,f.before_relations,i),false,&f.before_order))return Fail(error,"Original native receiver graph differs from public proof");
  f.after_native.reserve(Interfaces(graph,f.after_relations,i).size());f.after_order.reserve(f.after_native.capacity());facts.push_back(std::move(f));
 }
 for(size_t i=0;i<inputs.size();++i){size_t n=graph.inputs[i];if(n>=graph.nodes.size()||!graph.nodes[n].affected||inputs[i].strong_original!=graph.nodes[n].strong_class||((inputs[i].before.access^inputs[i].after.access)&~0x410u)||inputs[i].before.descriptor!=graph.nodes[n].descriptor||inputs[i].after.descriptor!=graph.nodes[n].descriptor||!Schema(graph,graph.nodes[n],inputs[i].before,false)||!Schema(graph,graph.nodes[n],inputs[i].after,true)||std::find(graph.inputs.begin(),graph.inputs.begin()+i,n)!=graph.inputs.begin()+i)return Fail(error,"Receiver source role/modifier or exact target proof differs");}
 for(size_t n=0;n<facts.size();++n){bool expected=false;for(auto root:graph.inputs)expected|=std::find(facts[n].before_relations.begin(),facts[n].before_relations.end(),root)!=facts[n].before_relations.end()||std::find(facts[n].after_relations.begin(),facts[n].after_relations.end(),root)!=facts[n].after_relations.end();if(expected!=graph.nodes[n].affected)return Fail(error,"Receiver snapshot omitted implicit affected relation cohort");}
 if(s.narrows&&!s.narrowing->Prepare(graph,error))return false;
 s.pair_nodes.assign(size_t(std::count_if(graph.nodes.begin(),graph.nodes.end(),[](const auto& n){return n.affected;})),absent);s.facts=std::move(facts);s.inputs=graph.inputs;s.graph=&graph;s.staged=true;return true;
}
bool Preservation::VerifyPrepared(const Graph& graph,const Prepared& prepared,std::string* error)const{
 auto& s=*state_;if(!s.Originals(graph))return Fail(error,"Staged receiver public/native graph changed");auto it=std::find_if(graph.nodes.begin(),graph.nodes.end(),[&](const auto& n){return n.klass==prepared.old_class;});if(it==graph.nodes.end()||!it->affected||!prepared.source_owner||!prepared.cache||!prepared.declarations)return Fail(error,"Receiver allocation lacks actual affected source owner");if(!Schema(graph,*it,*prepared.declarations,true))return Fail(error,"Receiver allocation declaration differs from staged proposal");size_t index=size_t(it-graph.nodes.begin());auto& fact=s.facts[index];
 void* parent=it->proposed_parent==absent?nullptr:Selected(graph.nodes[it->proposed_parent]);if((it->proposed_parent!=absent&&!parent)||prepared.parent!=parent)return Fail(error,"Receiver allocation selected an unproved parent");
 if(!prepared.new_class)return true;
 if(fact.linked||!s.Identity(prepared.new_class,*it,parent,true))return Fail(error,"Linked receiver Class has different namespace/role/parent");
 std::vector<void*> native;std::vector<size_t> order;if(!native_interface_graph::Read(*s.metadata,prepared.new_class,&native,error)||!Order(graph,native,Interfaces(graph,fact.after_relations,index),true,&order))return Fail(error,"Linked Class loses exact proposed interface relations");
 fact.after_native=std::move(native);fact.after_order=std::move(order);fact.linked=true;return true;
}
bool Preservation::Verify(const Graph& graph,const source_commit::Plan& plan)const noexcept{
 const auto& s=*state_;if(plan.old_classes.size()!=plan.new_classes.size()||plan.old_classes.size()!=s.pair_nodes.size()||(s.plan&&s.plan!=&plan)||!s.Originals(graph))return false;if(s.plan)for(size_t i=0;i<s.pair_nodes.size();++i)if(plan.old_classes[i]!=graph.nodes[s.pair_nodes[i]].klass||plan.new_classes[i]!=graph.nodes[s.pair_nodes[i]].replacement)return false;size_t affected=0;
 for(size_t n=0;n<graph.nodes.size();++n){const auto& node=graph.nodes[n];if(!node.affected)continue;++affected;size_t pair=absent;for(size_t i=0;i<plan.old_classes.size();++i)if(plan.old_classes[i]==node.klass){if(pair!=absent)return false;pair=i;}if(pair==absent||plan.new_classes[pair]!=node.replacement||!s.Replacement(graph,n,node.replacement))return false;}
 if(affected!=plan.old_classes.size())return false;if(!s.plan){for(size_t i=0;i<plan.old_classes.size();++i){auto it=std::find_if(graph.nodes.begin(),graph.nodes.end(),[&](const auto& n){return n.klass==plan.old_classes[i];});s.pair_nodes[i]=size_t(it-graph.nodes.begin());}s.plan=&plan;}return true;
}
bool Preservation::VerifyFinal(const Graph&graph,const source_commit::Plan&plan)const noexcept{auto&s=*state_;return Verify(graph,plan)&&(!s.narrows||(s.contextual&&s.narrowing&&s.narrowing->Validate(graph,plan,s.read_context)));}
void Preservation::BeginCommit(const Graph& graph,const source_commit::Plan& plan)const noexcept{(void)graph;(void)plan;assert(state_->graph==&graph&&state_->staged&&state_->plan==&plan&&plan.old_classes.size()==plan.new_classes.size());if(state_->narrows){if(!state_->narrowing)std::abort();state_->narrowing->BeginCommit(graph,plan);}}
}
