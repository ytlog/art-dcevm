#include <cstdlib>
#include <new>
static bool no_allocation=false;
void* operator new(std::size_t n){if(no_allocation)std::abort();if(auto* p=std::malloc(n))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete[](void* p)noexcept{::operator delete(p);}
// Exercise the actual service's Seal/virtual RefreshPlan implementation. The
// fixture seeds prepared state only; it grants no listener or ART permission.
#include "platform/common/native_final_field_service.cc"
#include <cassert>
#include <iostream>
using namespace artdcevm;
namespace {
struct Strong {void* current=nullptr;bool readable=true;};
int vm,old_class,new_class,moved_old,moved_new,instance,new_instance,moved_instance,moved_new_instance,mirror,moved_mirror,array,moved_array,origin,moved_origin;
bool operation_owned=true;
bool Decode(void*,jobject raw,void** out){auto& root=*reinterpret_cast<Strong*>(raw);if(!root.readable)return false;*out=root.current;return true;}
}
// Unused permission-consumer boundaries deny capability in this fixture.
namespace artdcevm::retirement {
bool FieldEntryGuardsOwn(const RetiredFieldRegistry&){return false;}
bool FieldVarHandleGuardsReady(const RetiredFieldRegistry&){return false;}
bool MethodHandleFieldGuardsReady(const RetiredFieldRegistry&){return false;}
}
namespace artdcevm::redefinition_gate {bool Owned(JavaVM* actual){return operation_owned&&actual==reinterpret_cast<JavaVM*>(&vm);}}
namespace artdcevm::final_field_service {
struct TestPeer {
 static void Prepare(Service& service,const source_commit::Plan& plan,Strong& old,Strong& next){
  auto& state=*service.state_;state.bindings.native.vm=reinterpret_cast<JavaVM*>(&vm);state.bindings.native.decode_global=Decode;
  Service::State::Public root;root.root=reinterpret_cast<jclass>(&old);root.replacement=reinterpret_cast<jclass>(&next);
  state.staged.push_back(std::move(root));state.rooted_pairs={0};state.expected=plan;state.prepared=true;
 }
 static bool Sealed(const Service& service){return service.state_->sealed;}
 static const source_commit::Plan& Expected(const Service& service){return service.state_->expected;}
 static void Committed(Service& service){service.state_->committed=true;}
};
}
int main(){
 using final_field_service::TestPeer;
 source_commit::Plan old;old.mirror=&mirror;old.new_classes_root=&array;old.old_classes={&old_class};old.new_classes={&new_class};old.old_instances={&instance};old.new_instances={&new_instance};old.class_root_origins={{&origin,2}};
 Strong old_root{&old_class},new_root{&new_class};final_field_service::Service service({});TestPeer::Prepare(service,old,old_root,new_root);
 const native_schema_guard_verifier::FinalFields& consumer=service;
 assert(service.Seal(old,nullptr)&&consumer.VerifyAliases(old,nullptr));
 source_commit::Plan moved=old;moved.mirror=&moved_mirror;moved.new_classes_root=&moved_array;moved.old_classes[0]=&moved_old;moved.new_classes[0]=&moved_new;moved.old_instances[0]=&moved_instance;moved.new_instances[0]=&moved_new_instance;moved.class_root_origins[0].array=&moved_origin;
 // The source verifier supplies a separately root-resolved, full frozen Plan.
 // These public strong Class roots move independently of its root storage.
 old_root.current=&moved_old;new_root.current=&moved_new;
 assert(!consumer.VerifyAliases(moved,nullptr)&&!consumer.VerifyAliases(old,nullptr));
 no_allocation=true;
 assert(consumer.RefreshPlan(moved,nullptr)&&TestPeer::Sealed(service)&&consumer.VerifyAliases(moved,nullptr));
 assert(source_commit::SameIdentity(moved,TestPeer::Expected(service)));
 no_allocation=false;
 auto reject=[&](auto mutate){auto wrong=moved;mutate(wrong);no_allocation=true;
  assert(!consumer.RefreshPlan(wrong,nullptr)&&!TestPeer::Sealed(service));
  // Failure must revoke the previous seal, even though its old snapshot is
  // still present. A fresh correct same-prepared refresh can recover.
  assert(!consumer.VerifyAliases(moved,nullptr));
  assert(consumer.RefreshPlan(moved,nullptr)&&consumer.VerifyAliases(moved,nullptr));no_allocation=false;};
 reject([](auto& p){p.old_classes.clear();});reject([](auto& p){p.new_classes.clear();});
 reject([](auto& p){p.old_instances.clear();});reject([](auto& p){p.new_instances.clear();});
 reject([](auto& p){p.class_root_origins.clear();});reject([](auto& p){++p.class_root_origins[0].index;});
 reject([](auto& p){p.abstract_instances=!p.abstract_instances;});reject([](auto& p){p.old_classes[0]=&old_class;});reject([](auto& p){p.new_classes[0]=&new_class;});
 operation_owned=false;no_allocation=true;assert(!consumer.RefreshPlan(moved,nullptr)&&!consumer.VerifyAliases(moved,nullptr));operation_owned=true;
 assert(consumer.RefreshPlan(moved,nullptr));old_root.readable=false;assert(!consumer.RefreshPlan(moved,nullptr)&&!consumer.VerifyAliases(moved,nullptr));old_root.readable=true;assert(consumer.RefreshPlan(moved,nullptr));
 TestPeer::Committed(service);assert(!consumer.RefreshPlan(moved,nullptr));no_allocation=false;
 std::cout<<"Actual permission Service: virtual same-prepared root refresh, moving strong Classes/full Plan, stale seal revocation, no allocation and refused mismatches PASS\n";
}
