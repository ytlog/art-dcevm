#include "platform/common/canonical_jni_members.h"
#include <cassert>
#include <iostream>
using namespace artdcevm::canonical_jni_members;
using Declarations=Scope::Declarations;
int manager,method,field;bool null_manager=false,wrong_method=false,null_field=false;
int starts=0,ends=0,method_calls=0,field_calls=0;
void Construct(artdcevm::abi::common::JniIdQueryScope* scope){++starts;scope->manager=null_manager?nullptr:&manager;}
void Destroy(artdcevm::abi::common::JniIdQueryScope* scope){assert(scope->manager==&manager||null_manager);++ends;}
void* DecodeMethod(void* receiver,jmethodID id){assert(receiver==&manager);++method_calls;
 if(wrong_method)return &field;
 return id==reinterpret_cast<jmethodID>(&method)||id==reinterpret_cast<jmethodID>(0x101)?&method:nullptr;}
void* DecodeField(void* receiver,jfieldID id){assert(receiver==&manager);++field_calls;
 return !null_field&&(id==reinterpret_cast<jfieldID>(&field)||id==reinterpret_cast<jfieldID>(0x201))?&field:nullptr;}
int main(){Declarations source{Construct,Destroy,DecodeMethod,DecodeField};std::string error;
 for(bool indexed:{false,true}){
  Scope scope(source);assert(scope.Ready());Field result;
  assert(!scope.Read(reinterpret_cast<jfieldID>(0x201),&result,&error)&&field_calls==0);
  assert(scope.Verify(indexed?reinterpret_cast<jmethodID>(0x101):reinterpret_cast<jmethodID>(&method),{&method},&error));
  assert(scope.Read(indexed?reinterpret_cast<jfieldID>(0x201):reinterpret_cast<jfieldID>(&field),&result,&error)&&result.address==&field);
  assert(scope.Methods()==1&&scope.Fields()==1&&scope.DistinctMethods()==size_t(indexed)&&scope.DistinctFields()==size_t(indexed));
  field_calls=0;
 }
 assert(starts==2&&ends==2);
 {Scope scope(source);wrong_method=true;assert(!scope.Verify(reinterpret_cast<jmethodID>(0x101),{&method},&error));
  Field retained{&method};assert(!scope.Read(reinterpret_cast<jfieldID>(0x201),&retained,&error)&&retained.address==&method);wrong_method=false;}
 {Scope scope(source);assert(scope.Verify(reinterpret_cast<jmethodID>(0x101),{&method},&error));null_field=true;
  Field retained{&method};assert(!scope.Read(reinterpret_cast<jfieldID>(0x201),&retained,&error)&&retained.address==&method);null_field=false;}
 {null_manager=true;Scope scope(source);assert(!scope.Ready()&&!scope.Verify(reinterpret_cast<jmethodID>(0x101),{&method},&error));}null_manager=false;
 assert(starts==5&&ends==5);
 {Scope scope(source);assert(scope.Verify(reinterpret_cast<jmethodID>(0x101),{&method},&error));wrong_method=true;
  assert(!scope.Verify(reinterpret_cast<jmethodID>(0x101),{&method},&error));Field retained{&method};
  assert(!scope.Read(reinterpret_cast<jfieldID>(0x201),&retained,&error));wrong_method=false;}
 assert(starts==6&&ends==6);
 {auto missing=source;missing.destroy=nullptr;Scope scope(missing);assert(!scope.Ready());}assert(starts==6&&ends==6);
 std::cout<<"Canonical JNI IDs: pointer/distinct indexed opaque modes, independent manager/leaf proof, null/mismatch refusals and balanced original lifetime passed\n";
}
