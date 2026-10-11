#pragma once
// Standalone ART/JNI protocol model for graph tests. Public declarations and
// source layouts are independent inputs; production owners grant all authority.
#include "platform/common/modern_field_metadata.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/common/class_metadata_abi.h"
#include "platform/common/executable_mirror_calibration.h"
#include "platform/common/runnable_executor.h"
#include "platform/common/dex_mapping_abi.h"
#include "platform/abi/arm64/android11/dex_cache.h"
#include "platform/abi/arm64/android11/jni_object_decode.h"
#include "platform/abi/arm64/android11/jni_current_thread.h"
#include "platform/abi/common/suspend_all.h"
#include "platform/abi/arm64/common/closure.h"
#include "platform/common/native_interface_graph.h"
#include <sys/mman.h>
#include <unistd.h>
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <new>
using namespace artdcevm;
bool forbid_provider_allocation=false;
void* operator new(size_t n){if(forbid_provider_allocation)std::abort();if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
namespace {
struct Heap {
 unsigned char* base;size_t used=0;
 Heap(){base=static_cast<unsigned char*>(mmap(reinterpret_cast<void*>(0x20000000),1<<20,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0));assert(base!=MAP_FAILED&&uintptr_t(base)+(1<<20)<=UINT32_MAX);}
 ~Heap(){munmap(base,1<<20);}
 void* New(size_t bytes){used=(used+15)&~size_t{15};assert(used+bytes<(1<<20));auto* p=base+used;used+=bytes;return p;}
} heap;
bool HeapExtent(void* owner,const void* address,size_t* extent){
 if(owner!=&heap||!extent)return false;const uintptr_t p=uintptr_t(address),start=uintptr_t(heap.base);
 if(p<start||p-start>=heap.used)return false;*extent=heap.used-size_t(p-start);return true;
}
uint32_t Read32(const void* p,size_t at=0){uint32_t n;std::memcpy(&n,static_cast<const char*>(p)+at,4);return n;}
void Put32(void* p,size_t at,uint32_t n){std::memcpy(static_cast<char*>(p)+at,&n,4);}
alignas(4096) std::array<unsigned char,512> thread{},foreign_thread{};
void* current=thread.data();int thread_list,mutex,linker;JavaVM runtime{};
int stopped=0,locked=0,jni_calls=0,binding_value=0;
class_metadata::Class17* old_class=nullptr;class_metadata::Class17* new_class=nullptr;
void* parent_class=nullptr;void* meta_class=nullptr;void* class_ext=nullptr;
std::array<abi::arm64::Field*,4> old_fields{},new_fields{};
std::array<const char*,4> names{"number","value","left","right"};
std::array<const char*,4> types{"I","I","Ljava/lang/Object;","Ljava/lang/Object;"};
struct Mirror {void* field;};std::array<Mirror,4> mirrors{};
abi::arm64::android12::Method12* old_method=nullptr;
void* Root(void* p){return reinterpret_cast<void*>(uintptr_t(Read32(p)));}
void* Mark(void* p){return p;}
const char* FieldName(void* field){for(size_t i=0;i<4;++i)if(old_fields[i]==field||new_fields[i]==field)return names[i];return nullptr;}
const char* FieldType(void* field){for(size_t i=0;i<4;++i)if(old_fields[i]==field||new_fields[i]==field)return types[i];return nullptr;}
void* FieldLeaf(void* mirror){assert(!stopped&&Read32(current)==0);return static_cast<Mirror*>(mirror)->field;}
const char* Descriptor(void* klass,void*){return klass==parent_class?"Ljava/lang/Object;":"LFixture;";}
void* Current(){return current;}
abi::arm64::android11::ReleaseObjectPointer Decode(const void*,jobject p){return {uintptr_t(p)};}
void Jni(){assert(!stopped);++jni_calls;}
jboolean Exception(JNIEnv*){Jni();return false;}
jthrowable ExceptionOccurred(JNIEnv*){Jni();return nullptr;}
void VoidException(JNIEnv*){Jni();}
jobjectRefType RefType(JNIEnv*,jobject){Jni();return JNIGlobalRefType;}
jobject Global(JNIEnv*,jobject p){Jni();return p;}
void Delete(JNIEnv*,jobject){Jni();}
jboolean SameObject(JNIEnv*,jobject a,jobject b){Jni();return a==b;}
jclass ObjectClass(JNIEnv*,jobject o){Jni();return reinterpret_cast<jclass>(Root(o));}
jclass Find(JNIEnv*,const char* name){Jni();return reinterpret_cast<jclass>(std::strcmp(name,"java/lang/Class")==0?meta_class:parent_class);}
int superclass_id,loader_id,gc_id,hash_id;
jmethodID MethodId(JNIEnv*,jclass,const char* name,const char*){Jni();return reinterpret_cast<jmethodID>(std::strcmp(name,"getSuperclass")==0?&superclass_id:&loader_id);}
jmethodID StaticMethodId(JNIEnv*,jclass,const char* name,const char*){Jni();return reinterpret_cast<jmethodID>(std::strcmp(name,"gc")==0?&gc_id:&hash_id);}
jobject ObjectMethod(JNIEnv*,jobject o,jmethodID id,va_list){Jni();assert(o);return id==reinterpret_cast<jmethodID>(&superclass_id)?reinterpret_cast<jobject>(parent_class):nullptr;}
jfieldID FieldId(JNIEnv*,jobject mirror){Jni();const auto state=Read32(current);Put32(current,0,0);void* field=FieldLeaf(mirror);Put32(current,0,state);return reinterpret_cast<jfieldID>(field);}
JNINativeInterface_ table{};JNIEnv env{&table};
void InitJni(){
 table.ExceptionCheck=Exception;table.ExceptionOccurred=ExceptionOccurred;table.ExceptionClear=VoidException;
 table.GetObjectRefType=RefType;table.NewGlobalRef=Global;table.DeleteGlobalRef=Delete;table.DeleteLocalRef=Delete;
 table.IsSameObject=SameObject;table.GetObjectClass=ObjectClass;table.FindClass=Find;
 table.GetMethodID=MethodId;table.GetStaticMethodID=StaticMethodId;table.CallObjectMethodV=ObjectMethod;table.FromReflectedField=FieldId;
}
void InitializeClass(class_metadata::Class17* c){c->object.klass=uint32_t(uintptr_t(meta_class));c->super_class=uint32_t(uintptr_t(parent_class));c->status=15u<<28;c->ext_data=uint32_t(uintptr_t(class_ext));}
void MakeFields(class_metadata::Class17* klass,std::array<abi::arm64::Field*,4>& output){auto* raw=static_cast<unsigned char*>(heap.New(4+4*16));Put32(raw,0,4);klass->fields=uintptr_t(raw);for(size_t i=0;i<4;++i){output[i]=reinterpret_cast<abi::arm64::Field*>(raw+4+16*i);*output[i]={uint32_t(uintptr_t(klass)),i==0?8u:1u,uint32_t(i),i==0?128u:uint32_t(4+4*i)};}}
void InitializeHeap(){
 meta_class=heap.New(160);parent_class=heap.New(160);class_ext=heap.New(16);
 old_class=static_cast<class_metadata::Class17*>(heap.New(160));new_class=static_cast<class_metadata::Class17*>(heap.New(160));InitializeClass(old_class);InitializeClass(new_class);
 auto*parent=static_cast<class_metadata::Class17*>(parent_class);parent->object.klass=uint32_t(uintptr_t(meta_class));parent->status=15u<<28;
 MakeFields(old_class,old_fields);MakeFields(new_class,new_fields);for(size_t i=0;i<4;++i)mirrors[i]={old_fields[i]};
 Put32(thread.data(),0,92u<<24);
}
// Independent public transitive closure: ArrayList -> DirectA -> SuperB,
// plus inherited Parent -> InheritedC. Native IfTable pairs are separate arrays.
void* public_array_class=nullptr;void* public_list=nullptr;void* public_parent=nullptr;
void* interface_a=nullptr;void* interface_b=nullptr;void* interface_c=nullptr;
int interfaces_id;
std::unordered_map<void*,void*> direct_arrays,public_parents;
void* Array(const std::vector<void*>& values){void* array=heap.New(12+values.size()*4);Put32(array,0,uint32_t(uintptr_t(public_array_class)));Put32(array,8,uint32_t(values.size()));for(size_t i=0;i<values.size();++i)Put32(array,12+4*i,uint32_t(uintptr_t(values[i])));return array;}
void* IfTable(const std::vector<void*>& values){std::vector<void*> pairs;for(auto v:values){pairs.push_back(v);pairs.push_back(nullptr);}return Array(pairs);}
jclass InterfaceFind(JNIEnv* e,const char* name){if(!std::strcmp(name,"java/util/ArrayList")){Jni();return reinterpret_cast<jclass>(public_list);}return Find(e,name);}
jmethodID InterfaceMethod(JNIEnv* e,jclass owner,const char* name,const char* signature){if(!std::strcmp(name,"getInterfaces")){Jni();return reinterpret_cast<jmethodID>(&interfaces_id);}return MethodId(e,owner,name,signature);}
jobject InterfaceObject(JNIEnv* e,jobject o,jmethodID id,va_list args){
 if(id==reinterpret_cast<jmethodID>(&interfaces_id)){Jni();return reinterpret_cast<jobject>(direct_arrays.at(o));}
 if(id==reinterpret_cast<jmethodID>(&superclass_id)){Jni();auto i=public_parents.find(o);return i==public_parents.end()?nullptr:reinterpret_cast<jobject>(i->second);}
 return ObjectMethod(e,o,id,args);
}
jsize InterfaceLength(JNIEnv*,jarray array){Jni();return jsize(Read32(array,8));}
jobject InterfaceElement(JNIEnv*,jobjectArray array,jsize index){Jni();assert(index>=0&&index<jsize(Read32(array,8)));return reinterpret_cast<jobject>(uintptr_t(Read32(array,12+size_t(index)*4)));}
void InterfaceFixtures(){
 public_array_class=heap.New(sizeof(class_metadata::Class17));auto* array_class=static_cast<class_metadata::Class17*>(public_array_class);array_class->component_type=uint32_t(uintptr_t(parent_class));
 public_list=heap.New(sizeof(class_metadata::Class17));public_parent=heap.New(sizeof(class_metadata::Class17));
 interface_a=heap.New(sizeof(class_metadata::Class17));interface_b=heap.New(sizeof(class_metadata::Class17));interface_c=heap.New(sizeof(class_metadata::Class17));
 for(auto v:{public_array_class,public_list,public_parent,interface_a,interface_b,interface_c})static_cast<class_metadata::Class17*>(v)->object.klass=uint32_t(uintptr_t(meta_class));
 for(auto v:{interface_a,interface_b,interface_c})static_cast<class_metadata::Class17*>(v)->access_flags=0x200;
 void* empty=Array({});
 direct_arrays[public_list]=Array({interface_a});direct_arrays[public_parent]=Array({interface_c});direct_arrays[interface_a]=Array({interface_b});
 for(auto v:{interface_b,interface_c,parent_class,static_cast<void*>(old_class),static_cast<void*>(new_class)})direct_arrays[v]=empty;
 public_parents[public_list]=public_parent;public_parents[public_parent]=parent_class;public_parents[old_class]=parent_class;public_parents[new_class]=parent_class;
 for(auto v:{public_list,public_parent,interface_a,interface_b,interface_c,parent_class,static_cast<void*>(old_class),static_cast<void*>(new_class)})static_cast<class_metadata::Class17*>(v)->iftable=uint32_t(uintptr_t(IfTable({})));
 static_cast<class_metadata::Class17*>(public_list)->iftable=uint32_t(uintptr_t(IfTable({interface_a,interface_b,interface_c})));
 static_cast<class_metadata::Class17*>(public_parent)->iftable=uint32_t(uintptr_t(IfTable({interface_c})));
 table.FindClass=InterfaceFind;table.GetMethodID=InterfaceMethod;table.CallObjectMethodV=InterfaceObject;table.GetArrayLength=InterfaceLength;table.GetObjectArrayElement=InterfaceElement;
}
struct ExplicitMappings {
 struct Range {uintptr_t begin,end;};std::vector<Range> ranges;
 const void* denied=nullptr;bool reject_all=false;size_t calls=0;
 void Cover(const void* address){size_t extent=0;assert(method_identity::ReadableMappingExtent(address,&extent));
  const auto p=uintptr_t(address);const auto page=uintptr_t(sysconf(_SC_PAGESIZE));
  assert(page&&extent<=UINTPTR_MAX-p);ranges.push_back({p-(p%page),p+extent});
 }
 static bool Extent(void* state,const void* address,size_t* out){auto& s=*static_cast<ExplicitMappings*>(state);++s.calls;
  if(!out||!stopped||current!=thread.data()||s.reject_all||address==s.denied)return false;
  const auto p=uintptr_t(address);for(const auto& r:s.ranges)if(p>=r.begin&&p<r.end){*out=r.end-p;return true;}
  return false;
 }
};
std::vector<unsigned char> File(const char* path){std::ifstream in(path,std::ios::binary);assert(in.good());return {std::istreambuf_iterator<char>(in),{}};}
void LinkDex(class_metadata::Class17* klass,std::vector<unsigned char>& bytes){
 auto* prefix=new(heap.New(sizeof(dex_mapping_abi::Prefix)))dex_mapping_abi::Prefix{nullptr,bytes.data(),bytes.size(),bytes.data(),bytes.size()};
 auto* cache=heap.New(sizeof(abi::arm64::android11::LegacyCache));const uint64_t link=uintptr_t(prefix);
 std::memcpy(static_cast<char*>(cache)+offsetof(abi::arm64::android11::LegacyCache,dex_file),&link,8);klass->dex_cache=uint32_t(uintptr_t(cache));
}
void Methods(class_metadata::Class17* klass,bool deletion){
 auto* array=static_cast<unsigned char*>(heap.New(8+3*32));Put32(array,0,deletion?2:3);klass->methods=uintptr_t(array);
 for(size_t n=0;n<(deletion?2u:3u);++n){const uint32_t index=deletion?uint32_t(n+1):uint32_t(n);auto* m=reinterpret_cast<abi::arm64::android12::Method12*>(array+8+32*n);
  m->declaring_class=uint32_t(uintptr_t(klass));m->dex_method_index=index;m->access_flags=index==0?0x101:index==1?1:9;m->data=index==0?uintptr_t(&binding_value):0;
 }
 if(klass==old_class)old_method=reinterpret_cast<abi::arm64::android12::Method12*>(array+8);
}
std::array<executable_mirror::Declaration*,3> executable{};
void* Leaf(const abi::common::RunnableJniScope&,jobject object){return reinterpret_cast<void*>(uintptr_t(reinterpret_cast<executable_mirror::Declaration*>(object)->art_method));}
struct RootHooks:HookEngine{
 bool Initialize(std::string*)override{return true;}bool Remove(void*,std::string*)override{return false;}
 void*Install(void*,void*,void**,std::string*)override{std::abort();}
 void*FindSymbol(const char*,const char*n)override{
  if(std::strstr(n,"GetArtFieldEv"))return reinterpret_cast<void*>(FieldLeaf);
  if(std::strstr(n,"ArtField7GetNameEv"))return reinterpret_cast<void*>(FieldName);
  if(std::strstr(n,"ArtField17GetTypeDescriptorEv"))return reinterpret_cast<void*>(FieldType);
  if(std::strstr(n,"Class13GetDescriptor"))return reinterpret_cast<void*>(Descriptor);
  if(std::strstr(n,"artReadBarrierMark"))return reinterpret_cast<void*>(Mark);
  if(std::strstr(n,"artReadBarrierForRootSlow"))return reinterpret_cast<void*>(Root);
  if(std::strstr(n,"CurrentFromGdbEv")||!std::strcmp(n,abi::arm64::android11::kCurrentThread))return reinterpret_cast<void*>(Current);
  if(std::strstr(n,"Thread13DecodeJObject"))return reinterpret_cast<void*>(Decode);
  return nullptr;
 }
};
}
namespace artdcevm::runnable_executor {
bool DecodeStrongGlobal(const runnable_jni::Context&,jobject p,void**out){if(!p||!out)return false;*out=p;return true;}
runnable_jni::Result With(JNIEnv*e,jobject,runnable_jni::Operation fn,void*data){assert(!stopped&&current==thread.data());Put32(thread.data(),0,0);const bool ok=fn({current,e,&runtime},data);Put32(thread.data(),0,92u<<24);return ok?runnable_jni::Result::kSuccess:runnable_jni::Result::kOperationRejected;}
}
