#include "platform/common/owned_method_table.h"
#include <array>
#include <cassert>
#include <iostream>
#include <map>
using namespace artdcevm;
namespace {std::map<const void*,size_t> mapped;std::map<jobject,void*> targets;
jboolean Exception(JNIEnv*){return JNI_FALSE;}
jobjectRefType Ref(JNIEnv*,jobject p){return targets.count(p)?JNIGlobalRefType:JNIInvalidRefType;}
void* Leaf(const abi::arm64::android_common::RunnableJniScope&,jobject p){return targets.at(p);}
void Put(void* p,size_t offset,uint32_t value){std::memcpy(static_cast<unsigned char*>(p)+offset,&value,4);}
}
namespace artdcevm::method_identity {bool ReadableMappingExtent(const void* p,size_t* extent){auto found=mapped.find(p);if(found==mapped.end())return false;*extent=found->second;return true;}}
int main(){
 JNINativeInterface_ functions{};functions.ExceptionCheck=Exception;functions.GetObjectRefType=Ref;JNIEnv env{&functions};
 runnable_jni::Context scope{reinterpret_cast<void*>(1),&env,reinterpret_cast<void*>(2)};
 for(auto family:{method_identity::Model::kMethod11,method_identity::Model::kMethod12Onward}){
  const auto& declaration=family==method_identity::Model::kMethod11?method_metadata::kAndroid11:method_metadata::kAndroid12Onward;
  alignas(8) std::array<unsigned char,56> klass{};std::vector<uint64_t> words((8+3*declaration.size+7)/8);void* array=words.data();
  Put(array,0,3);uint64_t pointer=reinterpret_cast<uintptr_t>(array);std::memcpy(klass.data()+48,&pointer,8);
  mapped={{klass.data(),klass.size()},{array,words.size()*8}};targets.clear();
  std::array<method_identity::Oracle,3> oracles{};
  // Constructor, static and instance: public declaration order intentionally
  // differs from physical ART order. DEX indices are independent constants.
  uint32_t flags[3]={1,0x10001,9},indices[3]={41,7,19};
  for(size_t i=0;i<3;i++){
   auto member=static_cast<unsigned char*>(array)+8+i*declaration.size;
   Put(member,4,flags[i]);Put(member,declaration.dex_method_index,indices[i]);
   auto root=reinterpret_cast<jobject>(0x1000+i*16);targets[root]=member;
   oracles[(i+1)%3]={root,indices[i],flags[i]&0x11fu};
  }
  method_array::View output;std::string error;evolution::MappedDexMemberCounts counts{};counts.direct_methods=2;counts.virtual_methods=1;
  assert(owned_method_table::ReadProvenDeclaration(scope,Leaf,family,klass.data(),counts,oracles.data(),3,&output,&error));
  assert(output.count==3&&output.stride==declaration.size);
  auto original=oracles[0];oracles[0]=oracles[1];
  assert(!owned_method_table::ReadProvenDeclaration(scope,Leaf,family,klass.data(),counts,oracles.data(),3,&output,&error));oracles[0]=original;
  oracles[0].dex_method_index+=1;
  assert(!owned_method_table::ReadProvenDeclaration(scope,Leaf,family,klass.data(),counts,oracles.data(),3,&output,&error));oracles[0]=original;
  counts.direct_methods=1;counts.virtual_methods=2;
  assert(!owned_method_table::ReadProvenDeclaration(scope,Leaf,family,klass.data(),counts,oracles.data(),3,&output,&error));counts.direct_methods=2;counts.virtual_methods=1;
  mapped[array]=8+2*declaration.size;
  assert(!owned_method_table::ReadProvenDeclaration(scope,Leaf,family,klass.data(),counts,oracles.data(),3,&output,&error));
  method_identity::Calibration incomplete{};incomplete.model=family;
  assert(!owned_method_table::Calibrate(scope,Leaf,incomplete,klass.data(),counts,oracles.data(),3,&output,&error));
 }
 std::cout<<"Complete target provenance: both named declarations, reordered bijection, duplicate/wrong DEX identities, direct flags, mapped truncation and missing bootstrap proof checked\n";
}
