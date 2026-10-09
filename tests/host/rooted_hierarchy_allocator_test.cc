#include "platform/common/rooted_hierarchy_allocator.h"
#include <cassert>
#include <deque>
#include <memory>
#include <iostream>
using namespace artdcevm::rooted_hierarchy;
namespace {
struct Node {std::string name,requested;Node* parent=nullptr;Node* loader=nullptr;Node* cache=nullptr;uint16_t index=0;};
struct Mock final:Access {
 std::deque<Node*> roots;std::vector<std::unique_ptr<Node>> nodes;std::vector<std::string> order;
 std::string failure;bool oom=false,wrong_parent=false,wrong_loader=false,wrong_cache=false;
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
 void* Allocate(void*,Root old,Root parent,Root cache,uint16_t,Root result)override{
  Node* n=New(Value(old)->name);n->loader=wrong_loader?nullptr:Value(old)->loader;n->cache=wrong_cache?nullptr:Value(cache);n->parent=wrong_parent?nullptr:Value(parent);
  order.push_back(n->name);Set(result,n);return oom?nullptr:n;
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
 std::cout<<"Rooted allocator: recursive parent-before-child order, exact loader, cycle refusal, early OOM capture and wrong native parent/loader/cache refusals passed\n";
}
