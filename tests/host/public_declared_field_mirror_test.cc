#include "platform/common/public_declared_field_mirror.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace artdcevm;
namespace {
JNINativeInterface_ table{};JNIEnv env{&table};
int calls=0,fail_at=0,locals=0,globals=0;bool pending=false,wrong_owner=false,wrong_static=false,duplicate=false;
auto owner=reinterpret_cast<jclass>(0x100001000ULL);auto type=reinterpret_cast<jclass>(0x200002000ULL);auto array=reinterpret_cast<jobjectArray>(0x300003000ULL);auto field1=reinterpret_cast<jobject>(0x400004000ULL);auto field2=reinterpret_cast<jobject>(0x500005000ULL);
auto id1=reinterpret_cast<jfieldID>(0x600006002ULL);auto id2=reinterpret_cast<jfieldID>(0x700007001ULL);
bool Step(){assert(!pending);++calls;if(calls==fail_at){pending=true;return false;}return true;}
jboolean Exception(JNIEnv*){return pending;}
jclass Type(JNIEnv*,jobject){if(!Step())return nullptr;++locals;return type;}
jmethodID Method(JNIEnv*,jclass,const char*name,const char*){if(!Step())return nullptr;uintptr_t value=!std::strcmp(name,"getDeclaredFieldsUnchecked")?1:!std::strcmp(name,"getDeclaringClass")?2:!std::strcmp(name,"getModifiers")?3:0;assert(value);return reinterpret_cast<jmethodID>(value);}
jobject Object(JNIEnv*,jobject object,jmethodID method,va_list args){if(!Step())return nullptr;++locals;if(uintptr_t(method)==1){assert(object==owner&&va_arg(args,int)==JNI_FALSE);return array;}assert(uintptr_t(method)==2);return wrong_owner?reinterpret_cast<jobject>(8):owner;}
jsize Length(JNIEnv*,jarray object){assert(object==array);Step();return 2;}
jobject Element(JNIEnv*,jobjectArray object,jsize index){assert(object==array);if(!Step())return nullptr;++locals;return index?field2:field1;}
jfieldID From(JNIEnv*,jobject field){Step();return field==field1||duplicate?id1:id2;}
jboolean Same(JNIEnv*,jobject a,jobject b){Step();return a==b;}
jint Flags(JNIEnv*,jobject field,jmethodID method,va_list){assert(uintptr_t(method)==3);Step();return ((field==field2)^wrong_static)?8:0;}
jobject Global(JNIEnv*,jobject object){if(!Step())return nullptr;++globals;return object;}
void DeleteLocal(JNIEnv*,jobject){--locals;assert(locals>=0);}
void DeleteGlobal(JNIEnv*,jobject){--globals;assert(globals>=0);}
void Reset(){assert(!locals&&!globals);calls=fail_at=0;pending=wrong_owner=wrong_static=duplicate=false;}
}
int main(){table.ExceptionCheck=Exception;table.GetObjectClass=Type;table.GetMethodID=Method;table.CallObjectMethodV=Object;table.CallIntMethodV=Flags;table.GetArrayLength=Length;table.GetObjectArrayElement=Element;table.FromReflectedField=From;table.IsSameObject=Same;table.NewGlobalRef=Global;table.DeleteLocalRef=DeleteLocal;table.DeleteGlobalRef=DeleteGlobal;
 for(bool statik:{false,true}){Reset();jobject result=nullptr;std::string error;assert(public_declared_field_mirror::Create(&env,owner,statik?id2:id1,statik,&result,&error));assert(result==(statik?field2:field1)&&!locals&&globals==1);int total=calls;DeleteGlobal(&env,result);
  for(int fail=1;fail<=total;++fail){Reset();fail_at=fail;result=reinterpret_cast<jobject>(1);assert(!public_declared_field_mirror::Create(&env,owner,statik?id2:id1,statik,&result,&error));assert(!result&&pending&&calls==fail&&!locals&&!globals);}
 }
 for(int fault=0;fault<4;++fault){Reset();wrong_owner=fault==0;wrong_static=fault==1;duplicate=fault==2;jobject result=nullptr;std::string error;auto id=fault==3?reinterpret_cast<jfieldID>(9):id1;assert(!public_declared_field_mirror::Create(&env,owner,id,false,&result,&error));assert(!result&&!locals&&!globals&&!pending);}
 Reset();pending=true;jobject result=reinterpret_cast<jobject>(1);std::string error;assert(!public_declared_field_mirror::Create(&env,owner,id1,false,&result,&error)&&!result&&!calls&&pending);
 std::puts("Original declared-field mirror boundary: even/odd opaqueIDs, exact owner/static, missing/duplicate refusal, every throwing JNI leaf cleanup, no getType/ToReflected(true) PASS");
}
