#include "platform/common/rooted_hierarchy_allocator.h"
#include <cassert>
#include <algorithm>
#include <deque>
#include <memory>
#include <iostream>
using namespace artdcevm::rooted_hierarchy;
namespace {
struct Node {std::string name,requested;Node* parent=nullptr;Node* loader=nullptr;Node* cache=nullptr;uint16_t index=0;};
struct Mock final:Access {
 std::deque<Node*> roots;std::vector<std::unique_ptr<Node>> nodes;std::vector<std::string> order;
 std::vector<void*> owners;std::string failure;bool oom=false,wrong_parent=false,wrong_loader=false,wrong_cache=false;
 Node* New(std::string name){nodes.push_back(std::make_unique<Node>());nodes.back()->name=std::move(name);return nodes.back().get();}
 Root Keep(Node* n){roots.push_back(n);return &roots.back();}
 Node* Value(Root r)const{return r?*static_cast<Node**>(r):nullptr;}
 Root Copy(Root r)override{return Keep(Value(r));}Root Empty()override{return Keep(nullptr);}
 void* Get(Root r)const override{return Value(r);}void Set(Root r,void* n)override{*static_cast<Node**>(r)=static_cast<Node*>(n);}
 Root Loader(Root r)override{return Keep(Value(r)->loader);}Root Cache(Root r)override{return Keep(Value(r)->cache);}
 uint16_t ClassIndex(Root r)const override{return Value(r)->index;}
 std::string RequestedParent(Root r,uint16_t index)override{assert(index==Value(r)->index);return Value(r)->requested;}
 Root Resolve(const std::string& name,Root loader)override{for(const auto& n:nodes)if(n->name==name&&n->loader==Value(loader)&&n->requested.empty())return Keep(n.get());return Keep(nullptr);}
 bool Subclass(Root child,Root parent)const override{for(Node* n=Value(child);n;n=n->parent)if(n==Value(parent))return true;return false;}
 void* Allocate(void* owner,Root old,Root parent,Root cache,uint16_t,Root result)override{
  Node* n=New(Value(old)->name);n->loader=wrong_loader?nullptr:Value(old)->loader;n->cache=wrong_cache?nullptr:Value(cache);n->parent=wrong_parent?nullptr:Value(parent);
  order.push_back(n->name);owners.push_back(owner);Set(result,n);return oom?nullptr:n;
 }
 bool ValidateReplacement(Root old,Root next,Root parent,Root cache)const override{return Value(next)!=Value(old)&&Value(next)->name==Value(old)->name&&Value(next)->loader==Value(old)->loader&&Value(next)->parent==Value(parent)&&Value(next)->cache==Value(cache);}
 void Fail(const char* message)override{failure=message;}
};
}
int main(){
 Mock m;Node* loader=m.New("loader"),*object=m.New("Object"),*parent=m.New("Parent"),*child=m.New("Child");
 object->loader=parent->loader=child->loader=loader;parent->parent=object;child->parent=parent;
 Node* pc=m.New("parent-cache"),*cc=m.New("child-cache");pc->requested="Object";cc->requested="Parent";parent->cache=pc;child->cache=cc;
 Root pr=m.Keep(parent),cr=m.Keep(child),pcr=m.Keep(pc),ccr=m.Keep(cc);
 int parent_owner,child_owner;
 Allocator a;a.Prepare({{&parent_owner,pr,pcr,true},{&child_owner,cr,ccr,false}});
 auto* replacement=static_cast<Node*>(a.Allocate(m,&child_owner,cr,ccr,0));
 assert(replacement&&replacement->parent!=parent&&replacement->parent->name=="Parent");
 assert((m.order==std::vector<std::string>{"Parent","Child"}));
 assert(a.Allocate(m,&child_owner,cr,ccr,0)==replacement&&m.order.size()==2);
 assert(a.Replacements().size()==2);
 assert(!a.Prepare({{&child_owner,cr,ccr,true}})&&a.Replacements().size()==2);
 a.Clear();assert(!a.Prepare({{nullptr,cr,ccr,true}}));m.order.clear();
 // A recursive requested graph cycle refuses before any source allocator call.
 pc->requested="Child";a.Prepare({{&parent_owner,pr,pcr,true},{&child_owner,cr,ccr,false}});
 assert(!a.Allocate(m,&child_owner,cr,ccr,0));assert(m.failure.find("Cycle")!=std::string::npos&&m.order.empty());
 pc->requested="Object";a.Clear();
 // Result slots captured before Ensure*Ids OOM remain available for retirement.
 m.oom=true;a.Prepare({{&child_owner,cr,ccr,true}});assert(!a.Allocate(m,&child_owner,cr,ccr,0));
 assert(a.Replacements().size()==1&&m.Get(a.Replacements()[0].new_class));
 const auto oom_calls=m.order.size();m.oom=false;
 assert(!a.Allocate(m,&child_owner,cr,ccr,0)&&m.order.size()==oom_calls);a.Clear();
 for(int fault=0;fault<3;++fault){m.wrong_parent=fault==0;m.wrong_loader=fault==1;m.wrong_cache=fault==2;
  a.Prepare({{&child_owner,cr,ccr,true}});assert(!a.Allocate(m,&child_owner,cr,ccr,0));
  assert(m.failure.find("Linked replacement")!=std::string::npos&&m.Get(a.Replacements()[0].new_class));a.Clear();}
 m.wrong_parent=m.wrong_loader=m.wrong_cache=false;
 // Same text in another loader never selects the requested original namespace.
 Node* other=m.New("other-loader"),*same=m.New("Parent");same->loader=other;
 a.Prepare({{&parent_owner,m.Keep(same),pcr,true},{&child_owner,cr,ccr,false}});
 auto* namespace_result=static_cast<Node*>(a.Allocate(m,&child_owner,cr,ccr,0));assert(namespace_result&&namespace_result->parent==parent);
 a.Clear();cc->requested="Missing";a.Prepare({{&child_owner,cr,ccr,true}});
 assert(!a.Allocate(m,&child_owner,cr,ccr,0)&&m.failure.find("Cannot resolve")!=std::string::npos);
 // Actual g2 graph: g1 Subject already extends Inserted -> Base. The
 // batch changes Subject -> Outer and structurally updates Outer and Base.
 // All permutations must allocate Base, implicit Inserted, Outer, Subject,
 // with the closest structural ancestor owning the implicit Inserted.
 std::vector<unsigned> permutation{0,1,2};
 do {
  Mock nested;auto* nl=nested.New("loader"),*obj=nested.New("Object");obj->loader=nl;
  auto* b=nested.New("Base"),*inserted=nested.New("Inserted"),*outer=nested.New("Outer"),*subject=nested.New("Subject");
  b->parent=obj;inserted->parent=b;outer->parent=inserted;subject->parent=inserted;
  auto cache=[&](Node* n,const char* requested){auto* c=nested.New(n->name+"-cache");c->requested=requested;n->loader=nl;n->cache=c;return nested.Keep(c);};
  Root bc=cache(b,"Object"),ic=cache(inserted,"Base"),oc=cache(outer,"Inserted"),sc=cache(subject,"Outer");(void)ic;
  Root br=nested.Keep(b),orr=nested.Keep(outer),sr=nested.Keep(subject);
  int bo,oo,so;std::vector<Target> targets{{&so,sr,sc,true},{&oo,orr,oc,true},{&bo,br,bc,true}},ordered;
  for(auto i:permutation)ordered.push_back(targets[i]);
  Allocator graph;assert(graph.Prepare(std::move(ordered)));
  auto* result=static_cast<Node*>(graph.Allocate(nested,&so,sr,sc,0));
  assert(result&&nested.failure.empty());
  assert((nested.order==std::vector<std::string>{"Base","Inserted","Outer","Subject"}));
  assert((nested.owners==std::vector<void*>{&bo,&bo,&oo,&so}));
  assert(result->parent->name=="Outer"&&result->parent!=outer);
  assert(result->parent->parent->name=="Inserted"&&result->parent->parent!=inserted);
  assert(result->parent->parent->parent->name=="Base"&&result->parent->parent->parent!=b);
  assert(graph.Replacements().size()==4);
  // An implicit descendant below Outer must use Outer rather than Base's
  // owner, even when the more distant Base target appears first.
  graph.Clear();nested.order.clear();nested.owners.clear();
  auto* bridge=nested.New("Bridge"),*leaf_node=nested.New("Leaf");bridge->parent=outer;leaf_node->parent=bridge;
  cache(bridge,"Outer");Root lc=cache(leaf_node,"Bridge"),lr=nested.Keep(leaf_node);
  std::vector<Target> again;for(auto i:permutation)again.push_back(targets[i]);
  assert(graph.Prepare(std::move(again))&&graph.Allocate(nested,&so,lr,lc,0));
  assert((nested.order==std::vector<std::string>{"Base","Inserted","Outer","Bridge","Leaf"}));
  assert((nested.owners==std::vector<void*>{&bo,&bo,&oo,&oo,&so}));
 } while(std::next_permutation(permutation.begin(),permutation.end()));
 // Deep implicit descendant chains use an explicit work stack, not recursive
 // native calls. The actual source allocator is entered in parent-first order.
 Mock deep;Node* dl=deep.New("loader"),*base=deep.New("Object");base->loader=dl;
 Node* previous=base;Root structural=nullptr;Root structural_cache=nullptr;Root leaf=nullptr,leaf_cache=nullptr;
 for(unsigned i=0;i<1024;++i){auto* n=deep.New("Deep"+std::to_string(i));auto* c=deep.New("Cache"+std::to_string(i));
  n->loader=dl;n->parent=previous;n->cache=c;c->requested=previous->name;
  leaf=deep.Keep(n);leaf_cache=deep.Keep(c);if(!i){structural=leaf;structural_cache=leaf_cache;}previous=n;}
 Allocator deep_allocator;assert(deep_allocator.Prepare({{&child_owner,structural,structural_cache,true}}));
 assert(deep_allocator.Allocate(deep,&child_owner,leaf,leaf_cache,0));
 assert(deep.order.size()==1024&&deep.order.front()=="Deep0"&&deep.order.back()=="Deep1023");
 std::cout<<"Rooted allocator: nested structural targets in all six batch orders, parent-before-child order, exact loader, cycle refusal, early OOM capture and wrong native parent/loader/cache refusals passed\n";
}
