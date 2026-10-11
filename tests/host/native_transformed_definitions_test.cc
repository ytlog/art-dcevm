#include "platform/common/native_transformed_definitions.h"
#include "platform/abi/arm64/common/transformed_definition.h"
#include <cassert>
#include <climits>
#include <iostream>
namespace {
using namespace artdcevm;
using Native=abi::arm64::android_common::TransformedDefinition;
using Vector=abi::arm64::android_common::TransformedDefinitions;
bool Same(const std::vector<Definition>&a,const std::vector<Definition>&b){
 if(a.size()!=b.size())return false;
 for(size_t n=0;n<a.size();++n)if(a[n].klass!=b[n].klass||a[n].dex!=b[n].dex)return false;
 return true;
}
}
int main(){
 using namespace artdcevm;int classes[2]{};unsigned char first[]{1,2,3},second[]{4,5};
 Native values[2]{};
 for(size_t n=0;n<2;++n){values[n].klass=reinterpret_cast<jclass>(classes+n);values[n].initialized=1;}
 values[0].dex_begin=first;values[0].dex_size=sizeof(first);
 values[1].dex_begin=second;values[1].dex_size=sizeof(second);
 Vector source{values,values+2,values+2};std::vector<Definition>copied;std::string error;
 assert(native_transformed_definitions::Capture(&source,2,&copied,&error));
 assert(copied.size()==2&&copied[0].klass==values[0].klass&&copied[1].klass==values[1].klass);
 assert(copied[0].dex==std::vector<unsigned char>({1,2,3})&&copied[1].dex==std::vector<unsigned char>({4,5}));
 // Source callbacks own these buffers. Subsequent mutation/disposal cannot
 // alter the final byte arrays that preparation will use after JNI resumes.
 first[0]=9;second[0]=8;
 assert(copied[0].dex[0]==1&&copied[1].dex[0]==4);
 const auto previous=copied;
 const auto rejected=[&](const void*input,size_t count){
  error.clear();assert(!native_transformed_definitions::Capture(input,count,&copied,&error));
  assert(!error.empty()&&Same(copied,previous));
 };
 rejected(nullptr,2);rejected(&source,0);rejected(&source,1);rejected(&source,3);
 Vector malformed=source;malformed.capacity=values+1;rejected(&malformed,2);
 malformed=source;malformed.end=reinterpret_cast<const Native*>(uintptr_t(values)+1);rejected(&malformed,2);
 malformed=source;malformed.begin=reinterpret_cast<const Native*>(uintptr_t(1));rejected(&malformed,2);
 rejected(reinterpret_cast<const void*>(UINTPTR_MAX),2);
 // Failure on the second definition must not publish the first copied item.
 const auto original=values[1];
 values[1].initialized=0;rejected(&source,2);values[1]=original;
 values[1].initialized=2;rejected(&source,2);values[1]=original;
 values[1].redefined=2;rejected(&source,2);values[1]=original;
 values[1].structural_update=2;rejected(&source,2);values[1]=original;
 values[1].klass=nullptr;rejected(&source,2);values[1]=original;
 values[1].dex_size=0;rejected(&source,2);values[1]=original;
 values[1].dex_size=size_t(INT32_MAX)+1;rejected(&source,2);values[1]=original;
 values[1].dex_begin=reinterpret_cast<const unsigned char*>(UINTPTR_MAX);rejected(&source,2);values[1]=original;
 assert(native_transformed_definitions::Capture(&source,2,&copied,&error));
 assert(copied[0].dex[0]==9&&copied[1].dex[0]==8);
 std::vector<Definition>foreign;
 assert(native_transformed_definitions::Capture(&source,&foreign,&error)&&Same(foreign,copied));
 const auto installed=foreign;Vector empty{values,values,values};
 assert(!native_transformed_definitions::Capture(&empty,&foreign,&error)&&Same(foreign,installed));
 values[1].initialized=0;
 assert(!native_transformed_definitions::Capture(&source,&foreign,&error)&&Same(foreign,installed));
 values[1]=original;
 std::cout<<"Actual final-vector reader: ordered deep copy, malformed geometry/count/flags/spans and late-member failure preserve prior output PASS; no JNI identity, DEX semantics or transaction permission granted\n";
}
