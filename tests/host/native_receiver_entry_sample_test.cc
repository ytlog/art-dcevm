#include "platform/common/native_receiver_entry_sample.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace artdcevm::receiver_guard;
namespace {
JNINativeInterface_ jni{};JNIEnv env{&jni};int calls=0,fail_at=0,locals=0,globals=0;
bool pending=false,null_failure=false;int array_size=2;
int input,type,arrays[3],objects[6];
bool Query(){assert(!pending);if(++calls!=fail_at)return false;if(!null_failure)pending=true;return true;}
jboolean Exception(JNIEnv*){return pending;}
jclass Class(JNIEnv*,jobject in){assert(in==reinterpret_cast<jobject>(&input));if(Query())return nullptr;++locals;return reinterpret_cast<jclass>(&type);}
jfieldID Field(JNIEnv*,jclass in,const char*name,const char*signature){assert(in==reinterpret_cast<jclass>(&type));if(Query())return nullptr;const char*names[]={"receiverEntryMethods","receiverEntryFields","receiverEntryObjects"};const char*signatures[]={"[Ljava/lang/reflect/Method;","[Ljava/lang/reflect/Field;","[Ljava/lang/Object;"};for(int i=0;i<3;++i)if(!std::strcmp(name,names[i])){assert(!std::strcmp(signature,signatures[i]));return reinterpret_cast<jfieldID>(&arrays[i]);}std::abort();}
jobject Object(JNIEnv*,jobject in,jfieldID id){assert(in==reinterpret_cast<jobject>(&input));if(Query())return nullptr;++locals;return reinterpret_cast<jobject>(id);}
jsize Length(JNIEnv*,jarray){return Query()?0:array_size;}
jobject Element(JNIEnv*,jobjectArray array,jsize index){assert(index>=0&&index<2);if(Query())return nullptr;for(int g=0;g<3;++g)if(array==reinterpret_cast<jobjectArray>(&arrays[g])){++locals;return reinterpret_cast<jobject>(&objects[g*2+index]);}std::abort();}
jobject Global(JNIEnv*,jobject value){if(Query())return nullptr;++globals;return value;}
void DeleteLocal(JNIEnv*,jobject value){assert(value&&locals>0);--locals;}
void DeleteGlobal(JNIEnv*,jobject value){assert(value&&globals>0);--globals;}
void Reset(int fail=0,bool null=false){assert(!locals&&!globals);calls=0;fail_at=fail;pending=false;null_failure=null;array_size=2;}
}
int main(){
 jni.ExceptionCheck=Exception;jni.GetObjectClass=Class;jni.GetFieldID=Field;jni.GetObjectField=Object;jni.GetArrayLength=Length;jni.GetObjectArrayElement=Element;jni.NewGlobalRef=Global;jni.DeleteLocalRef=DeleteLocal;jni.DeleteGlobalRef=DeleteGlobal;
 int count=0;{EntrySamples sample(&env);assert(sample.Read(reinterpret_cast<jobject>(&input)));assert(!locals&&globals==6);const auto*s=sample.Samples();assert(s&&!s->transition_reference);for(size_t i=0;i<2;++i){assert(s->methods[i]==reinterpret_cast<jobject>(&objects[i]));assert(s->fields[i]==reinterpret_cast<jobject>(&objects[2+i]));assert(s->receivers[i]==reinterpret_cast<jobject>(&objects[4+i]));}count=calls;assert(!sample.Read(reinterpret_cast<jobject>(&input))&&calls==count);}assert(!locals&&!globals);
 for(bool null:{false,true})for(int failure=1;failure<=count;++failure){Reset(failure,null);{EntrySamples sample(&env);assert(!sample.Read(reinterpret_cast<jobject>(&input)));assert(!sample.Samples());assert(pending==!null);}assert(!locals&&!globals);}
 Reset();array_size=3;{EntrySamples sample(&env);assert(!sample.Read(reinterpret_cast<jobject>(&input))&&!sample.Samples());}assert(!locals&&!globals);
 Reset();pending=true;{EntrySamples sample(&env);assert(!sample.Read(reinterpret_cast<jobject>(&input))&&!calls);}assert(pending&&!locals&&!globals);
 std::puts("Entry public sample collector: exact three independent two-element arrays, six strong roots, no transition claim; every JNI null/pending boundary and wrong length clean own refs without clearing pending exception PASS");
}
