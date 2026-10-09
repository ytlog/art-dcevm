#include "platform/common/native_final_field_service.cc"
#include "platform/common/native_removal_policy.h"
#include <cassert>
#include <iostream>
int main(){
 using namespace artdcevm;
 static_assert(jvmti_field_flags::Public(0xffff0000u)==0);
 static_assert(jvmti_field_flags::Public(0x20001u)==1);
 evolution::ClassSchema a,b;a.descriptor=b.descriptor="LFixture;";
 a.fields.push_back({"value","I",0x10000001u,0});b.fields.push_back({"value","I",0x20020001u,0});
 // Actual Service schema attestation ignores high field runtime bits without
 // inventing synchronized. It still refuses any changed Java field modifier.
 assert(final_field_service::Schema(a,b));
 std::string error;assert(native_owned_removal::ValidateRemoval(a,b,true,&error,false));
 for(uint32_t bit:{1u,2u,4u,8u,0x10u,0x40u,0x80u,0x1000u,0x4000u}){
  b.fields[0].access=a.fields[0].access^bit;assert(!final_field_service::Schema(a,b));
  assert(!native_owned_removal::ValidateRemoval(a,b,true,&error,false));
 }
 b.fields=a.fields;a.methods.push_back({"call","()V",1u,0});b.methods.push_back({"call","()V",0x20001u,0});
 // Method-only declared synchronization remains part of method attestation.
 assert(!final_field_service::Schema(a,b));
 std::cout<<"PASS actual FinalService field flags and method isolation\n";
}
