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
 // Exercise the actual rejection formatter used by CheckWindow. The new
 // identity must reach callers without granting any staged-graph permission.
 std::string diagnostic;
 assert(!ClassWindowFailure(&diagnostic,"Class was defined after caller staging; retry complete schema operation","Lforeign/SameName;",JVMTI_CLASS_STATUS_VERIFIED|JVMTI_CLASS_STATUS_PREPARED));
 assert(diagnostic=="Class was defined after caller staging; retry complete schema operation [descriptor=Lforeign/SameName; status=0x00000003]");
 assert(!ClassWindowFailure(&diagnostic,"Unprepared Class appeared before native schema sealing",nullptr,0));
 assert(diagnostic=="Unprepared Class appeared before native schema sealing [descriptor=<unavailable> status=0x00000000]");
 std::string long_descriptor(5000,'x');
 assert(!ClassWindowFailure(&diagnostic,"late",long_descriptor.c_str(),JVMTI_CLASS_STATUS_INITIALIZED));
 assert(diagnostic.size()<768&&diagnostic.find("status=0x00000004")!=std::string::npos);
 // A three-byte modified-UTF-8 code unit crosses the 384-byte cap.
 const std::string unicode_descriptor=std::string(383,'x')+"\xe4\xb8\xad"+";";
 assert(!ClassWindowFailure(&diagnostic,"late",unicode_descriptor.c_str(),3));
 assert(diagnostic=="late [descriptor="+std::string(383,'x')+" status=0x00000003]");
 assert(!ClassWindowFailure(nullptr,"late","Lforeign/Other;",3));
 assert(Ordinary(JVMTI_CLASS_STATUS_PREPARED));
 assert(!Ordinary(JVMTI_CLASS_STATUS_ARRAY));
 assert(!Ordinary(JVMTI_CLASS_STATUS_PRIMITIVE));
 assert(!Ordinary(JVMTI_CLASS_STATUS_ERROR|JVMTI_CLASS_STATUS_PREPARED));
 // Interface reflection's null parent and ART's bootstrap Object field are
 // separate fixed semantics. The public hierarchy edge remains absent.
 std::vector<Node> nodes(3);nodes[0].schema.descriptor="Ljava/lang/Object;";
 nodes[1].schema.descriptor="LInterface;";nodes[1].schema.access=0x201;
 nodes[2].schema.descriptor="LConcrete;";nodes[2].schema.access=1;
 Snapshot roots;roots.classes={p(20),p(21),p(22)};roots.loaders={nullptr,p(30),p(30)};roots.parents={absent,absent,0};
 void* superclass=p(99);assert(NativeSuperclass(nodes,roots,1,&superclass)&&superclass==p(20));assert(roots.parents[1]==absent);
 assert(NativeSuperclass(nodes,roots,0,&superclass)&&!superclass);
 assert(NativeSuperclass(nodes,roots,2,&superclass)&&superclass==p(20));
 roots.parents[1]=0;assert(!NativeSuperclass(nodes,roots,1,&superclass));roots.parents[1]=absent;
 roots.loaders[0]=p(30);assert(!NativeSuperclass(nodes,roots,1,&superclass));roots.loaders[0]=nullptr;
 nodes[0].schema.descriptor="LOther;";assert(!NativeSuperclass(nodes,roots,1,&superclass));nodes[0].schema.descriptor="Ljava/lang/Object;";
 nodes[2].schema.descriptor="Ljava/lang/Object;";roots.loaders[2]=nullptr;assert(!NativeSuperclass(nodes,roots,1,&superclass));nodes[2].schema.descriptor="LConcrete;";roots.loaders[2]=p(30);
 roots.parents[2]=20;assert(!NativeSuperclass(nodes,roots,2,&superclass));roots.parents[2]=0;
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
 std::cout<<"actual schema verifier: exact Holder attestation, Class kinds, exact public/native interface superclass, and loaded access/abstract policy passed\n";
}
