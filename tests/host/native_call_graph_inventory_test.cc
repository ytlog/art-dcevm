#include "platform/common/native_schema_guard_verifier.cc"
#include <cassert>
#include <iostream>
using namespace artdcevm;
namespace {
unsigned requests=0;int mode=0;bool native_method=false;
jvmtiError JNICALL Deallocate(jvmtiEnv*,unsigned char*p){std::free(p);return JVMTI_ERROR_NONE;}
jvmtiError JNICALL ClassSignature(jvmtiEnv*,jclass,char**out,char**){*out=strdup("Lsample/Test;");return JVMTI_ERROR_NONE;}
jvmtiError JNICALL ClassModifiers(jvmtiEnv*,jclass,jint*out){*out=1;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL ClassMethods(jvmtiEnv*,jclass,jint*out,jmethodID**ids){*out=1;*ids=static_cast<jmethodID*>(std::malloc(sizeof(jmethodID)));(*ids)[0]=reinterpret_cast<jmethodID>(0x1000);return JVMTI_ERROR_NONE;}
jvmtiError JNICALL MethodName(jvmtiEnv*,jmethodID,char**name,char**signature,char**){*name=strdup("run");*signature=strdup("()V");return JVMTI_ERROR_NONE;}
jvmtiError JNICALL MethodModifiers(jvmtiEnv*,jmethodID,jint*out){*out=native_method?0x101:1;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL ClassFields(jvmtiEnv*,jclass,jint*out,jfieldID**ids){*out=0;*ids=nullptr;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Bytecodes(jvmtiEnv*,jmethodID,jint*out,unsigned char**bytes){++requests;if(mode==3)return JVMTI_ERROR_ABSENT_INFORMATION;*out=mode==1?1:2;*bytes=static_cast<unsigned char*>(std::malloc(size_t(*out)));(*bytes)[0]=mode==2?0:0x0e;if(*out==2)(*bytes)[1]=0;return JVMTI_ERROR_NONE;}
bool granted=false,potential=true,rejected=false,grant_after_add=true;unsigned adds=0;
jvmtiError JNICALL Capabilities(jvmtiEnv*,jvmtiCapabilities*out){*out={};out->can_get_bytecodes=granted;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Potential(jvmtiEnv*,jvmtiCapabilities*out){*out={};out->can_get_bytecodes=potential;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Add(jvmtiEnv*,const jvmtiCapabilities*requested){assert(requested->can_get_bytecodes&& !requested->can_tag_objects);++adds;if(rejected)return JVMTI_ERROR_NOT_AVAILABLE;if(grant_after_add)granted=true;return JVMTI_ERROR_NONE;}
jboolean JNICALL ExceptionCheck(JNIEnv*){return JNI_FALSE;}
}
int main(){
 jvmtiInterface_1 table{};table.Deallocate=Deallocate;table.GetClassSignature=ClassSignature;table.GetClassModifiers=ClassModifiers;table.GetClassMethods=ClassMethods;table.GetMethodName=MethodName;table.GetMethodModifiers=MethodModifiers;table.GetClassFields=ClassFields;table.GetBytecodes=Bytecodes;
 jvmtiEnv ti{&table};JNINativeInterface_ jni{};jni.ExceptionCheck=ExceptionCheck;JNIEnv env{&jni};std::string error;
 using namespace native_schema_guard_verifier;
 table.GetCapabilities=Capabilities;table.GetPotentialCapabilities=Potential;table.AddCapabilities=Add;
 assert(EnsureCallGraphBytecodes(&ti,&error)&&adds==1&&granted);assert(EnsureCallGraphBytecodes(&ti,&error)&&adds==1);
 granted=false;potential=false;assert(!EnsureCallGraphBytecodes(&ti,&error)&&adds==1);
 potential=true;rejected=true;assert(!EnsureCallGraphBytecodes(&ti,&error)&&adds==2);
 rejected=false;grant_after_add=false;assert(!EnsureCallGraphBytecodes(&ti,&error)&&adds==3);grant_after_add=true;
 {
  std::vector<std::vector<size_t>> graph{{1,2},{3},{3},{0,4},{}};
  assert(WalkCallGraphDomain(graph,0)==std::vector<size_t>({0,1,2,3,4}));
  graph[4].push_back(7);assert(WalkCallGraphDomain(graph,0).empty());
  constexpr size_t count=50000;graph.assign(count,{});for(size_t i=1;i<count;++i)graph[i-1].push_back(i);
  auto domain=WalkCallGraphDomain(graph,0);assert(domain.size()==count&&domain.back()==count-1);
 }

 Node original;assert(PublicMembers(&env,&ti,nullptr,&original,&error));assert(requests==1&&original.method_bytes.size()==1&&original.method_bytes[0]==std::vector<unsigned char>({0x0e,0}));
 Node same;assert(PublicMembers(&env,&ti,nullptr,&same,&error)&&SamePublic(original,same));
 mode=2;Node changed;assert(PublicMembers(&env,&ti,nullptr,&changed,&error)&&!SamePublic(original,changed));
 mode=1;Node odd;assert(!PublicMembers(&env,&ti,nullptr,&odd,&error));mode=3;Node absent;assert(!PublicMembers(&env,&ti,nullptr,&absent,&error));
 table.GetBytecodes=nullptr;Node missing;assert(!PublicMembers(&env,&ti,nullptr,&missing,&error));native_method=true;Node native;auto before=requests;assert(PublicMembers(&env,&ti,nullptr,&native,&error)&&requests==before&&native.method_bytes.size()==1&&native.method_bytes[0].empty());
 std::cout<<"Actual verifier public Code inventory: capability negotiation/refused grant/body changes/odd bytes/native boundary; source domain diamond/cycle/50000 depth passed\n";
}
