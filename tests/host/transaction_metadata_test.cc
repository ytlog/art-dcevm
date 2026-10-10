#include "platform/android13/transaction_metadata.h"
#include "platform/android13/validation_metadata.h"
#include "platform/symbols/android13/source_symbols.h"
#include "platform/abi/arm64/android13/validation_metadata_abi.h"
#include "platform/abi/arm64/android13/art_method_abi.h"
#include "platform/abi/arm64/android13/redefinition_art_abi.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <sys/mman.h>
namespace g=artdcevm::android13;using Class=g::validation_abi::ClassPrefix;using Method=g::art_method_abi::ArtMethod;
namespace {unsigned char* arena;size_t used;
template<class T>T* New(){used=(used+alignof(T)-1)&~(alignof(T)-1);auto* p=new(arena+used) T{};used+=sizeof(T);return p;}
uint32_t Ref(void* p){assert(uintptr_t(p)<=UINT32_MAX);return uint32_t(uintptr_t(p));}
void* Root(void* slot){return reinterpret_cast<void*>(uintptr_t(*static_cast<uint32_t*>(slot)));}
struct Array {g::validation_abi::ObjectPrefix object;int32_t length;uint32_t values[13*129];};
struct PointerArray {g::validation_abi::ObjectPrefix object;int32_t length;alignas(8) uint64_t values[3];};
struct Methods {uint64_t count;Method values[2];};
Array* Arr(Class* type,int32_t count){auto* a=New<Array>();a->object.klass=Ref(type);a->length=count;return a;}
uintptr_t Mirror(void* definition){return uintptr_t(definition);}
class Hooks:public artdcevm::HookEngine {public:bool missing_mirror=false;bool Initialize(std::string*)override{return true;}void* Install(void*,void*,void**,std::string*)override{return nullptr;}bool Remove(void*,std::string*)override{return true;}void* FindSymbol(const char*,const char* name)override{if(!std::strcmp(name,::artdcevm::symbols::android13::kGetMirrorClass))return missing_mirror?nullptr:reinterpret_cast<void*>(Mirror);return std::strcmp(name,"artReadBarrierForRootSlow")==0?reinterpret_cast<void*>(Root):nullptr;}};
const char* names[]={"libart.so","libopenjdkjvmti.so","libdexfile.so","libc++.so"};
const char* ids[]={"14fc77b24db0b351b919c97009dc86bf","2fe21c45581b9c8b0519b71cbde32938","e6aaffcef8ef63baf438267cda44d400","1f426797e505c9b841f55cc49d32b3f4"};
}
namespace artdcevm {LoadedModule FindLoadedModule(const char* n){for(size_t i=0;i<4;++i)if(!std::strcmp(names[i],n))return {std::string("/apex/")+n,ids[i]};return {};}}
namespace artdcevm::android13 {void* ReadHeapReference(void* p,size_t at,bool){return reinterpret_cast<void*>(uintptr_t(*reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(p)+at)));}}
int main(){
 arena=static_cast<unsigned char*>(mmap(reinterpret_cast<void*>(0x24000000),1<<20,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0));assert(arena!=MAP_FAILED&&uintptr_t(arena)+(1<<20)<=UINT32_MAX);
 Hooks hooks;g::DeviceProfile profile;g::TransactionMetadataReader reader;std::string error;assert(!reader.Configure(hooks,profile,&error));assert(g::MatchDeviceProfile(&profile,&error));assert(reader.Configure(hooks,profile,&error));
 hooks.missing_mirror=true;assert(!g::ConfigureValidationMetadata(hooks,&error));hooks.missing_mirror=false;
 assert(g::ConfigureValidationMetadata(hooks,&error));
 auto* metadata_class=New<Class>();auto* metadata_cache=New<g::validation_abi::DexCachePrefix>();
 unsigned char dex_bytes[0x80]{},compact_data[256]{};
 g::validation_abi::DexMappingPrefix dex_mapping{nullptr,dex_bytes,sizeof(dex_bytes),compact_data,sizeof(compact_data)};
 metadata_class->dex_cache=Ref(metadata_cache);metadata_class->dex_class_def_index=7;metadata_class->access_flags=0x201;
 metadata_cache->dex_file=uintptr_t(&dex_mapping);g::ValidationMetadata metadata;
 assert(g::ReadValidationMetadata(metadata_class,&metadata));
 assert(metadata.dex.begin==dex_bytes&&metadata.dex.data_begin==compact_data&&metadata.dex.data_size==256&&metadata.dex.class_def_index==7&&metadata.access_flags==0x201);
 assert(!g::ReadValidationMetadata(metadata_class,nullptr));
 dex_mapping.size=0x6f;assert(!g::ReadValidationMetadata(metadata_class,&metadata));dex_mapping.size=sizeof(dex_bytes);
 dex_mapping.data_begin=reinterpret_cast<unsigned char*>(UINTPTR_MAX-8);assert(!g::ReadValidationMetadata(metadata_class,&metadata));dex_mapping.data_begin=compact_data;
 metadata_cache->dex_file=0;assert(!g::ReadValidationMetadata(metadata_class,&metadata));metadata_cache->dex_file=uintptr_t(&dex_mapping);
 assert(g::ReadValidationMetadata(metadata_class,&metadata));
 auto* meta=New<Class>();meta->object.klass=Ref(meta);meta->primitive_type=2<<16;
 auto* object=New<Class>();object->object.klass=Ref(meta);object->primitive_type=2<<16;
 auto* array_type=New<Class>();array_type->component_type=Ref(object);array_type->object.klass=Ref(meta);
 auto* class_array_type=New<Class>();class_array_type->component_type=Ref(meta);class_array_type->object.klass=Ref(meta);
 auto* roots=Arr(array_type,13*129);uint32_t array_handle=Ref(roots);
 g::redefinition_abi::ClassRedefinition defs[129]{};g::hierarchy_allocation_abi::Vector owners{reinterpret_cast<unsigned char*>(defs),reinterpret_cast<unsigned char*>(defs+129),reinterpret_cast<unsigned char*>(defs+129)};
 uint64_t words[3]{};words[1]=2;g::allocation_abi::Holder holder{&array_handle,&owners,{words,129,3},{words,129,3},{words,129,3}};
 g::TransactionHolderView holder_view;assert(reader.ReadHolder(&holder,&holder_view,&error)&&holder_view.count==129);
 g::allocation_abi::Iterator iterator{65,&holder};g::TransactionRootView view;
 roots->values[65*13+4]=Ref(object);assert(reader.ReadRoots(&iterator,&view,&error)&&view.index==65&&view.owner==&defs[65]&&view.initialized&&view.actually_structural&&view.initial_structural&&view.mirror==object);
 iterator.index=64;assert(reader.ReadRoots(&iterator,&view,&error)&&!view.initialized);iterator.index=129;assert(!reader.ReadRoots(&iterator,&view,&error));iterator.index=-1;assert(!reader.ReadRoots(&iterator,&view,&error));iterator.index=65;
 holder.initialized.capacity=2;assert(!reader.ReadHolder(&holder,&holder_view,&error));holder.initialized.capacity=3;
 holder.initial_structural.size=128;assert(!reader.ReadHolder(&holder,&holder_view,&error));holder.initial_structural.size=129;
 owners.capacity=owners.end-1;assert(!reader.ReadHolder(&holder,&holder_view,&error));owners.capacity=owners.end;
 owners.end=owners.begin-1;assert(!reader.ReadHolder(&holder,&holder_view,&error));owners.end=owners.capacity;
 roots->length--;assert(!reader.ReadHolder(&holder,&holder_view,&error));roots->length++;
 auto* prim=New<Class>();prim->primitive_type=6;array_type->component_type=Ref(prim);assert(!reader.ReadHolder(&holder,&holder_view,&error));array_type->component_type=Ref(object);
 auto* empty=Arr(array_type,0);array_handle=Ref(empty);owners={};holder.initialized={};holder.actually_structural={};holder.initial_structural={};assert(reader.ReadHolder(&holder,&holder_view,&error)&&!holder_view.count);iterator.index=0;assert(!reader.ReadRoots(&iterator,&view,&error));
 auto* cache_view=New<g::hierarchy_allocation_abi::Cache>();cache_view->prefix.dex_file=123;g::hierarchy_allocation_abi::Cache cache_copy;
 assert(reader.ReadCache(cache_view,&cache_copy,&error));cache_view->num_types=1;assert(!reader.ReadCache(cache_view,&cache_copy,&error));alignas(8) uint64_t type_pair=0;cache_view->types=uintptr_t(&type_pair);assert(reader.ReadCache(cache_view,&cache_copy,&error));cache_view->num_types=1025;assert(!reader.ReadCache(cache_view,&cache_copy,&error));cache_view->num_types=1;
 cache_view->fields=UINTPTR_MAX&~uintptr_t{15};cache_view->num_fields=1;
 assert(!reader.ReadCache(cache_view,&cache_copy,&error));cache_view->fields=0;cache_view->num_fields=0;
 cache_view->methods=UINTPTR_MAX&~uintptr_t{15};cache_view->num_methods=1;
 assert(!reader.ReadCache(cache_view,&cache_copy,&error));cache_view->methods=0;cache_view->num_methods=0;
 cache_view->types=UINTPTR_MAX&~uintptr_t{7};assert(!reader.ReadCache(cache_view,&cache_copy,&error));
 cache_view->types=uintptr_t(&type_pair);assert(reader.ReadCache(cache_view,&cache_copy,&error));
 auto* klass=New<Class>();klass->object.klass=Ref(meta);klass->primitive_type=2<<16;auto* iface=New<Class>();iface->object.klass=Ref(meta);iface->access_flags=0x200;
 auto* methods=New<Methods>();methods->count=2;methods->values[0].declaring_class=Ref(klass);methods->values[1].declaring_class=Ref(iface);methods->values[1].access_flags=0x1000000;klass->methods=uintptr_t(methods);klass->virtual_methods_offset=1;klass->copied_methods_offset=1;
 g::MethodStorageView storage;assert(reader.ReadMethods(klass,&storage,&error)&&storage.count==2&&storage.direct_count==1);
 g::MethodStorageView sentinel{methods,17,19,23};
 for(void* bad:{reinterpret_cast<void*>(UINTPTR_MAX-3),static_cast<void*>(reinterpret_cast<unsigned char*>(klass)+1)}) {
  assert(!reader.ReadMethods(bad,&sentinel,&error));
  assert(sentinel.array==methods && sentinel.count==17 && sentinel.direct_count==19 && sentinel.copied_start==23);
 }
 klass->methods=UINTPTR_MAX&~uintptr_t{7};assert(!reader.ReadMethods(klass,&sentinel,&error));
 klass->methods=uintptr_t(methods)+4;assert(!reader.ReadMethods(klass,&sentinel,&error));
 klass->methods=0;klass->virtual_methods_offset=0;klass->copied_methods_offset=0;
 assert(reader.ReadMethods(klass,&storage,&error) && !storage.array && !storage.count);
 klass->methods=uintptr_t(methods);klass->virtual_methods_offset=1;klass->copied_methods_offset=1;
 methods->values[1].access_flags|=0x80000000;assert(!reader.ReadMethods(klass,&storage,&error));methods->values[1].access_flags=0x1000000;iface->access_flags=0;assert(!reader.ReadMethods(klass,&storage,&error));iface->access_flags=0x200;
 klass->copied_methods_offset=3;assert(!reader.ReadMethods(klass,&storage,&error));klass->copied_methods_offset=1;klass->virtual_methods_offset=2;assert(!reader.ReadMethods(klass,&storage,&error));klass->virtual_methods_offset=1;
 klass->access_flags=0x400;auto* long_array_type=New<Class>();long_array_type->component_type=Ref(prim);auto* table=New<PointerArray>();table->object.klass=Ref(long_array_type);table->length=2;table->values[0]=uintptr_t(&methods->values[0]);table->values[1]=uintptr_t(&methods->values[1]);klass->vtable=Ref(table);
 bool abstract;assert(reader.ReadAbstractVTable(klass,&abstract,&error)&&!abstract);methods->values[1].access_flags|=0x400;assert(reader.ReadAbstractVTable(klass,&abstract,&error)&&abstract);table->values[0]=0;assert(!reader.ReadAbstractVTable(klass,&abstract,&error));table->values[0]=uintptr_t(&methods->values[0]);table->length=-1;assert(!reader.ReadAbstractVTable(klass,&abstract,&error));table->length=2;klass->component_type=Ref(object);assert(!reader.ReadAbstractVTable(klass,&abstract,&error));klass->component_type=0;
 auto* next=New<Class>();next->object.klass=Ref(meta);next->primitive_type=2<<16;auto* old_instance=New<Class>();old_instance->object.klass=Ref(klass);auto* new_instance=New<Class>();new_instance->object.klass=Ref(next);
 auto* olds=Arr(class_array_type,1);olds->values[0]=Ref(klass);auto* news=Arr(class_array_type,1);news->values[0]=Ref(next);auto* instances=Arr(array_type,1);instances->values[0]=Ref(old_instance);auto* replacements=Arr(array_type,1);replacements->values[0]=Ref(new_instance);
 view.mirror=klass;view.old_classes=olds;view.new_classes=news;view.old_instances=instances;view.new_instances=replacements;assert(reader.CheckCommitTypes(view,&error));
 new_instance->object.klass=Ref(object);assert(!reader.CheckCommitTypes(view,&error));new_instance->object.klass=Ref(next);old_instance->object.klass=Ref(object);assert(!reader.CheckCommitTypes(view,&error));old_instance->object.klass=Ref(klass);
 klass->status=2u<<28;assert(!reader.CheckCommitTypes(view,&error));klass->status=0;news->length=0;assert(!reader.CheckCommitTypes(view,&error));news->length=1;next->object.klass=Ref(object);assert(!reader.CheckCommitTypes(view,&error));next->object.klass=Ref(meta);assert(reader.CheckCommitTypes(view,&error));
 munmap(arena,1<<20);std::cout<<"holder ranges, word-capacity/64-bit boundaries, rooted slots, copied-default storage, abstract external table and commit type correspondence: passed\n";
}
