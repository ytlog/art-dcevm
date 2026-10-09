// Compile the actual verifier TU so the attestation comparison used by both
// final Verify and BeginCommit is tested, rather than a duplicate test helper.
#include "platform/common/native_schema_guard_verifier.cc"
#include <cassert>
#include <iostream>
using namespace artdcevm;
int main(){
 // Test the same normalization helper used by the actual verifier TU.
 // The comparator source must be an attested native arena entry; arbitrary
 // mapped addresses and recursive proxy origins cannot grant provenance.
 for(const auto layout:{method_metadata::kAndroid11,method_metadata::kAndroid12Onward}){
  alignas(8) unsigned char raw[40]{},prototype[40]{},copied[40]{},foreign[40]{};
  for(const auto name:{"<init>","toString","interfaceCall"}){
   std::map<void*,proxy_method_signature::Declaration> arena{
    {raw,{name,"()V",true}},{prototype,{name,"()V",false}},{copied,{name,"()V",false}}};
   const auto set=[&](void* origin){__atomic_store_n(reinterpret_cast<uintptr_t*>(raw+layout.data),reinterpret_cast<uintptr_t>(origin),__ATOMIC_RELAXED);};
   proxy_method_signature::Witness witness;
   set(prototype);assert(proxy_method_signature::Resolve(layout,raw,name,"()V",arena,&witness));
   assert(witness.source==prototype&&witness.Unchanged());
   set(copied);assert(!witness.Unchanged());assert(proxy_method_signature::Resolve(layout,raw,name,"()V",arena,&witness));
   assert(witness.source==copied&&witness.Unchanged());
   set(nullptr);assert(!proxy_method_signature::Resolve(layout,raw,name,"()V",arena,&witness));
   set(foreign);assert(!proxy_method_signature::Resolve(layout,raw,name,"()V",arena,&witness));
   set(raw);assert(!proxy_method_signature::Resolve(layout,raw,name,"()V",arena,&witness));
   set(prototype+1);assert(!proxy_method_signature::Resolve(layout,raw,name,"()V",arena,&witness));
   set(prototype);arena[prototype].descriptor="(I)V";assert(!proxy_method_signature::Resolve(layout,raw,name,"()V",arena,&witness));
   arena[prototype].descriptor="()V";arena[prototype].name="wrong";assert(!proxy_method_signature::Resolve(layout,raw,name,"()V",arena,&witness));
   arena[prototype].name=name;arena[prototype].proxy=true;assert(!proxy_method_signature::Resolve(layout,raw,name,"()V",arena,&witness));
   arena[prototype].proxy=false;arena[raw].proxy=false;assert(!proxy_method_signature::Resolve(layout,raw,name,"()V",arena,&witness));
   arena[raw].proxy=true;assert(!proxy_method_signature::Resolve(layout,raw+1,name,"()V",arena,&witness));
   assert(!proxy_method_signature::Resolve(layout,raw,name,"()V",arena,nullptr));
   auto invalid=layout;invalid.data=invalid.size;assert(!proxy_method_signature::Resolve(invalid,raw,name,"()V",arena,&witness));
  }
 }
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
 // The real verifier comparison projects raw ArtMethod flags exactly as
 // fixed-source JVMTI, without applying its transformations to public flags.
 const uint32_t raw_abstract_dispatch=0x401u|0x100u|0x80000u;
 method_metadata::Method11 native11{};native11.access_flags=raw_abstract_dispatch;
 method_metadata::Method12 native12{};native12.access_flags=0x20101u;
 assert(MethodWindowProof(true,true,method_metadata::ReadAccessFlags(&native11),0x401u));
 assert(MethodWindowProof(true,true,method_metadata::ReadAccessFlags(&native12),0x121u));
 assert(MethodWindowProof(true,true,raw_abstract_dispatch,0x401u));
 assert(MethodWindowProof(true,true,0x121u,0x101u));
 assert(MethodWindowProof(true,true,0x20101u,0x121u));
 assert(!MethodWindowProof(true,true,0x20101u,0x101u));
 assert(!MethodWindowProof(true,true,raw_abstract_dispatch,0x501u));
 assert(!MethodWindowProof(false,true,raw_abstract_dispatch,0x401u));
 assert(!MethodWindowProof(true,false,raw_abstract_dispatch,0x401u));
 assert(!MethodWindowProof(true,true,raw_abstract_dispatch^1u,0x401u));
 evolution::ClassSchema owner_schema;owner_schema.descriptor="LExpected;";
 evolution::Member method_schema{"invoke","(I)V",0x401u,UINT32_MAX};
 assert(!MethodWindowFailure(&diagnostic,owner_schema,method_schema,true,"LForeign;",raw_abstract_dispatch));
 assert(diagnostic.find("class=LExpected; method=invoke(I)V mapped=1 owner=LForeign;")!=std::string::npos);
 assert(diagnostic.find("raw=0x00080501 projected=0x00000401 public=0x00000401")!=std::string::npos);
 assert(!MethodWindowFailure(&diagnostic,owner_schema,method_schema,false,"<unmapped>",0));
 assert(diagnostic.find("mapped=0 owner=<unmapped>")!=std::string::npos);
 owner_schema.descriptor=unicode_descriptor;method_schema.name=std::string(95,'x')+"\xe4\xb8\xad";
 assert(!MethodWindowFailure(&diagnostic,owner_schema,method_schema,true,nullptr,1));
 assert(diagnostic.size()<768&&diagnostic.find("\xe4") == std::string::npos);
 assert(!ClassIdentityFailure(&diagnostic,"Ljava/lang/reflect/Proxy$0;","proxy",true,false,false,false,"Array/proxy descriptor needs unavailable platform string storage"));
 assert(diagnostic.find("class=Ljava/lang/reflect/Proxy$0; kind=proxy mapped=1 identity=0 descriptorMatch=0 loaderMatch=0")!=std::string::npos);
 assert(diagnostic.find("nativeError=Array/proxy descriptor needs unavailable platform string storage")!=std::string::npos);
 assert(!ClassIdentityFailure(&diagnostic,"LExpected;","ordinary",true,true,true,false,""));
 assert(diagnostic.find("identity=1 descriptorMatch=1 loaderMatch=0")!=std::string::npos);
 assert(!ClassIdentityFailure(&diagnostic,"LExpected;","unmapped",false,false,false,false,long_descriptor.c_str()));
 assert(diagnostic.size()<768);
 assert(CallerClass(JVMTI_CLASS_STATUS_PREPARED));
 assert(CallerClass(JVMTI_CLASS_STATUS_ARRAY));
 assert(CallerClass(JVMTI_CLASS_STATUS_PRIMITIVE));
 assert(!CallerClass(JVMTI_CLASS_STATUS_ERROR|JVMTI_CLASS_STATUS_PREPARED));
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
