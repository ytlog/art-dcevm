#include <cassert>
#include <cstdio>
#include <string>
#include "../../app/src/debug/cpp/testing/native_test_bridge.cc"
namespace {
jint instance_value=41,static_value=42;bool pending=false,reject_static=false;int local_deletes=0,read_count=0,sets=0;
jclass JNICALL Class(JNIEnv*,jobject){return reinterpret_cast<jclass>(0x100);}
jfieldID JNICALL InstanceId(JNIEnv*,jclass,const char* name,const char* type){assert(std::string(name)=="keep"&&std::string(type)=="I");return reinterpret_cast<jfieldID>(0x200);}
jfieldID JNICALL StaticId(JNIEnv*,jclass,const char* name,const char* type){assert(std::string(name)=="staticKeep"&&std::string(type)=="I");if(reject_static){pending=true;return nullptr;}return reinterpret_cast<jfieldID>(0x300);}
jint JNICALL Get(JNIEnv*,jobject,jfieldID id){assert(id==reinterpret_cast<jfieldID>(0x200));++read_count;return instance_value;}
jint JNICALL GetStatic(JNIEnv*,jclass,jfieldID id){assert(id==reinterpret_cast<jfieldID>(0x300));++read_count;return static_value;}
void JNICALL Set(JNIEnv*,jobject,jfieldID id,jint value){assert(id==reinterpret_cast<jfieldID>(0x200));++sets;instance_value=value;}
void JNICALL SetStatic(JNIEnv*,jclass,jfieldID id,jint value){assert(id==reinterpret_cast<jfieldID>(0x300));++sets;static_value=value;}
jboolean JNICALL Pending(JNIEnv*){return pending?JNI_TRUE:JNI_FALSE;}
void JNICALL Delete(JNIEnv*,jobject){++local_deletes;}
void Reset(){instance_value=41;static_value=42;pending=reject_static=false;local_deletes=read_count=sets=0;}
}
int main(){
 JNINativeInterface_ api{};api.GetObjectClass=Class;api.GetFieldID=InstanceId;api.GetStaticFieldID=StaticId;api.GetIntField=Get;api.GetStaticIntField=GetStatic;api.SetIntField=Set;api.SetStaticIntField=SetStatic;api.ExceptionCheck=Pending;api.DeleteLocalRef=Delete;JNIEnv env{&api};
 auto run=[&]{return Java_com_github_ytlog_artdcevm_testing_NativeTestBridge_proveFreshKeptFieldIds(&env,nullptr,reinterpret_cast<jobject>(0x400));};
 Reset();assert(run()==4);assert(instance_value==41&&static_value==42&&sets==4&&read_count==4&&local_deletes==1);
 Reset();instance_value=7;assert(run()==0&&sets==0&&local_deletes==1);
 Reset();static_value=7;assert(run()==0&&sets==0&&local_deletes==1);
 Reset();reject_static=true;assert(run()==0&&pending&&sets==0&&read_count==0&&local_deletes==1);
 puts("fresh kept JNI: replacement IDs, both storage modes, read/write/restore and refusal PASS");
}
