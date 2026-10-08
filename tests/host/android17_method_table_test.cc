#include "platform/android17/method_table.h"
#include <array>
#include <cassert>
#include <iostream>
using namespace artdcevm;
namespace {
bool pending=false;
std::array<int,4> handles{};
std::array<void*,4> native{};
jboolean Exception(JNIEnv*) {return pending;}
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
  }
  method_identity::Calibration model{method_identity::Model::kMethod12Onward,{{1,19},{8,20},{0x102,22}},{32,32,32}};
  evolution::MappedDexMemberCounts counts{0,0,3,1};
  method_array::View table;
  std::string error;
  const auto run=[&](){return android17::CalibrateMethodTable(context,Leaf,model,&klass,counts,oracles.data(),oracles.size(),&table,&error);};
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
  std::cout<<"Android17 method table: interleaved declarations, constructors, copied methods and independent identity failures passed\n";
}
