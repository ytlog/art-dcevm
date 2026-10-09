// Compile the actual verifier TU so the attestation comparison used by both
// final Verify and BeginCommit is tested, rather than a duplicate test helper.
#include "platform/common/native_schema_guard_verifier.cc"
#include <cassert>
#include <iostream>
using namespace artdcevm;
int main(){
 using namespace native_schema_guard_verifier;
 auto p=[](uintptr_t x){return reinterpret_cast<void*>(x);};
 source_commit::Plan a;a.mirror=p(1);a.new_classes_root=p(2);
 a.old_classes={p(3),p(4)};a.new_classes={p(5),p(6)};
 a.old_instances={p(7)};a.new_instances={p(8)};a.abstract_instances=true;
 a.class_root_origins={{p(9),0},{p(10),2}};
 assert(SamePlan(a,a));
 const auto rejects=[&](auto mutation){auto b=a;mutation(b);assert(!SamePlan(a,b));assert(!SamePlan(b,a));};
 rejects([&](auto& b){b.mirror=p(11);});
 rejects([&](auto& b){b.new_classes_root=p(11);});
 rejects([&](auto& b){b.old_classes[1]=p(11);});
 rejects([&](auto& b){b.new_classes[0]=p(11);});
 rejects([&](auto& b){b.old_instances[0]=p(11);});
 rejects([&](auto& b){b.new_instances[0]=p(11);});
 rejects([](auto& b){b.abstract_instances=false;});
 rejects([&](auto& b){b.class_root_origins[0].array=p(11);});
 rejects([](auto& b){b.class_root_origins[1].index=1;});
 rejects([](auto& b){b.class_root_origins.pop_back();});
 rejects([](auto& b){std::swap(b.old_classes[0],b.old_classes[1]);});
 assert(Ordinary(JVMTI_CLASS_STATUS_PREPARED));
 assert(!Ordinary(JVMTI_CLASS_STATUS_ARRAY));
 assert(!Ordinary(JVMTI_CLASS_STATUS_PRIMITIVE));
 assert(!Ordinary(JVMTI_CLASS_STATUS_ERROR|JVMTI_CLASS_STATUS_PREPARED));
 evolution::Member m{"call","()V",1u,1};assert(Virtual(m));
 m.access=2;assert(!Virtual(m));m.access=8;assert(!Virtual(m));
 m.access=1;m.name="<init>";assert(!Virtual(m));
 const auto parent=[](size_t n){return n>1?n-1:0;};
 const auto same_package=[](size_t a,size_t b){return a==3&&b==1;};
 using namespace loaded_member_guards;
 assert(Accessible(size_t(3),size_t(1),1,parent,same_package));
 assert(!Accessible(size_t(3),size_t(1),2,parent,same_package));
 assert(Accessible(size_t(2),size_t(1),4,parent,same_package));
 assert(!Accessible(size_t(2),size_t(1),4,parent,same_package,true));
 assert(Accessible(size_t(3),size_t(1),0,parent,same_package));
 assert(!Accessible(size_t(2),size_t(1),0,parent,same_package));
 assert(ConcreteBelow(size_t(4),size_t(1),parent,[](size_t n){return n==2;}));
 assert(!ConcreteBelow(size_t(4),size_t(1),parent,[](size_t n){return n==1;}));
 std::cout<<"actual schema verifier: exact Holder attestation, Class kinds, and loaded access/abstract policy passed\n";
}
