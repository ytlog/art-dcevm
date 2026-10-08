#include "platform/android17/method_table.h"
#include "platform/common/executable_mirror_calibration.h"
#include "platform/common/native_method_binding_calibration.h"
#include "platform/common/modern_method_services.h"
#include <array>
#include <cassert>
#include <iostream>
using namespace artdcevm;
namespace {
bool pending=false;
int jni_checks=0;
void* old_class_pointer=nullptr;
int obsolete_calls=0;
void* Root(void* method){uint32_t value;std::memcpy(&value,method,4);return reinterpret_cast<void*>(uintptr_t(value));}
bool Same(void* a,void* b){return a==b;}
void Obsolete(void* linker,void* method){assert(linker&&method);++obsolete_calls;}
bool Retired(void*){return false;}
struct Hooks:HookEngine {
  bool missing=false;
  bool Initialize(std::string*)override{return true;}
  void* FindSymbol(const char*,const char* name)override{
    if(missing)return nullptr;
    if(std::strcmp(name,"artReadBarrierForRootSlow")==0)return reinterpret_cast<void*>(Root);
    if(std::strstr(name,"HasSameNameAndSignature"))return reinterpret_cast<void*>(Same);
    if(std::strstr(name,"SetEntryPointsForObsoleteMethod"))return reinterpret_cast<void*>(Obsolete);
    return nullptr;
  }
  void* Install(void*,void*,void**,std::string*)override{std::abort();}
  bool Remove(void*,std::string*)override{std::abort();}
};
std::array<int,4> handles{};
std::array<void*,4> native{};
std::array<executable_mirror::Declaration,4> mirrors{};
bool Decode(const runnable_jni::Context&, jobject method, void** out) {
  for(size_t i=0;i<handles.size();++i)if(method==reinterpret_cast<jobject>(&handles[i])){*out=&mirrors[i];return true;}
  return false;
}
jboolean Exception(JNIEnv*) {++jni_checks;return pending;}
jobjectRefType RefType(JNIEnv*,jobject){return JNIGlobalRefType;}
void* Leaf(const abi::arm64::android_common::RunnableJniScope&,jobject method){
  for(size_t i=0;i<handles.size();++i)if(method==reinterpret_cast<jobject>(&handles[i]))return native[i];
  return nullptr;
}
}
int main(){
  JNINativeInterface_ jni{};jni.ExceptionCheck=Exception;jni.GetObjectRefType=RefType;
  JNIEnv env{&jni};int self,vm;
  runnable_jni::Context context{&self,&env,&vm};
  class_metadata::Class17 klass{};
  alignas(8) std::array<unsigned char,8+5*32> array{};
  uint32_t length=5;std::memcpy(array.data(),&length,4);
  klass.methods=uintptr_t(array.data());
  const uint32_t flags[]={1,8,0x10001,0x102};
  std::array<method_identity::Oracle,4> oracles{};
  for(size_t i=0;i<4;++i){
    native[i]=array.data()+8+i*32;uint32_t index=19+uint32_t(i);
    std::memcpy(static_cast<unsigned char*>(native[i])+4,&flags[i],4);
    std::memcpy(static_cast<unsigned char*>(native[i])+8,&index,4);
    oracles[i]={reinterpret_cast<jobject>(&handles[i]),index,flags[i]};
    mirrors[i].art_method=uintptr_t(native[i]);mirrors[i].access_flags=flags[i];mirrors[i].dex_method_index=index;
  }
  method_identity::Calibration model{method_identity::Model::kMethod12Onward,{{1,19},{8,20},{0x10001,21},{0x102,22}},{32,32,32,32}};
  evolution::MappedDexMemberCounts counts{0,0,3,1};
  method_array::View table;
  std::string error;
  const auto run=[&](){return android17::CalibrateMethodTable(context,Leaf,model,&klass,counts,oracles.data(),oracles.size(),&table,&error);};
  const auto mirror_run=[&](){return executable_mirror::Calibrate(context,Leaf,Decode,oracles.data(),oracles.size(),&error);};
  assert(mirror_run());
  int binding;
  uint64_t native_binding=uintptr_t(&binding);
  std::memcpy(static_cast<unsigned char*>(native[3])+16,&native_binding,8);
  assert(native_method_binding::Calibrate(context,Leaf,oracles[3],&binding,&error));
  native_binding++;std::memcpy(static_cast<unsigned char*>(native[3])+16,&native_binding,8);
  assert(!native_method_binding::Calibrate(context,Leaf,oracles[3],&binding,&error));
  assert(!native_method_binding::Calibrate(context,Leaf,oracles[0],&binding,&error));
  // Legacy storage has a distinct DEX-index and mutable binding declaration.
  method_metadata::Method11 legacy{};
  legacy.access_flags=oracles[3].java_modifiers;
  legacy.dex_method_index=oracles[3].dex_method_index;
  legacy.data=uintptr_t(&binding);
  auto* modern_native=native[3];native[3]=&legacy;
  assert(native_method_binding::Calibrate(context,Leaf,oracles[3],&binding,method_identity::Model::kMethod11,&error));
  assert(!native_method_binding::Calibrate(context,Leaf,oracles[3],&binding,&error));
  legacy.data++;
  assert(!native_method_binding::Calibrate(context,Leaf,oracles[3],&binding,method_identity::Model::kMethod11,&error));
  legacy.data--;
  assert(!native_method_binding::Calibrate(context,Leaf,oracles[0],&binding,method_identity::Model::kMethod11,&error));
  assert(!native_method_binding::Calibrate(context,Leaf,oracles[3],&binding,static_cast<method_identity::Model>(99),&error));
  native[3]=modern_native;
  mirrors[0].art_method++;assert(!mirror_run());mirrors[0].art_method--;
  mirrors[1].access_flags^=8;assert(!mirror_run());mirrors[1].access_flags^=8;
  mirrors[2].dex_method_index++;assert(!mirror_run());mirrors[2].dex_method_index--;
  assert(mirror_run());
  assert(run() && table.count==5 && table.boundaries.declared_count==4);
  assert(!table.IsDirect(0) && table.IsDirect(1) && table.IsDirect(2) && table.IsDirect(3));
  // One copied default remains virtual; reflection need cover declared members only.
  assert(!table.IsDirect(4));
  std::swap(oracles[0],oracles[3]);assert(run());
  const auto failure=[&](){table.count=77;assert(!run() && table.count==77 && !error.empty());};
  oracles[0].dex_method_index++;failure();oracles[0].dex_method_index--;
  oracles[0].java_modifiers^=8;failure();oracles[0].java_modifiers^=8;
  auto* saved=native[0];native[0]=array.data()+8+4*32;failure();native[0]=saved;
  counts.direct_methods=2;counts.virtual_methods=2;failure();counts={0,0,3,1};
  klass.methods++;failure();klass.methods--;
  pending=true;failure();pending=false;
  model.model=method_identity::Model::kMethod11;failure();model.model=method_identity::Model::kMethod12Onward;
  length=UINT32_MAX;std::memcpy(array.data(),&length,4);failure();
  // Exercise the actual modern adapter over two fully checked source tables.
  length=5;std::memcpy(array.data(),&length,4);assert(run());
  old_class_pointer=reinterpret_cast<void*>(0x1000);
  uint32_t compact=0x1000;for(uint32_t i=0;i<5;++i)std::memcpy(array.data()+8+i*32,&compact,4);
  native_binding=uintptr_t(&binding);std::memcpy(static_cast<unsigned char*>(native[3])+16,&native_binding,8);
  alignas(8) auto replacement=array;
  auto new_table=table;new_table.array=replacement.data();void* new_class=reinterpret_cast<void*>(0x2000);int linker;
  compact=0x2000;for(uint32_t i=0;i<5;++i)std::memcpy(replacement.data()+8+i*32,&compact,4);
  Hooks hooks;
  auto service=modern_methods::Service::Prepare(hooks,context,Leaf,oracles[0],&binding,
      &linker,old_class_pointer,table,new_class,new_table,Retired,&error);
  assert(service&&service->Count(old_class_pointer)==5&&service->Count(new_class)==5);
  auto bound=modern_methods::Service::BindOwned(hooks,context,Leaf,oracles[0],&binding,&linker,&error);
  assert(bound);
  int before_jni=jni_checks;pending=true;
  auto prepared=modern_methods::Service::PrepareTables(*bound,old_class_pointer,table,new_class,new_table,Retired,&error);
  assert(prepared&&jni_checks==before_jni); // Actual commit preparation cannot query JNI.
  pending=false;

  assert(service->DeclaringClass(native[3])==old_class_pointer&&service->SameIdentity(native[3],native[3]));
  assert(!service->IsRetired(native[3]));
  uint64_t zero=0;std::memcpy(replacement.data()+8+3*32+16,&zero,8);
  service->TransferNativeBinding(native[3],service->At(new_class,3));
  uint64_t transferred=0;std::memcpy(&transferred,replacement.data()+8+3*32+16,8);
  assert(transferred==uintptr_t(&binding));
  compact=0x2000;for(uint32_t i=0;i<5;++i)std::memcpy(array.data()+8+i*32,&compact,4);
  service->RestoreOldDeclaringClasses();
  assert(service->DeclaringClass(native[0])==old_class_pointer);
  service->AddFlags(native[0],0x40000);assert(service->Flags(native[0])&0x40000);
  service->SetObsoleteEntry(native[0]);assert(obsolete_calls==1);
  hooks.missing=true;
  assert(!modern_methods::Service::Prepare(hooks,context,Leaf,oracles[0],&binding,
      &linker,old_class_pointer,table,new_class,new_table,Retired,&error));
  hooks.missing=false;new_table.stride=40;
  assert(!modern_methods::Service::Prepare(hooks,context,Leaf,oracles[0],&binding,
      &linker,old_class_pointer,table,new_class,new_table,Retired,&error));
  std::cout<<"Android17 method table: interleaved declarations, constructors, copied methods and independent identity failures passed\n";
}
