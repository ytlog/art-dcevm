#include <cassert>
#include <cstring>
#include "../../app/src/debug/cpp/testing/production_permission_probe.cc"
namespace {
JNIEnv environment;JNINativeInterface_ original{},changed{};
JavaVM* actual_vm=reinterpret_cast<JavaVM*>(0x1000);
jclass owner=reinterpret_cast<jclass>(0x2000);
jobject instance=reinterpret_cast<jobject>(0x3000);
jfieldID field_id=reinterpret_cast<jfieldID>(0x4000),static_id=reinterpret_cast<jfieldID>(0x5000);
jstring field_name=reinterpret_cast<jstring>(0x6000),static_name=reinterpret_cast<jstring>(0x7000);
int value=17,shared=19,globals=0,deleted=0,created=0,utf=0,errors=0,poison=0;
bool pending=false,fail_field=false,fail_second_global=false;
jint JNICALL Vm(JNIEnv*,JavaVM** vm){*vm=actual_vm;return JNI_OK;}
jboolean JNICALL Exception(JNIEnv*){return pending;}
jclass JNICALL Find(JNIEnv*,const char*){return owner;}
jint JNICALL Throw(JNIEnv*,jclass,const char*){pending=true;errors++;return JNI_OK;}
void JNICALL Local(JNIEnv*,jobject){}
const char* JNICALL String(JNIEnv*,jstring name,jboolean*){utf++;return name==field_name?"value":"shared";}
void JNICALL ReleaseString(JNIEnv*,jstring,const char*){utf--;}
jfieldID JNICALL Field(JNIEnv*,jclass cls,const char* name,const char* type){assert(cls==owner&&std::strcmp(name,"value")==0&&std::strcmp(type,"I")==0);if(fail_field){pending=true;return nullptr;}return field_id;}
jfieldID JNICALL StaticField(JNIEnv*,jclass cls,const char* name,const char* type){assert(cls==owner&&std::strcmp(name,"shared")==0&&std::strcmp(type,"I")==0);return static_id;}
jobject JNICALL Global(JNIEnv*,jobject obj){created++;if(fail_second_global&&created==2){pending=true;return nullptr;}globals++;return obj;}
void JNICALL Delete(JNIEnv*,jobject){globals--;deleted++;}
jint JNICALL Get(JNIEnv*,jobject obj,jfieldID id){assert(obj==instance&&id==field_id);return value;}
jint JNICALL GetStatic(JNIEnv*,jclass cls,jfieldID id){assert(cls==owner&&id==static_id);return shared;}
void JNICALL Set(JNIEnv*,jobject obj,jfieldID id,jint v){assert(obj==instance&&id==field_id);value=v;}
void JNICALL SetStatic(JNIEnv*,jclass cls,jfieldID id,jint v){assert(cls==owner&&id==static_id);shared=v;}
jint JNICALL PoisonGet(JNIEnv*,jobject,jfieldID){poison++;return -1;}
jint JNICALL PoisonStaticGet(JNIEnv*,jclass,jfieldID){poison++;return -1;}
void JNICALL PoisonSet(JNIEnv*,jobject,jfieldID,jint){poison++;}
void JNICALL PoisonStaticSet(JNIEnv*,jclass,jfieldID,jint){poison++;}
void Retain(){Java_com_github_ytlog_artdcevm_testing_ProductionPermissionNative_retain(&environment,nullptr,owner,instance,field_name,static_name);}
void Clear(){Java_com_github_ytlog_artdcevm_testing_ProductionPermissionNative_release(&environment,nullptr);}
int Read(bool statik){return Java_com_github_ytlog_artdcevm_testing_ProductionPermissionNative_read(&environment,nullptr,statik);}
void Write(bool statik,int v){Java_com_github_ytlog_artdcevm_testing_ProductionPermissionNative_write(&environment,nullptr,statik,v);}
}
int main(){
 original.GetJavaVM=Vm;original.ExceptionCheck=Exception;original.FindClass=Find;original.ThrowNew=Throw;original.DeleteLocalRef=Local;
 original.GetStringUTFChars=String;original.ReleaseStringUTFChars=ReleaseString;original.GetFieldID=Field;original.GetStaticFieldID=StaticField;
 original.NewGlobalRef=Global;original.DeleteGlobalRef=Delete;original.GetIntField=Get;original.GetStaticIntField=GetStatic;
 original.SetIntField=Set;original.SetStaticIntField=SetStatic;environment.functions=&original;
 Retain();assert(globals==2&&utf==0&&Read(false)==17&&Read(true)==19);
 // The actual TU stores original function entries, not just IDs. A later table cannot impersonate them.
 changed=original;changed.GetIntField=PoisonGet;changed.GetStaticIntField=PoisonStaticGet;changed.SetIntField=PoisonSet;changed.SetStaticIntField=PoisonStaticSet;environment.functions=&changed;
 Write(false,73);Write(true,84);assert(Read(false)==73&&Read(true)==84&&poison==0);
 Retain();assert(errors==1&&globals==2);pending=false;
 actual_vm=reinterpret_cast<JavaVM*>(0x9990);Write(false,1);assert(pending&&value==73);pending=false;assert(Read(true)==0&&pending&&shared==84);pending=false;
 actual_vm=reinterpret_cast<JavaVM*>(0x1000);Clear();assert(globals==0&&deleted==2&&!cache.owner);Clear();assert(deleted==2);
 assert(Read(false)==0&&pending);pending=false;
 environment.functions=&original;created=0;fail_second_global=true;Retain();assert(pending&&globals==0&&deleted==3&&!cache.owner&&utf==0);pending=false;fail_second_global=false;
 fail_field=true;Retain();assert(pending&&globals==0&&!cache.owner&&utf==0);pending=false;fail_field=false;
 created=0;Retain();assert(globals==2&&Read(false)==73);Clear();assert(globals==0&&utf==0);
}
