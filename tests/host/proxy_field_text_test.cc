#include "api/hook_engine.h"
#include "platform/common/field_copy.h"
#include "platform/common/runnable_jni_bridge.h"
#include "platform/common/field_array_view.h"
#include <string>
#define private public
#include "platform/common/modern_field_metadata.h"
#undef private
#include "platform/common/dex_mapping_abi.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/abi/arm64/android11/class.h"
#include "platform/abi/arm64/android11/dex_cache.h"
#include "platform/abi/arm64/android17/class.h"
#include <sys/mman.h>
#include <cassert>
#include <cstring>
#include <vector>
#include <iostream>
using namespace artdcevm;
namespace artdcevm::native_interface_graph {
bool Calibrate(const runnable_jni::Context&,jobject,const modern_field_metadata::FieldMetadata&,std::string*){std::abort();}
}
namespace {
int named_calls=0;const char* named_name="interfaces";const char* named_type="[Ljava/lang/Class;";
void* Root(void* field){uint32_t v;std::memcpy(&v,field,4);return reinterpret_cast<void*>(uintptr_t(v));}
void* Mark(void* o){return o;}
void* Leaf(void* o){return o;}
const char* ClassDescriptor(void*,void* storage){assert(!storage);return "LFixture;";}
const char* Name(void*){++named_calls;return named_name;}
const char* Type(void*){++named_calls;return named_type;}
struct Hooks:HookEngine {
 unsigned named=0;
 bool Initialize(std::string*)override{return true;}
 void* Install(void*,void*,void**,std::string*)override{std::abort();}
 bool Remove(void*,std::string*)override{std::abort();}
 void* FindSymbol(const char*,const char* n)override{
  if(std::strstr(n,"GetArtField"))return reinterpret_cast<void*>(Leaf);
  if(std::strstr(n,"ArtField7GetName"))return named&1?reinterpret_cast<void*>(Name):nullptr;
  if(std::strstr(n,"ArtField17GetTypeDescriptor"))return named&2?reinterpret_cast<void*>(Type):nullptr;
  if(std::strstr(n,"Class13GetDescriptor"))return reinterpret_cast<void*>(ClassDescriptor);
  if(std::strstr(n,"artReadBarrierForRootSlow"))return reinterpret_cast<void*>(Root);
  if(std::strstr(n,"artReadBarrierMark"))return reinterpret_cast<void*>(Mark);
  return nullptr;
 }
};
void Put(void* p,size_t at,uint32_t v){std::memcpy(static_cast<char*>(p)+at,&v,4);}
std::vector<unsigned char> Dex(){
 std::vector<unsigned char> v(0x70);std::memcpy(v.data(),"dex\n035\0",8);
 auto put=[&](size_t at,uint32_t x){std::memcpy(v.data()+at,&x,4);};
 put(36,0x70);put(40,0x12345678);
 put(56,3);put(60,0x70);v.resize(0x70+12);
 put(64,2);put(68,uint32_t(v.size()));size_t types=v.size();v.resize(v.size()+8);put(types,0);put(types+4,1);
 put(80,1);put(84,uint32_t(v.size()));size_t fields=v.size();v.resize(v.size()+8);v[fields+2]=1;put(fields+4,2);
 for(size_t i=0;i<3;++i){const char* s=i==0?"LFixture;":i==1?"I":"ordinary";put(0x70+4*i,uint32_t(v.size()));v.push_back(uint8_t(std::strlen(s)));v.insert(v.end(),s,s+std::strlen(s)+1);}
 put(32,uint32_t(v.size()));return v;
}
}
struct Snapshot {
 std::vector<std::pair<const void*,size_t>> ranges;
 const void* deny=nullptr;const void* truncate=nullptr;size_t calls=0;
 static bool Extent(void* owner,const void* address,size_t* size){
  auto& snapshot=*static_cast<Snapshot*>(owner);++snapshot.calls;
  if(address==snapshot.deny)return false;
  for(const auto& range:snapshot.ranges){const auto a=uintptr_t(address),b=uintptr_t(range.first);
   if(a>=b&&a-b<range.second){*size=address==snapshot.truncate?1:range.second-(a-b);return true;}}
  return false;
 }
};
int main(){
 using namespace modern_field_metadata;
 auto* heap=static_cast<char*>(mmap(reinterpret_cast<void*>(0x20000000),4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0));
 assert(heap!=MAP_FAILED&&uintptr_t(heap)+4096<=UINT32_MAX);
 auto* klass=heap;auto* field=reinterpret_cast<field_array::Field*>(heap+256);
 field->declaring_class=uint32_t(uintptr_t(klass));field->access_flags=8;
 std::string error;const char *name=nullptr,*type=nullptr;
 for(auto model:{Model::kSplit,Model::kMerged}){
  const auto flag=model==Model::kSplit?offsetof(abi::arm64::android11::Class11,access_flags):offsetof(abi::arm64::android17::Class17,access_flags);
  std::memset(klass,0,160);Put(klass,flag,0x40000);
  for(unsigned availability=0;availability<4;++availability){
   Hooks hooks;hooks.named=availability;FieldMetadata metadata;assert(metadata.Bind(hooks,&error));
   for(uint32_t index=0;index<2;++index){field->dex_field_index=index;field->access_flags=8;named_name=index?"throws":"interfaces";named_type=index?"[[Ljava/lang/Class;":"[Ljava/lang/Class;";
    assert(metadata.Text(field,model,&name,&type,&error));assert(!std::strcmp(name,named_name)&&!std::strcmp(type,named_type));
   }
   field->dex_field_index=2;assert(!metadata.Text(field,model,&name,&type,&error));
   field->dex_field_index=UINT32_MAX;assert(!metadata.Text(field,model,&name,&type,&error));
   field->dex_field_index=0;field->access_flags=1;assert(!metadata.Text(field,model,&name,&type,&error));field->access_flags=8;
   if(availability==3){named_name="bad";assert(!metadata.Text(field,model,&name,&type,&error));continue;}
   Put(klass,flag,0);assert(!metadata.Text(field,model,&name,&type,&error));Put(klass,flag,0x40000);
   field->declaring_class=0;assert(!metadata.Text(field,model,&name,&type,&error));field->declaring_class=uint32_t(uintptr_t(klass));
  }
 }
 // Ordinary split fields still resolve their actual FieldId, not proxy text.
 Hooks hooks;FieldMetadata metadata;assert(metadata.Bind(hooks,&error));std::memset(klass,0,160);
 auto bytes=Dex();auto* prefix=new(heap+512)dex_mapping_abi::Prefix{nullptr,bytes.data(),bytes.size(),bytes.data(),bytes.size()};
 auto* cache=heap+768;const uint64_t file=uintptr_t(prefix);std::memcpy(cache+offsetof(abi::arm64::android11::LegacyCache,dex_file),&file,8);
 Put(klass,offsetof(abi::arm64::android11::Class11,dex_cache),uint32_t(uintptr_t(cache)));
 field->dex_field_index=0;assert(metadata.Text(field,Model::kSplit,&name,&type,&error));assert(!std::strcmp(name,"ordinary")&&!std::strcmp(type,"I"));
 field->dex_field_index=1;assert(!metadata.Text(field,Model::kSplit,&name,&type,&error));
 assert(!metadata.Text(field,Model::kMerged,&name,&type,&error));
 // The same fixed reader uses an explicit snapshot without any /proc/maps.
 Snapshot snapshot{{{heap,4096},{bytes.data(),bytes.size()}}};
 ReadContext context{&snapshot,Snapshot::Extent};metadata.calibrated_=true;
 field->dex_field_index=0;field_copy::CopyFieldView identity;
 const auto queries=method_identity::MappingOsQueries();
 for(size_t i=0;i<100;++i){assert(metadata.FieldIdentity(field,&identity,&error,&context));assert(!std::strcmp(identity.name,"ordinary")&&!std::strcmp(identity.descriptor,"I"));}
 assert(snapshot.calls>=600&&method_identity::MappingOsQueries()==queries);
 // Every source range retains its checks; a denied snapshot cannot fall back.
 for(const void* target:{static_cast<void*>(field),static_cast<void*>(klass),static_cast<void*>(cache),static_cast<void*>(prefix),static_cast<void*>(bytes.data())}){
  snapshot.deny=target;assert(!metadata.FieldIdentity(field,&identity,&error,&context));snapshot.deny=nullptr;
  snapshot.truncate=target;assert(!metadata.FieldIdentity(field,&identity,&error,&context));snapshot.truncate=nullptr;
 }
 ReadContext absent{&snapshot,nullptr};assert(!metadata.FieldIdentity(field,&identity,&error,&absent));
 Snapshot different{{{heap,4096}}};ReadContext other{&different,Snapshot::Extent};assert(!metadata.FieldIdentity(field,&identity,&error,&other));
 // Reader failure and DEX identity/type mismatch cannot publish a field view.
 field->dex_field_index=1;assert(!metadata.FieldIdentity(field,&identity,&error,&context));field->dex_field_index=0;
 // Split field-array prefix/length bounds use the same explicit view.
 auto* allocation=heap+1024;uint32_t count=1;std::memcpy(allocation,&count,4);
 auto* entry=new(allocation+field_array::kDataOffset)field_array::Field{uint32_t(uintptr_t(klass)),1,0,8};
 const uint64_t raw_array=uintptr_t(allocation);std::memcpy(klass+offsetof(abi::arm64::android11::Class11,ifields),&raw_array,8);
 field_array::View fields;assert(metadata.Fields(klass,false,&fields,&error,&context)&&fields.selected_count==1);
 snapshot.deny=allocation;assert(!metadata.Fields(klass,false,&fields,&error,&context));snapshot.deny=nullptr;
 snapshot.truncate=allocation;assert(!metadata.Fields(klass,false,&fields,&error,&context));snapshot.truncate=nullptr;
 (void)entry;
 assert(method_identity::MappingOsQueries()==queries);
 assert(metadata.FieldIdentity(field,&identity,&error));assert(method_identity::MappingOsQueries()>queries);
 munmap(heap,4096);
 std::cout<<"Actual FieldMetadata Text: named/partial/missing leaves; split/merged proxy static0/1, invalid index/flags/root, ordinary DEX identity PASS\n";
}
