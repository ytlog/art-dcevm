#include "platform/common/jit_service.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace artdcevm::jit_service;
namespace {
struct Call {void* jit;void* method;void* self;int kind;bool baseline,osr,prejit,dynamic;} call{};
int calls=0;bool result=true;int objects[3];
bool Legacy(void* j,void* m,void* s,bool b,bool o,bool p){++calls;call={j,m,s,-1,b,o,p,false};return result;}
bool Standard(void* j,void* m,void* s,CompilationKind12 k,bool p){++calls;call={j,m,s,static_cast<int>(k),false,false,p,false};return result;}
bool Modern(void* j,void* m,void* s,CompilationKind17 k,bool p,bool d){++calls;call={j,m,s,static_cast<int>(k),false,false,p,d};return result;}
}
int main(){
 for(int api=30;api<=37;++api){
  Compiler compiler;int lookups=0;
  assert(Compiler::Bind(api,[&](const char* name)->void*{
   ++lookups;
   const char* expected=api==30?kCompile11:api==37?kCompile17:kCompile12;
   assert(std::strcmp(name,expected)==0);
   return api==30?reinterpret_cast<void*>(Legacy):api==37?reinterpret_cast<void*>(Modern):reinterpret_cast<void*>(Standard);
  },&compiler));
  assert(lookups==1);
  for(Kind kind:{Kind::kOsr,Kind::kBaseline,Kind::kOptimized})for(bool p:{false,true}){
   result=true;int before=calls;
   assert(compiler.Compile(&objects[0],&objects[1],&objects[2],kind,p));assert(calls==before+1);
   assert(call.jit==&objects[0]&&call.method==&objects[1]&&call.self==&objects[2]&&call.prejit==p&&!call.dynamic);
   if(api==30){assert(call.baseline==(kind==Kind::kBaseline));assert(call.osr==(kind==Kind::kOsr));}
   else {int k=kind==Kind::kOsr?0:kind==Kind::kBaseline?(api==37?2:1):(api==37?3:2);assert(call.kind==k);}
   result=false;assert(!compiler.Compile(&objects[0],&objects[1],&objects[2],kind,p));assert(calls==before+2);
  }
  result=true;int before=calls;
  assert(compiler.Compile(&objects[0],&objects[1],&objects[2],Kind::kFast,true)==(api==37));
  assert(calls==before+(api==37?1:0));
  if(api==37){
   assert(call.kind==1&&call.prejit&&!call.dynamic);
   assert(call.jit==&objects[0]&&call.method==&objects[1]&&call.self==&objects[2]);
   result=false;
   assert(!compiler.Compile(&objects[0],&objects[1],&objects[2],Kind::kFast,false,true));
   assert(calls==before+2&&call.kind==1&&!call.prejit&&call.dynamic);
  }
  result=true;before=calls;
  bool ok=compiler.Compile(&objects[0],&objects[1],&objects[2],Kind::kOptimized,true,true);
  assert(ok==(api==37));assert(calls==before+(api==37?1:0));if(api==37)assert(call.dynamic&&call.kind==3);
  before=calls;
  assert(!compiler.Compile(nullptr,&objects[1],&objects[2],Kind::kOptimized));
  assert(!compiler.Compile(&objects[0],nullptr,&objects[2],Kind::kOptimized));
  assert(!compiler.Compile(&objects[0],&objects[1],nullptr,Kind::kOptimized));
  assert(!compiler.Compile(&objects[0],&objects[1],&objects[2],static_cast<Kind>(42)));assert(calls==before);
  // An unsuccessful rebinding must retain the previously authorized function.
  assert(!Compiler::Bind(api,[&](const char*)->void*{++lookups;return nullptr;},&compiler));assert(lookups==2);
  for(int unsupported:{29,38})assert(!Compiler::Bind(unsupported,[&](const char*)->void*{++lookups;return nullptr;},&compiler));
  assert(!Compiler::Bind(api,[&](const char*)->void*{++lookups;return nullptr;},nullptr));assert(lookups==2);
  assert(compiler.Compile(&objects[0],&objects[1],&objects[2],Kind::kOptimized));
 }
 Compiler unbound;int before=calls;
 assert(!unbound.Compile(&objects[0],&objects[1],&objects[2],Kind::kOptimized));assert(calls==before);
 std::cout<<"JIT compile service: all APIs, kinds, exact symbols, argument propagation and failed binding passed\n";
}
