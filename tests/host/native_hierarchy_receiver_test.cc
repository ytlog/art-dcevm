#include "native_hierarchy_receiver_fixture.h"
#include "platform/common/native_hierarchy_receiver.h"
using native_hierarchy_receiver::Preservation;
static Node N(Fake& f,size_t p=SIZE_MAX,std::vector<size_t> interfaces={}){Node n;n.strong_class=reinterpret_cast<jclass>(&f);n.klass=&f;n.descriptor=f.descriptor;n.flags=f.bytes.access_flags;n.initialized=true;n.parent=n.proposed_parent=p;n.interfaces=n.proposed_interfaces=std::move(interfaces);return n;}
static evolution::ClassSchema S(const Graph& g,size_t index,bool proposed){const auto& n=g.nodes[index];size_t p=proposed?n.proposed_parent:n.parent;const auto& edges=proposed?n.proposed_interfaces:n.interfaces;evolution::ClassSchema s;s.descriptor=n.descriptor;s.access=n.flags;s.superclass=p==SIZE_MAX?(n.flags&0x200u?"Ljava/lang/Object;":""):g.nodes[p].descriptor;for(auto i:edges)s.interfaces.push_back(g.nodes[i].descriptor);return s;}
struct Fixture {
 Fake object{"Ljava/lang/Object;"},base{"Lpkg/Base;",&object},a{"Lpkg/A;",&base},b{"Lpkg/B;",&a},parent{"Lpkg/AddedParent;",&base},i0{"Lpkg/I0;",nullptr,0x601},i2{"Lpkg/I2;",nullptr,0x601},i3{"Lpkg/I3;",nullptr,0x601},i4{"Lpkg/I4;",nullptr,0x601},other{"Lpkg/Caller;",&object};Graph graph;std::vector<Input> inputs;
 Fixture(){
  graph.nodes={N(object),N(base,0),N(a,1,{6}),N(b,2),N(parent,1,{7}),N(i0),N(i2,SIZE_MAX,{5}),N(i3,SIZE_MAX,{5}),N(i4,SIZE_MAX,{6,7}),N(other,0,{5})};
  a.native_interfaces={&i2,&i0};b.native_interfaces={&i0,&i2};parent.native_interfaces={&i3,&i0};i2.native_interfaces={&i0};i3.native_interfaces={&i0};i4.native_interfaces={&i2,&i3,&i0};other.native_interfaces={&i0};
  graph.inputs={2};graph.nodes[2].proposed_parent=4;graph.nodes[2].proposed_interfaces={8};graph.nodes[2].affected=graph.nodes[3].affected=true;inputs={{graph.nodes[2].strong_class,S(graph,2,false),S(graph,2,true)}};
 }
};
int main(){
 modern_field_metadata::FieldMetadata metadata;assert(metadata.Calibrate({},nullptr,nullptr,nullptr,0,nullptr));std::string error;
 {
  Fixture f;Preservation owner(metadata);assert(owner.VerifyStaged(f.graph,f.inputs,&error));
  int source=0,cache=0;Prepared p{&source,&f.a,nullptr,&cache,&f.parent,7,&f.inputs[0].after};assert(owner.VerifyPrepared(f.graph,p,&error));auto wrong=f.inputs[0].after;wrong.superclass=f.object.descriptor;p.declarations=&wrong;assert(!owner.VerifyPrepared(f.graph,p,&error));p.declarations=&f.inputs[0].after;
  // Source moving-GC roots are authority-owned. Receiver witnesses use frozen
  // node indices, so every relocated Class/parent/interface must be read anew.
  Fake moved_object=f.object,moved_base=f.base,moved_a=f.a,moved_b=f.b,moved_parent=f.parent,moved_i0=f.i0,moved_i2=f.i2,moved_i3=f.i3,moved_i4=f.i4,moved_other=f.other;
  moved_base.parent=&moved_object;moved_a.parent=&moved_base;moved_b.parent=&moved_a;moved_parent.parent=&moved_base;moved_other.parent=&moved_object;
  moved_a.native_interfaces={&moved_i2,&moved_i0};moved_b.native_interfaces={&moved_i0,&moved_i2};moved_parent.native_interfaces={&moved_i3,&moved_i0};moved_i2.native_interfaces={&moved_i0};moved_i3.native_interfaces={&moved_i0};moved_i4.native_interfaces={&moved_i2,&moved_i3,&moved_i0};moved_other.native_interfaces={&moved_i0};
  std::vector<Fake*> moved{&moved_object,&moved_base,&moved_a,&moved_b,&moved_parent,&moved_i0,&moved_i2,&moved_i3,&moved_i4,&moved_other};for(size_t i=0;i<moved.size();++i)f.graph.nodes[i].klass=moved[i];
  assert(!owner.VerifyPrepared(f.graph,p,&error));p.old_class=&moved_a;p.parent=&moved_parent;assert(owner.VerifyPrepared(f.graph,p,&error));
  Fake new_a(f.a.descriptor.c_str(),&moved_parent),new_b(f.b.descriptor.c_str(),&new_a);new_a.native_interfaces={&moved_i3,&moved_i0,&moved_i4,&moved_i2};new_b.native_interfaces={&moved_i4,&moved_i2,&moved_i0,&moved_i3};
  p.new_class=&new_a;assert(owner.VerifyPrepared(f.graph,p,&error));f.graph.nodes[2].replacement=&new_a;auto implicit=S(f.graph,3,true);Prepared q{&source,&moved_b,&new_b,&cache,&new_a,8,&implicit};assert(owner.VerifyPrepared(f.graph,q,&error));f.graph.nodes[3].replacement=&new_b;
  source_commit::Plan plan;plan.old_classes={&moved_a,&moved_b};plan.new_classes={&new_a,&new_b};
  auto another_plan=plan;forbid_alloc=true;assert(owner.Verify(f.graph,plan));assert(!owner.Verify(f.graph,another_plan));new_a.native_interfaces[3]=&moved_i3;assert(!owner.Verify(f.graph,plan));new_a.native_interfaces[3]=&moved_i2;new_b.parent=&moved_base;assert(!owner.Verify(f.graph,plan));new_b.parent=&new_a;f.graph.nodes[9].flags^=0x10;assert(!owner.Verify(f.graph,plan));f.graph.nodes[9].flags^=0x10;assert(owner.Verify(f.graph,plan));owner.BeginCommit(f.graph,plan);forbid_alloc=false;
  // A second complete relocation after linked preparation changes both the
  // public roots and unpublished replacements; no receiver-owned raw roots exist.
  Fake again_object=moved_object,again_base=moved_base,again_a=moved_a,again_b=moved_b,again_parent=moved_parent,again_i0=moved_i0,again_i2=moved_i2,again_i3=moved_i3,again_i4=moved_i4,again_other=moved_other,again_new_a=new_a,again_new_b=new_b;
  again_base.parent=&again_object;again_a.parent=&again_base;again_b.parent=&again_a;again_parent.parent=&again_base;again_other.parent=&again_object;again_new_a.parent=&again_parent;again_new_b.parent=&again_new_a;
  again_a.native_interfaces={&again_i2,&again_i0};again_b.native_interfaces={&again_i0,&again_i2};again_parent.native_interfaces={&again_i3,&again_i0};again_i2.native_interfaces={&again_i0};again_i3.native_interfaces={&again_i0};again_i4.native_interfaces={&again_i2,&again_i3,&again_i0};again_other.native_interfaces={&again_i0};again_new_a.native_interfaces={&again_i3,&again_i0,&again_i4,&again_i2};again_new_b.native_interfaces={&again_i4,&again_i2,&again_i0,&again_i3};
  moved={&again_object,&again_base,&again_a,&again_b,&again_parent,&again_i0,&again_i2,&again_i3,&again_i4,&again_other};for(size_t i=0;i<moved.size();++i)f.graph.nodes[i].klass=moved[i];f.graph.nodes[2].replacement=&again_new_a;f.graph.nodes[3].replacement=&again_new_b;plan.old_classes={&again_a,&again_b};plan.new_classes={&again_new_a,&again_new_b};
  forbid_alloc=true;assert(owner.Verify(f.graph,plan));plan.new_classes[0]=&again_new_b;assert(!owner.Verify(f.graph,plan));plan.new_classes[0]=&again_new_a;again_i2.bytes.access_flags|=0x40000;assert(!owner.Verify(f.graph,plan));again_i2.bytes.access_flags&=~0x40000u;assert(owner.Verify(f.graph,plan));forbid_alloc=false;owner.Reset();assert(!owner.Verify(f.graph,plan));
 }
 {
  Fixture f;Preservation owner(metadata);f.graph.nodes[2].proposed_parent=0;f.inputs[0].after=S(f.graph,2,true);assert(!owner.VerifyStaged(f.graph,f.inputs,&error)); // Loses Base despite keeping Object/interfaces.
 }
 {
  Fixture f;Preservation owner(metadata);f.graph.nodes[2].proposed_interfaces={7};f.inputs[0].after=S(f.graph,2,true);assert(!owner.VerifyStaged(f.graph,f.inputs,&error)); // Loses I2 through changed direct interfaces.
 }
 {
  Fixture f;Preservation owner(metadata);f.graph.inputs={2,6};f.graph.nodes[6].affected=true;f.graph.nodes[6].proposed_interfaces={};f.inputs[0].after=S(f.graph,2,true);f.inputs.push_back({f.graph.nodes[6].strong_class,S(f.graph,6,false),S(f.graph,6,true)});assert(!owner.VerifyStaged(f.graph,f.inputs,&error)); // Batch interface ancestor drops I0.
 }
 {
  Fixture f;Preservation owner(metadata);f.graph.inputs={2,1};f.graph.nodes[1].affected=true;f.graph.nodes[1].interfaces={5};f.base.native_interfaces={&f.i0};f.graph.nodes[1].proposed_interfaces={};f.inputs.push_back({f.graph.nodes[1].strong_class,S(f.graph,1,false),S(f.graph,1,true)});assert(!owner.VerifyStaged(f.graph,f.inputs,&error)); // Old parent itself loses interface, not a local edge check.
 }
 {
  Fixture f;Preservation owner(metadata);f.graph.nodes[3].affected=false;assert(!owner.VerifyStaged(f.graph,f.inputs,&error)); // An omitted implicit descendant cannot be granted.
 }
 {
  Fixture f;Preservation owner(metadata);f.inputs[0].after.access|=0x10;assert(!owner.VerifyStaged(f.graph,f.inputs,&error));
 }
 {
  Fixture f;Preservation owner(metadata);f.a.bytes.access_flags|=0x40000;assert(!owner.VerifyStaged(f.graph,f.inputs,&error));
 }
 {
  Fixture f;Preservation owner(metadata);f.graph.nodes[2].descriptor="[Lpkg/A;";assert(!owner.VerifyStaged(f.graph,f.inputs,&error));
 }
 {
  Fixture f;Preservation owner(metadata);f.inputs[0].after.access^=0x200;assert(!owner.VerifyStaged(f.graph,f.inputs,&error));
 }
 {
  Fixture f;Preservation owner(metadata);f.graph.nodes[9].parent=f.graph.nodes[9].proposed_parent=SIZE_MAX;f.other.parent=nullptr;assert(!owner.VerifyStaged(f.graph,f.inputs,&error));
 }
 {
  Fixture f;Preservation owner(metadata);int loader=0;Fake foreign(f.base.descriptor.c_str(),&f.object);foreign.loader=&loader;auto node=N(foreign,0);node.loader=&loader;node.strong_loader=reinterpret_cast<jobject>(&loader);f.graph.nodes.push_back(node);f.graph.nodes[2].proposed_parent=f.graph.nodes.size()-1;f.inputs[0].after=S(f.graph,2,true);assert(!owner.VerifyStaged(f.graph,f.inputs,&error)); // Same parent descriptor in another loader is a lost identity.
 }
 std::puts("Additive native receiver: complete retained ancestry/interface relations, actual linked graph, moving roots and allocation-free final checks PASS; narrowing/dispatch hooks remain unavailable");
}
