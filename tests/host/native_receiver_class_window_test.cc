#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "provider_fixture.inc"
#pragma clang diagnostic pop
#include "platform/common/native_receiver_dex_view.h"
#include "platform/common/exclusive_owned_roots.h"
namespace {
void*window_mark_from=nullptr,*window_mark_to=nullptr;size_t window_marks=0;
void*WindowMark(void*p){++window_marks;return p==window_mark_from?window_mark_to:Mark(p);}
struct WindowHooks:SourceClassHooks{void*FindSymbol(const char*lib,const char*name)override{if(std::strstr(name,"artReadBarrierMark"))return reinterpret_cast<void*>(WindowMark);return SourceClassHooks::FindSymbol(lib,name);}};
struct WindowRequest {
 rc::ClassSource*source;ExplicitMappings*maps;void*object,*a,*b,*old,*replacement,*removed;
 std::vector<std::vector<unsigned char>>*dex;bool visited=false;
};
bool WindowOperation(const exclusive_owned_roots::Context& actual,void*raw){
 auto&r=*static_cast<WindowRequest*>(raw);assert(actual.self==current&&stopped);
 modern_field_metadata::ReadContext context{r.maps,ExplicitMappings::Extent};
 rc::ClassSource::ReadWindow owner(*r.source,context);auto access=owner.Access();rc::Relations relations(access);
 const auto os=method_identity::MappingOsQueries();const auto calls=jni_calls;forbid_provider_allocation=true;
 rc::ClassView facts;void*value=nullptr,*normalized=nullptr;bool related=false;
 assert(access.read(access.owner,r.old,&facts)&&facts.parent==r.removed&&facts.interface_count==2&&facts.direct_count==1);
 auto*live=static_cast<class_metadata::Class17*>(r.old);const auto monitor=live->object.monitor;
 window_mark_from=r.removed;window_mark_to=r.object;live->object.monitor=monitor|(1u<<28);const auto marks=window_marks;
 assert(access.read(access.owner,r.old,&facts)&&facts.parent==r.object&&window_marks>marks);live->object.monitor=monitor;window_mark_from=window_mark_to=nullptr;
 assert(access.direct_at(access.owner,r.old,0,&value)&&value==r.a);
 assert(access.interface_at(access.owner,r.old,1,&value)&&value==r.b);
 assert(relations.Normalize(r.old,r.replacement,&normalized)&&normalized==r.replacement);
 assert(relations.Assignable(r.a,r.old,&related)&&related);assert(relations.Assignable(r.a,r.replacement,&related)&&!related);
 assert(relations.Special(r.a,r.old,&related)&&related);
 const auto table=static_cast<class_metadata::Class17*>(r.old)->iftable;
 static_cast<class_metadata::Class17*>(r.old)->iftable=static_cast<class_metadata::Class17*>(r.replacement)->iftable;
 assert(access.read(access.owner,r.old,&facts)&&facts.interface_count==0);assert(!access.direct_at(access.owner,r.old,0,&value));
 static_cast<class_metadata::Class17*>(r.old)->iftable=table;
 r.maps->denied=r.old;assert(!access.read(access.owner,r.old,&facts));r.maps->denied=nullptr;
 auto*old=static_cast<class_metadata::Class17*>(r.old);const auto original_type=old->dex_type_index;
 old->dex_type_index=99;assert(!access.read(access.owner,r.old,&facts));old->dex_type_index=original_type;
 auto& bytes=(*r.dex)[4];const auto original_magic=bytes[0];bytes[0]='x';assert(!access.read(access.owner,r.old,&facts));bytes[0]=original_magic;
 uint32_t off=0;std::memcpy(&off,bytes.data()+112,4);const auto original_length=bytes[off];bytes[off]=127;assert(!access.read(access.owner,r.old,&facts));bytes[off]=original_length;
 const auto original_flags=old->access_flags;old->access_flags|=0x10;assert(!access.read(access.owner,r.old,&facts));old->access_flags=original_flags;
 const auto first=bytes[516];bytes[516]=99;assert(!access.read(access.owner,r.old,&facts));bytes[516]=first;
 source_old_classes[r.old]=r.replacement;assert(!relations.Normalize(r.old,r.replacement,&normalized));source_old_classes[r.old]=nullptr;
 source_direct_override=r.b;assert(!access.direct_at(access.owner,r.old,0,&value));source_direct_override=nullptr;
 void*saved=context.owner;context.owner=&r;assert(!access.read(access.owner,r.old,&facts));context.owner=saved;
 auto*prior=current;current=foreign_thread.data();assert(!access.read(access.owner,r.old,&facts));current=prior;
 r.maps->reject_all=true;assert(!access.read(access.owner,r.old,&facts));r.maps->reject_all=false;
 assert(method_identity::MappingOsQueries()==os&&jni_calls==calls);forbid_provider_allocation=false;r.visited=true;return true;
}
}
int main(){
 InitJni();InitializeHeap();InterfaceFixtures();Put32(meta_class,0,uint32_t(uintptr_t(meta_class)));
 WindowHooks hooks;std::string error;modern_field_metadata::FieldMetadata metadata;assert(metadata.Bind(hooks,&error));
 std::array<modern_field_metadata::FieldOracle,4> oracle{};for(size_t i=0;i<4;++i)oracle[i]={reinterpret_cast<jobject>(&mirrors[i]),names[i],types[i],i==0?8u:1u};
 struct Cal{modern_field_metadata::FieldMetadata*m;modern_field_metadata::FieldOracle*o;std::string*e;}cal{&metadata,oracle.data(),&error};
 auto calibrate=[](const runnable_jni::Context&ctx,void*raw){auto&c=*static_cast<Cal*>(raw);return c.m->Calibrate(ctx,reinterpret_cast<jobject>(old_class),"LFixture;",c.o,4,c.e);};
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),calibrate,&cal)==runnable_jni::Result::kSuccess);
 std::vector<std::vector<unsigned char>> dex;dex.reserve(8);
 auto make=[&](const char*name,uint32_t flags,void*parent,std::vector<void*>direct,std::vector<const char*>descriptors,std::vector<void*>closure){
  auto*k=new(heap.New(160))class_metadata::Class17{};k->object.klass=uint32_t(uintptr_t(meta_class));k->access_flags=flags;k->status=15u<<28;k->super_class=uint32_t(uintptr_t(parent));k->iftable=uint32_t(uintptr_t(IfTable(closure)));source_class_names[k]=name;source_direct_classes[k]=direct;
  dex.push_back(SourceClassDex(name,flags,descriptors));LinkDex(k,dex.back());return k;
 };
 auto*object=make("Ljava/lang/Object;",1,nullptr,{}, {},{});auto*b=make("LSourceB;",0x601,object,{}, {},{});
 auto*a=make("LSourceA;",0x601,object,{b},{"LSourceB;"},{b});auto*removed=make("LSourceRemoved;",1,object,{}, {},{});
 auto*old=make("LSourceTarget;",1,removed,{a},{"LSourceA;"},{a,b});auto*replacement=make("LSourceTarget;",1,object,{}, {},{});
 source_old_classes[replacement]=old;source_old_classes[old]=nullptr;
 rc::ClassSource source;assert(source.Bind(hooks,metadata,Current,&error));ExplicitMappings maps;maps.Cover(heap.base);maps.Cover(names[0]);for(const auto&bytes:dex)maps.Cover(bytes.data());
 // Independent source-format oracles exercise the same bounded reader used
 // by callbacks, including mapped compact data and a v041 container header.
 for(unsigned mode=0;mode<3;++mode){auto bytes=dex[4];
  if(mode){std::memcpy(bytes.data()+160,bytes.data()+112,8);Put32(bytes.data(),60,160);Put32(bytes.data(),36,mode==1?136:120);
   if(mode==1)std::memcpy(bytes.data(),"cdex001\0",8);
   else{std::memcpy(bytes.data(),"dex\n041\0",8);Put32(bytes.data(),112,bytes.size());Put32(bytes.data(),116,0);Put32(bytes.data(),104,0);Put32(bytes.data(),108,0);}
  }
  evolution::MappedDexMetadata map{bytes.data(),bytes.size(),bytes.data(),bytes.size(),0};rc::BoundedDexView view(map);const char*descriptor=nullptr;uint32_t flags=0,type=99,count=0;
  forbid_provider_allocation=true;assert(view.Initialize()&&view.Identity(&descriptor,&flags,&type)&&!std::strcmp(descriptor,"LSourceTarget;")&&flags==1&&type==0&&view.DirectCount(&count)&&count==1&&view.DirectAt(0,&descriptor)&&!std::strcmp(descriptor,"LSourceA;"));
  auto*sentinel=descriptor;assert(!view.DirectAt(1,&descriptor)&&descriptor==sentinel);forbid_provider_allocation=false;
 }
 WindowRequest request{&source,&maps,object,a,b,old,replacement,removed,&dex};
 exclusive_owned_roots::Service exclusive;assert(exclusive_owned_roots::Service::Bind(hooks,&exclusive,&error));const std::array<jobject,2>roots{reinterpret_cast<jobject>(old),reinterpret_cast<jobject>(replacement)};
 assert(exclusive.With(&env,roots.data(),roots.size(),WindowOperation,&request,&error)&&request.visited);
 // Expired actual mapping owner refuses without consulting OS bounds.
 modern_field_metadata::ReadContext outside{&maps,ExplicitMappings::Extent};rc::ClassSource::ReadWindow expired(source,outside);auto access=expired.Access();rc::ClassView view;const auto queries=method_identity::MappingOsQueries();assert(!access.read(access.owner,old,&view)&&method_identity::MappingOsQueries()==queries);
 std::cout<<"Actual ClassSource read window: live topology/history/full IfTable/direct DEX, changed live table, wrong owner/thread/expired bounds/malformed DEX, no JNI/new/OS PASS; permanent dispatch lifetime not granted\n";
}
