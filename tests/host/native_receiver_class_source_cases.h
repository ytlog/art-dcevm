#include "platform/common/native_receiver_class_source.h"
namespace {
namespace rc=artdcevm::receiver_guard;
std::unordered_map<void*,std::string> source_class_names;
std::unordered_map<void*,void*> source_old_classes;
std::unordered_map<void*,std::vector<void*>> source_direct_classes;
void* source_direct_override=nullptr;
const char* SourceClassDescriptor(void*klass,void*storage){auto i=source_class_names.find(klass);return i==source_class_names.end()?ResolutionDescriptor(klass,storage):i->second.c_str();}
abi::arm64::android11::ReleaseObjectPointer SourceObsoleteClass(void*klass){auto i=source_old_classes.find(klass);return {uintptr_t(i==source_old_classes.end()?nullptr:i->second)};}
abi::arm64::android11::ReleaseObjectPointer SourceDirectClass(void*klass,uint32_t index){const auto&v=source_direct_classes[klass];return {uintptr_t(source_direct_override?source_direct_override:index<v.size()?v[index]:nullptr)};}
abi::arm64::android_common::DirectTypeIndex SourceDirectType(void*,uint32_t index){return {uint16_t(index+1)};}
abi::arm64::android11::ReleaseObjectPointer SourceLookupDirect(void*linker,abi::arm64::android_common::DirectTypeIndex index,abi::arm64::android11::ReleaseObjectPointer owner){assert(linker==&runtime);return SourceDirectClass(reinterpret_cast<void*>(owner.reference),uint32_t(index.index)-1);}
struct SourceClassHooks:ProviderHooks {bool legacy=false;void*FindSymbol(const char*lib,const char*name)override{
 if(!std::strcmp(name,abi::arm64::android_common::kObsoleteClass))return reinterpret_cast<void*>(SourceObsoleteClass);
 if(!std::strcmp(name,abi::arm64::android_common::kDirectInterface))return legacy?nullptr:reinterpret_cast<void*>(SourceDirectClass);
 if(!std::strcmp(name,abi::arm64::android_common::kDirectInterfaceType))return reinterpret_cast<void*>(SourceDirectType);
 if(!std::strcmp(name,abi::arm64::android_common::kLookupDirectInterface))return reinterpret_cast<void*>(SourceLookupDirect);
 if(std::strstr(name,"Class13GetDescriptor"))return reinterpret_cast<void*>(SourceClassDescriptor);
 return ProviderHooks::FindSymbol(lib,name);
}};
std::vector<unsigned char> SourceClassDex(const char*name,uint32_t flags,const std::vector<const char*>&direct){
 std::vector<const char*> names{name};names.insert(names.end(),direct.begin(),direct.end());std::vector<unsigned char>b(1024);
 auto put=[&](size_t at,uint32_t n){std::memcpy(b.data()+at,&n,4);};std::memcpy(b.data(),"dex\n035",8);put(32,b.size());put(36,112);put(40,0x12345678);
 put(56,names.size());put(60,112);put(64,names.size());put(68,144);put(96,1);put(100,192);put(104,768);put(108,256);put(192,0);put(196,flags);put(200,UINT32_MAX);
 size_t at=256;for(size_t i=0;i<names.size();++i){put(112+4*i,at);put(144+4*i,i);const auto len=std::strlen(names[i]);assert(len<128);b[at++]=len;std::memcpy(b.data()+at,names[i],len+1);at+=len+1;}
 if(!direct.empty()){put(204,512);put(512,direct.size());for(size_t i=0;i<direct.size();++i){uint16_t index=i+1;std::memcpy(b.data()+516+2*i,&index,2);}}
 return b;
}
void CheckActualClassSource(){
 const uint32_t old_meta=Read32(meta_class);Put32(meta_class,0,uint32_t(uintptr_t(meta_class)));
 SourceClassHooks hooks;std::string error;modern_field_metadata::FieldMetadata metadata;assert(metadata.Bind(hooks,&error));
 std::array<modern_field_metadata::FieldOracle,4> oracle{};for(size_t i=0;i<4;++i)oracle[i]={reinterpret_cast<jobject>(&mirrors[i]),names[i],types[i],i==0?8u:1u};
 struct Cal{modern_field_metadata::FieldMetadata*m;modern_field_metadata::FieldOracle*o;std::string*e;} cal{&metadata,oracle.data(),&error};
 auto calibrate=[](const runnable_jni::Context&ctx,void*raw){auto&c=*static_cast<Cal*>(raw);return c.m->Calibrate(ctx,reinterpret_cast<jobject>(old_class),"LFixture;",c.o,4,c.e);};
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),calibrate,&cal)==runnable_jni::Result::kSuccess);
 std::vector<std::vector<unsigned char>> dex;dex.reserve(8);
 auto make=[&](const char*name,uint32_t flags,void*parent,std::vector<void*>direct,std::vector<const char*>descriptors,std::vector<void*>closure){
  auto*k=new(heap.New(160))class_metadata::Class17{};k->object.klass=uint32_t(uintptr_t(meta_class));k->access_flags=flags;k->status=15u<<28;k->super_class=uint32_t(uintptr_t(parent));k->iftable=uint32_t(uintptr_t(IfTable(closure)));source_class_names[k]=name;source_direct_classes[k]=direct;
  dex.push_back(SourceClassDex(name,flags,descriptors));LinkDex(k,dex.back());return k;
 };
 auto*object=make("Ljava/lang/Object;",1,nullptr,{}, {},{});
 auto*b=make("LSourceB;",0x601,object,{}, {},{});
 auto*a=make("LSourceA;",0x601,object,{b},{"LSourceB;"},{b});
 auto*removed=make("LSourceRemoved;",1,object,{}, {},{});
 auto*old=make("LSourceTarget;",1,removed,{a},{"LSourceA;"},{a,b});
 auto*replacement=make("LSourceTarget;",1,object,{}, {},{});
 rc::ClassSource source;assert(source.Bind(hooks,metadata,Current,&error));
 hooks.legacy=true;rc::ClassSource legacy;assert(legacy.Bind(hooks,metadata,Current,&error,&runtime));void*legacy_value=nullptr;auto legacy_access=legacy.OrdinaryAccess();assert(legacy_access.direct_at(legacy_access.owner,old,0,&legacy_value)&&legacy_value==a);hooks.legacy=false;
 auto access=source.OrdinaryAccess();rc::Relations relations(access);rc::ClassView facts;
 assert(access.read(access.owner,old,&facts)&&facts.parent==removed&&facts.interface_count==2&&facts.direct_count==1);
 void*actual=nullptr;assert(access.direct_at(access.owner,old,0,&actual)&&actual==a);actual=replacement;assert(!access.direct_at(access.owner,old,1,&actual)&&actual==replacement);
 source_old_classes[replacement]=old;void*selected=nullptr;bool match=false;
 assert(relations.Normalize(old,replacement,&selected)&&selected==replacement);assert(relations.Assignable(selected,replacement,&match)&&match);
 assert(relations.Assignable(removed,replacement,&match)&&!match);assert(relations.Assignable(a,old,&match)&&match);assert(relations.Assignable(a,replacement,&match)&&!match);
 assert(relations.Special(a,old,&match)&&match);assert(relations.Special(a,replacement,&match)&&!match);
 source_old_classes[old]=replacement;assert(!relations.Normalize(old,replacement,&selected));source_old_classes[old]=nullptr;
 old->class_loader=uint32_t(uintptr_t(heap.New(8)));assert(!relations.Normalize(old,replacement,&selected));old->class_loader=0;
 old->access_flags|=0x10;facts.descriptor="sentinel";assert(!access.read(access.owner,old,&facts)&&!std::strcmp(facts.descriptor,"sentinel"));old->access_flags=1;
 source_direct_override=b;actual=replacement;assert(!access.direct_at(access.owner,old,0,&actual)&&actual==replacement);source_direct_override=nullptr;
 auto table=old->iftable;old->iftable=uint32_t(uintptr_t(IfTable({b})));assert(!access.direct_at(access.owner,old,0,&actual));old->iftable=table;
 // Public tuple is checked under the actual decoded strong-root scope; a
 // wrong owner, parent, role or retained history cannot become a shape proof.
 struct Shape{rc::ClassSource*s;void*k,*parent,*owner;bool iface;std::vector<jobject>direct;}shape{&source,old,removed,old,false,{reinterpret_cast<jobject>(a)}};
 auto observe=[](const runnable_jni::Context&ctx,void*raw){auto&v=*static_cast<Shape*>(raw);return v.s->Observe(ctx,reinterpret_cast<jobject>(v.k),reinterpret_cast<jobject>(v.parent),nullptr,v.direct,v.iface,v.owner);};
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old),observe,&shape)==runnable_jni::Result::kSuccess);
 shape.owner=replacement;assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old),observe,&shape)!=runnable_jni::Result::kSuccess);shape.owner=old;
 shape.parent=object;assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old),observe,&shape)!=runnable_jni::Result::kSuccess);shape.parent=removed;
 shape.iface=true;assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old),observe,&shape)!=runnable_jni::Result::kSuccess);shape.iface=false;
 source_old_classes[old]=replacement;assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old),observe,&shape)!=runnable_jni::Result::kSuccess);source_old_classes[old]=nullptr;
 Put32(meta_class,0,old_meta);source_class_names.clear();source_old_classes.clear();source_direct_classes.clear();
 std::cout<<"Actual calibrated ClassSource: named resolved direct/history leaves, complete IfTable plus defining DEX flags/types, actual public strong tuple, lost parent/interface and cyclic/foreign/wrong-role/partial closure refusal PASS (native leaf doubles explicit)\n";
}
}
