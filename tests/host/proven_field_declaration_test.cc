#include "platform/common/modern_field_metadata.h"
#include "platform/common/runnable_executor.h"
#include "platform/common/art_method_metadata.h"
#include "platform/abi/arm64/android11/class.h"
#include "platform/abi/arm64/android17/class.h"
#include <sys/mman.h>
#include <cassert>
#include <cstring>
#include <map>
#include <vector>
#include <iostream>
using namespace artdcevm;
namespace {
unsigned char* heap=nullptr;size_t used=0;int self,vm;JNIEnv env{};
void* Allocate(size_t n){used=(used+15)&~size_t{15};assert(used+n<65536);void* p=heap+used;used+=n;std::memset(p,0,n);return p;}
void Put64(void* p,size_t offset,uint64_t value){std::memcpy(static_cast<char*>(p)+offset,&value,8);}
void Put32(void* p,size_t offset,uint32_t value){std::memcpy(static_cast<char*>(p)+offset,&value,4);}
struct Mirror{void* field;};
std::map<void*,std::pair<std::string,std::string>> identities;
void* bootstrap=nullptr;
void* watched_method=nullptr;void* moved_class=nullptr;unsigned method_roots=0,marks=0;
void* Root(void* p){if(p==watched_method){++method_roots;return moved_class;}uint32_t n;std::memcpy(&n,p,4);return reinterpret_cast<void*>(uintptr_t(n));}
void* Mark(void* p){++marks;return p;}
void* Leaf(void* p){return static_cast<Mirror*>(p)->field;}
const char* Name(void* p){return identities.at(p).first.c_str();}
const char* Type(void* p){return identities.at(p).second.c_str();}
const char* Descriptor(void* p,void*){return p==bootstrap?"LBootstrap;":"LTarget;";}
bool Implements(void*,void*){return false;}
struct Hooks:HookEngine {
 bool Initialize(std::string*)override{return true;}
 void* Install(void*,void*,void**,std::string*)override{std::abort();}
 bool Remove(void*,std::string*)override{std::abort();}
 void* FindSymbol(const char*,const char* name)override{
#define SYMBOL(text,fn) if(std::strstr(name,text))return reinterpret_cast<void*>(fn)
  SYMBOL("GetArtField",Leaf);SYMBOL("ArtField7GetName",Name);SYMBOL("ArtField17GetTypeDescriptor",Type);
  SYMBOL("Class13GetDescriptor",Descriptor);SYMBOL("artReadBarrierMark",Mark);SYMBOL("artReadBarrierForRootSlow",Root);SYMBOL("Class10Implements",Implements);
#undef SYMBOL
  return nullptr;
 }
};
struct Definition {void* klass;std::vector<Mirror*> mirrors;std::vector<modern_field_metadata::FieldOracle> fields;std::vector<void*> native;};
Definition Build(bool split,const std::vector<bool>& statics){
 Definition d;d.klass=Allocate(160);
 Put32(d.klass,split?offsetof(abi::arm64::android11::Class11,status):offsetof(abi::arm64::android17::Class17,status),15u<<28);
 size_t static_count=0;for(bool s:statics)static_count+=s;
 void* first=nullptr;void* second=nullptr;
 if(split){if(static_count){first=Allocate(4+static_count*sizeof(field_array::Field));Put32(first,0,uint32_t(static_count));}
  if(statics.size()-static_count){second=Allocate(4+(statics.size()-static_count)*sizeof(field_array::Field));Put32(second,0,uint32_t(statics.size()-static_count));}
  Put64(d.klass,offsetof(abi::arm64::android11::Class11,sfields),uintptr_t(first));Put64(d.klass,offsetof(abi::arm64::android11::Class11,ifields),uintptr_t(second));
 }else{if(!statics.empty()){first=Allocate(4+statics.size()*sizeof(field_array::Field));Put32(first,0,uint32_t(statics.size()));}Put64(d.klass,offsetof(abi::arm64::android17::Class17,fields),uintptr_t(first));}
 size_t si=0,ii=0;
 for(size_t i=0;i<statics.size();++i){bool s=statics[i];void* array=split?(s?first:second):first;size_t index=split?(s?si++:ii++):i;
  auto* f=new(static_cast<char*>(array)+4+index*sizeof(field_array::Field))field_array::Field{uint32_t(uintptr_t(d.klass)),s?9u:1u,uint32_t(7+i),uint32_t(8+i*4)};
  identities[f]={"field"+std::to_string(i),"I"};auto* mirror=new(Allocate(sizeof(Mirror)))Mirror{f};d.mirrors.push_back(mirror);d.native.push_back(f);
  const auto& text=identities.at(f);d.fields.push_back({reinterpret_cast<jobject>(mirror),text.first.c_str(),text.second.c_str(),s?9u:1u,uint32_t(7+i)});
 }
 return d;
}
}
namespace artdcevm::runnable_executor {
bool DecodeStrongGlobal(const runnable_jni::Context& c,jobject root,void** out){if(c.self!=&self||c.env!=&env||c.vm!=&vm||!root||!out)return false;*out=root;return true;}
}
namespace artdcevm::native_interface_graph {
bool Calibrate(const runnable_jni::Context&,jobject,const modern_field_metadata::FieldMetadata&,std::string*){std::abort();}
}
int main(){
 heap=static_cast<unsigned char*>(mmap(reinterpret_cast<void*>(0x20000000),65536,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0));assert(heap!=MAP_FAILED&&uintptr_t(heap)+65536<=UINT32_MAX);
 JNINativeInterface_ table{};table.ExceptionCheck=[](JNIEnv*){return jboolean{false};};env.functions=&table;
 Hooks hooks;const runnable_jni::Context context{&self,&env,&vm};std::string error;
 for(bool split:{true,false}){
  auto independent=Build(split,{true,false});bootstrap=independent.klass;
  modern_field_metadata::FieldMetadata metadata;assert(metadata.Bind(hooks,&error));
  assert(metadata.Calibrate(context,reinterpret_cast<jobject>(bootstrap),"LBootstrap;",independent.fields.data(),independent.fields.size(),&error));
  assert(metadata.StorageModel()==(split?modern_field_metadata::Model::kSplit:modern_field_metadata::Model::kMerged));
  // The same declared GcRoot slot exists in both actual source Method layouts.
  // Named root reading observes relocation even without Baker mirror marking;
  // access_flags bit28 must never be interpreted as Object::monitor_.
  for(size_t size:{sizeof(method_metadata::Method11),sizeof(method_metadata::Method12)}) {
   void* method=Allocate(size);Put32(method,0,uint32_t(uintptr_t(bootstrap)));
   watched_method=method;method_roots=0;
   void* relocated=Allocate(160);
   for(uint32_t flags:{1u,0x10000001u}) {
    Put32(method,4,flags);moved_class=bootstrap;marks=0;
    assert(metadata.MethodDeclaringClass(method)==bootstrap&&marks==0);
    moved_class=relocated;
    assert(metadata.MethodDeclaringClass(method)==relocated&&marks==0);
    // The wrong mirror path misses the move and may invoke a mark based on a
    // method flag. This negative control detects reverting the real reader.
    assert(metadata.Reference(method,0)==bootstrap);
    assert(marks==unsigned((flags>>28)&1u));
    uint32_t unchanged=0;std::memcpy(&unchanged,static_cast<char*>(method)+4,4);
    assert(unchanged==flags);
   }
   assert(method_roots==4);watched_method=nullptr;moved_class=nullptr;
  }

  for(const auto& roles:std::vector<std::vector<bool>>{{},{false},{true},{false,false},{true,true},{false,true}}){
   auto target=Build(split,roles);
   const auto verify=[&](){return metadata.VerifyProvenDeclaration(context,reinterpret_cast<jobject>(target.klass),"LTarget;",target.fields.data(),target.fields.size(),&error);};
   assert(verify());modern_field_metadata::FieldMetadata unknown;assert(unknown.Bind(hooks,&error));assert(!unknown.VerifyProvenDeclaration(context,reinterpret_cast<jobject>(target.klass),"LTarget;",target.fields.data(),target.fields.size(),&error));
   assert(!metadata.VerifyProvenDeclaration(context,reinterpret_cast<jobject>(target.klass),"LWrong;",target.fields.data(),target.fields.size(),&error));
   auto foreign=context;foreign.self=&vm;assert(!metadata.VerifyProvenDeclaration(foreign,reinterpret_cast<jobject>(target.klass),"LTarget;",target.fields.data(),target.fields.size(),&error));
   if(target.fields.empty())continue;
   auto* field=static_cast<field_array::Field*>(target.native[0]);const auto saved=*field;
   field->declaring_class=uint32_t(uintptr_t(bootstrap));assert(!verify());*field=saved;
   ++field->dex_field_index;assert(!verify());*field=saved;
   field->access_flags^=8;assert(!verify());*field=saved;
   auto oracle=target.fields[0];target.fields[0].name="wrong";assert(!verify());target.fields[0]=oracle;
   target.fields[0].descriptor="J";assert(!verify());target.fields[0]=oracle;
   target.fields[0].dex_index=UINT32_MAX;assert(!verify());target.fields[0]=oracle;
   assert(!metadata.VerifyProvenDeclaration(context,reinterpret_cast<jobject>(target.klass),"LTarget;",target.fields.data(),target.fields.size()-1,&error));
   target.mirrors[0]->field=independent.native[0];assert(!verify());target.mirrors[0]->field=field;
   auto* alien=new(Allocate(sizeof(field_array::Field)))field_array::Field(saved);
   identities[alien]=identities.at(field);target.mirrors[0]->field=alien;assert(!verify());target.mirrors[0]->field=field;
   void* array=nullptr;const auto& source=split?field_array::kSplit:field_array::kMerged17;
   assert(field_array::ReadPointer(source,target.klass,160,roles[0],&array)&&array);
   uint32_t original_count=0;std::memcpy(&original_count,array,4);Put32(array,0,0);assert(!verify());Put32(array,0,original_count);
   if(target.fields.size()>1){auto second=target.fields[1];target.fields[1]=target.fields[0];assert(!verify());target.fields[1]=second;}
   assert(verify());
  }
 }
 munmap(heap,65536);std::cout<<"Actual proven FieldMetadata: independently calibrated split/merged; zero/one/all-static/all-instance/mixed complete arrays, wrong Class/root/index/flags/name/type/missing/duplicate/foreign/unproved-family rejection; Method11/12 moving GcRoot and access_flags bit28 mirror negative control PASS\n";
}
