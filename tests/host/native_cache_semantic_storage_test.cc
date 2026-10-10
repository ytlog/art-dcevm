#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "native_commit_fixture.inc"
#pragma clang diagnostic pop
#include "platform/common/native_cache_semantic.h"
#include <iostream>
namespace art {
class ReflectionSourceInfo {
 public:virtual ~ReflectionSourceInfo()=default;virtual void Describe(std::ostream& os)const{os<<type_;}
 explicit ReflectionSourceInfo(uint32_t t):type_(t){}
 private:const uint32_t type_;
};
class DexCacheSourceInfo final:public ReflectionSourceInfo {
 public:DexCacheSourceInfo(uint32_t t,size_t i,void* c):ReflectionSourceInfo(t),index_(i),cache_(c){}
 void Describe(std::ostream& os)const override{ReflectionSourceInfo::Describe(os);os<<index_<<cache_;}
 private:size_t index_;void* cache_;
};
}

namespace {
std::array<type_cache::Pair,1024> type_pairs{};std::array<uint32_t,4> type_full{};
void* cache_object=nullptr;void* native_method=reinterpret_cast<void*>(uintptr_t{0x123456789});
void* reflected_cache_method=native_method;unsigned visit_calls=0,original_writes=0;
void CacheVisit(void* object,art::ReflectiveValueVisitor* visitor){
 assert(object==cache_object);++visit_calls;
 if(reflected_cache_method){art::DexCacheSourceInfo info(8,3,object);
  auto next=visitor->VisitMethod(static_cast<art::ArtMethod*>(reflected_cache_method),info);
  if(next!=reflected_cache_method){reflected_cache_method=next;++original_writes;}}
}
void* CacheType(void* object,uint32_t i){assert(object==cache_object);return i==1?reinterpret_cast<void*>(uintptr_t(type_full[1])):nullptr;}
void* CacheMethod(void* object,uint32_t i){assert(object==cache_object);return i==3?reflected_cache_method:nullptr;}
class SemanticHooks final:public Hooks {
 public:void* FindSymbol(const char* lib,const char* name)override{
  if(std::strstr(name,"DexCache22VisitReflectiveTargets"))return reinterpret_cast<void*>(CacheVisit);
  if(std::strstr(name,"DexCache21GetResolvedTypesEntry"))return reinterpret_cast<void*>(CacheType);
  if(std::strstr(name,"DexCache23GetResolvedMethodsEntry"))return reinterpret_cast<void*>(CacheMethod);
  return Hooks::FindSymbol(lib,name);
 }
};
jlong ReadLong(JNIEnv*,jobject owner,jfieldID id){Jni();uint64_t bits;std::memcpy(&bits,reinterpret_cast<unsigned char*>(owner)+reinterpret_cast<abi::arm64::Field*>(id)->offset,8);return static_cast<jlong>(bits);}
void LongAt(void* object,uint32_t offset,uint64_t bits){std::memcpy(static_cast<unsigned char*>(object)+offset,&bits,8);}
bool Deny(void*,const void*,size_t*){return false;}
}
int main(){
 InitJni();InitializeHeap();SemanticHooks hooks;std::string error;
 modern_field_metadata::FieldMetadata original;assert(original.Bind(hooks,&error));
 std::array<modern_field_metadata::FieldOracle,4> oracle{};for(size_t i=0;i<4;++i)oracle[i]={reinterpret_cast<jobject>(&mirrors[i]),names[i],types[i],i==0?8u:1u};
 struct Cal {modern_field_metadata::FieldMetadata* m;const modern_field_metadata::FieldOracle* o;std::string* e;} cal{&original,oracle.data(),&error};
 auto run=[](const runnable_jni::Context& c,void* p){auto& x=*static_cast<Cal*>(p);return x.m->Calibrate(c,reinterpret_cast<jobject>(old_class),"LFixture;",x.o,4,x.e);};
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),run,&cal)==runnable_jni::Result::kSuccess);
 modern_field_storage::Service storage;assert(storage.Calibrate(hooks,&env,reinterpret_cast<jobject>(old_class),reinterpret_cast<jobject>(old_instance),original,oracle.data(),4,&error));
 // Complete independent public/native declarations use arbitrary field offsets;
 // no DexCache 96/112-byte layout appears in the production semantic reader.
 names[1]="resolvedTypes";names[2]="resolvedTypesArray";types[1]=types[2]="J";
 old_fields[1]->access_flags=old_fields[2]->access_flags=2;
 old_fields[1]->offset=8;old_fields[2]->offset=16;old_fields[3]->offset=24;
 cache_object=heap.New(48);Put32(cache_object,0,uint32_t(uintptr_t(old_class)));
 LongAt(cache_object,8,uintptr_t(type_pairs.data()));LongAt(cache_object,16,uintptr_t(type_full.data()));
 type_pairs[1]={uint32_t(uintptr_t(old_class)),1};type_full[1]=uint32_t(uintptr_t(old_class));
 for(size_t i=1;i<3;++i){oracle[i].name=names[i];oracle[i].descriptor="J";oracle[i].modifiers=2;}
 modern_field_metadata::FieldMetadata metadata;assert(metadata.Bind(hooks,&error));cal.m=&metadata;
 if(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),run,&cal)!=runnable_jni::Result::kSuccess){std::cerr<<error<<"\n";return 2;}
 table.GetLongField=ReadLong;
 const auto public_pairs=uint64_t(env.GetLongField(reinterpret_cast<jobject>(cache_object),reinterpret_cast<jfieldID>(old_fields[1])));
 const auto public_full=uint64_t(env.GetLongField(reinterpret_cast<jobject>(cache_object),reinterpret_cast<jfieldID>(old_fields[2])));
 native_cache_semantic::Service service;assert(service.Bind(hooks,&error));
 assert(!service.PrepareStorage(metadata,storage,cache_object,new_class,old_fields[1],old_fields[2],public_pairs,public_full,nullptr,&error));
 assert(!service.PrepareStorage(metadata,storage,cache_object,old_class,old_fields[1],old_fields[1],public_pairs,public_full,nullptr,&error));
 assert(!service.PrepareStorage(metadata,storage,cache_object,old_class,old_fields[1],old_fields[2],public_pairs+8,public_full,nullptr,&error));
 assert(!service.StoragePrepared());
 assert(service.PrepareStorage(metadata,storage,cache_object,old_class,old_fields[1],old_fields[2],public_pairs,public_full,nullptr,&error));
 type_cache::View view;assert(service.Types(cache_object,4,&view,nullptr)&&view.pairs==type_pairs.data()&&view.full==type_full.data());
 const auto jni= jni_calls;
 assert(service.Calibrate(cache_object,4,4,4,1,old_class,2,old_fields[2],3,native_method,true,nullptr,&error));
 // A denied explicit final mapping never falls back to an OS mapping lookup.
 modern_field_metadata::ReadContext denied{&service,Deny};
 assert(!service.Types(cache_object,4,&view,&denied));
 old_fields[2]->declaring_class=uint32_t(uintptr_t(new_class));assert(!service.Types(cache_object,4,&view,nullptr));old_fields[2]->declaring_class=uint32_t(uintptr_t(old_class));
 old_fields[2]->access_flags|=8;assert(!service.Types(cache_object,4,&view,nullptr));old_fields[2]->access_flags&=~8u;
 assert(service.Clear(cache_object,4,4)&&service.Cleared(cache_object,4,4)&&original_writes==1);
 assert(jni_calls==jni&&type_pairs[1].object==uint32_t(uintptr_t(old_class))&&type_full[1]==uint32_t(uintptr_t(old_class)));
 // Incoming private cache has the exact owning Class but an independent payload.
 void* incoming=heap.New(48);Put32(incoming,0,uint32_t(uintptr_t(old_class)));
 std::array<type_cache::Pair,1024> incoming_pairs{};LongAt(incoming,8,uintptr_t(incoming_pairs.data()));LongAt(incoming,16,0);
 assert(service.Types(incoming,4,&view,nullptr)&&view.pairs==incoming_pairs.data()&&!view.full);
 Put32(incoming,0,uint32_t(uintptr_t(new_class)));assert(!service.Types(incoming,4,&view,nullptr));
 std::cout<<"semantic production source field reader + original public long oracle + native visitor PASS\n";
}
