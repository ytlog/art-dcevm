#include <jni.h>
#include <mutex>
namespace {
struct Cache {
 JavaVM* vm=nullptr;jclass owner=nullptr;jobject instance=nullptr;jfieldID field=nullptr,static_field=nullptr;
 jint(JNICALL* get)(JNIEnv*,jobject,jfieldID)=nullptr;
 jint(JNICALL* get_static)(JNIEnv*,jclass,jfieldID)=nullptr;
 void(JNICALL* set)(JNIEnv*,jobject,jfieldID,jint)=nullptr;
 void(JNICALL* set_static)(JNIEnv*,jclass,jfieldID,jint)=nullptr;
 void Clear(JNIEnv* env){if(instance)env->DeleteGlobalRef(instance);if(owner)env->DeleteGlobalRef(owner);*this={};}
};
Cache cache;std::mutex lock;
void Error(JNIEnv* env,const char* text){if(env->ExceptionCheck())return;jclass cls=env->FindClass("java/lang/IllegalStateException");if(cls){env->ThrowNew(cls,text);env->DeleteLocalRef(cls);}}
struct Utf {JNIEnv* env;jstring value;const char* chars;Utf(JNIEnv* e,jstring v):env(e),value(v),chars(v?e->GetStringUTFChars(v,nullptr):nullptr){}~Utf(){if(chars)env->ReleaseStringUTFChars(value,chars);}};
bool Current(JNIEnv* env){JavaVM* vm=nullptr;return cache.vm&&env->GetJavaVM(&vm)==JNI_OK&&vm==cache.vm;}
}
extern "C" JNIEXPORT void JNICALL Java_com_github_ytlog_artdcevm_testing_ProductionPermissionNative_retain(JNIEnv* env,jclass,jclass owner,jobject instance,jstring instance_name,jstring static_name){
 std::lock_guard<std::mutex> guard(lock);if(cache.owner){Error(env,"Permission probe already owns a cache; release it first");return;}
 Cache next;Utf field(env,instance_name),statik(env,static_name);
 if(!owner||!instance||!field.chars||!statik.chars||env->ExceptionCheck()||env->GetJavaVM(&next.vm)!=JNI_OK){Error(env,"Permission probe lacks actual JNI VM/owner/instance/names");return;}
 next.field=env->GetFieldID(owner,field.chars,"I");
 if(next.field&&!env->ExceptionCheck())next.static_field=env->GetStaticFieldID(owner,statik.chars,"I");
 if(!next.field||!next.static_field||env->ExceptionCheck())return;
 next.owner=static_cast<jclass>(env->NewGlobalRef(owner));
 if(next.owner&&!env->ExceptionCheck())next.instance=env->NewGlobalRef(instance);
 if(!next.owner||!next.instance||env->ExceptionCheck()){next.Clear(env);return;}
 next.get=env->functions->GetIntField;next.get_static=env->functions->GetStaticIntField;
 next.set=env->functions->SetIntField;next.set_static=env->functions->SetStaticIntField;
 cache=next;
}
extern "C" JNIEXPORT jint JNICALL Java_com_github_ytlog_artdcevm_testing_ProductionPermissionNative_read(JNIEnv* env,jclass,jboolean statik){
 std::lock_guard<std::mutex> guard(lock);if(!Current(env)){Error(env,"Permission probe cache does not belong to current VM");return 0;}
 return statik?cache.get_static(env,cache.owner,cache.static_field):cache.get(env,cache.instance,cache.field);
}
extern "C" JNIEXPORT void JNICALL Java_com_github_ytlog_artdcevm_testing_ProductionPermissionNative_write(JNIEnv* env,jclass,jboolean statik,jint value){
 std::lock_guard<std::mutex> guard(lock);if(!Current(env)){Error(env,"Permission probe cache does not belong to current VM");return;}
 if(statik)cache.set_static(env,cache.owner,cache.static_field,value);else cache.set(env,cache.instance,cache.field,value);
}
extern "C" JNIEXPORT void JNICALL Java_com_github_ytlog_artdcevm_testing_ProductionPermissionNative_release(JNIEnv* env,jclass){std::lock_guard<std::mutex> guard(lock);cache.Clear(env);}
