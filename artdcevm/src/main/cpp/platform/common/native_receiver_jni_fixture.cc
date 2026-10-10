#include "platform/common/native_receiver_jni_fixture.h"
#include <mutex>
#include <cstdlib>
namespace artdcevm::receiver_guard {
namespace {
std::mutex registry_lock;JniFixtureEntries* owner=nullptr;
constexpr char kReceiver[]="(Lcom/github/ytlog/artdcevm/internal/ReceiverGuardFixture$Base;)I";
constexpr char kWrite[]="(Lcom/github/ytlog/artdcevm/internal/ReceiverGuardFixture$Base;I)V";
bool Fail(std::string*e,const char*t){if(e)*e=t;return false;}
struct Local {JNIEnv*env;jobject value=nullptr;~Local(){if(value)env->DeleteLocalRef(value);}};
// RegisterNatives can install a prefix before failing. Roll it back through the
// actual JNI API with the original exception rooted, then restore that exception.
// No public operation except exception/reference cleanup runs while pending.
struct Exception final {
 JNIEnv*env;jthrowable original=nullptr;
 explicit Exception(JNIEnv*e):env(e){if(env->ExceptionCheck()){original=env->ExceptionOccurred();if(original)env->ExceptionClear();}}
 ~Exception(){if(original){if(env->ExceptionCheck())env->ExceptionClear();env->Throw(original);env->DeleteLocalRef(original);}}
};
void Unavailable(JNIEnv*env){if(!env||env->ExceptionCheck())return;Local klass{env,env->FindClass("java/lang/IllegalStateException")};if(klass.value&&!env->ExceptionCheck())env->ThrowNew(static_cast<jclass>(klass.value),"Receiver fixture JNI entry has no live registered owner");}
}
struct JniFixtureEntries::Reader final {
 JniFixtureEntries*service=nullptr;JavaVM*vm=nullptr;jclass base=nullptr;jmethodID method=nullptr;jfieldID field=nullptr;
 Reader(){std::lock_guard<std::mutex> lock(registry_lock);if(owner&&owner->phase_==Phase::Registered){service=owner;++service->readers_;vm=service->vm_;base=service->base_;method=service->method_;field=service->field_;}}
 ~Reader(){if(service){std::lock_guard<std::mutex> lock(registry_lock);if(!service->readers_)std::abort();--service->readers_;}}
 bool Actual(JNIEnv*env)const{if(!service||!env||env->ExceptionCheck())return false;JavaVM*actual=nullptr;return env->GetJavaVM(&actual)==JNI_OK&&actual==vm&&!env->ExceptionCheck();}
};
JniFixtureEntries::~JniFixtureEntries(){std::lock_guard<std::mutex>lock(registry_lock);if(phase_!=Phase::Idle||readers_||owner==this)std::abort();}
bool JniFixtureEntries::Owned()const noexcept{std::lock_guard<std::mutex>lock(registry_lock);return owner==this&&phase_!=Phase::Idle;}
bool JniFixtureEntries::Registered()const noexcept{std::lock_guard<std::mutex>lock(registry_lock);return owner==this&&phase_==Phase::Registered;}
jint JniFixtureEntries::Method(JNIEnv*env,jclass,jobject receiver){Reader reader;if(!reader.Actual(env)){Unavailable(env);return 0;}return env->CallIntMethod(receiver,reader.method);}
jint JniFixtureEntries::Read(JNIEnv*env,jclass,jobject receiver){Reader reader;if(!reader.Actual(env)){Unavailable(env);return 0;}return env->GetIntField(receiver,reader.field);}
void JniFixtureEntries::Write(JNIEnv*env,jclass,jobject receiver,jint value){Reader reader;if(!reader.Actual(env)){Unavailable(env);return;}env->SetIntField(receiver,reader.field,value);}
jint JniFixtureEntries::Invoke(JNIEnv*env,jclass,jobject receiver){Reader reader;if(!reader.Actual(env)){Unavailable(env);return 0;}
 // Real original JNI nonvirtual dispatch reaches ART's original invocation
 // machinery. NativeInvoke coverage still requires its actual recorded source
 // event; neither this call nor its successful result can grant coverage.
 return env->CallNonvirtualIntMethod(receiver,reader.base,reader.method);
}
bool JniFixtureEntries::Register(JNIEnv*env,jclass fixture,jclass base,std::string*e){
 if(!env||!fixture||!base||env->ExceptionCheck())return Fail(e,"Receiver fixture registration requires ordinary JNI and dedicated live Classes");
 JavaVM*vm=nullptr;if(env->GetJavaVM(&vm)!=JNI_OK||!vm||env->ExceptionCheck())return Fail(e,"Receiver fixture registration lacks actual public VM");
 // Independently establish that both Classes have the same actual dedicated
 // defining loader. A Class global keeps that loader alive after local teardown.
 Local class_type{env,env->GetObjectClass(fixture)};if(!class_type.value||env->ExceptionCheck())return false;
 auto loader_id=env->GetMethodID(static_cast<jclass>(class_type.value),"getClassLoader","()Ljava/lang/ClassLoader;");if(!loader_id||env->ExceptionCheck())return false;
 Local fixture_loader{env,env->CallObjectMethod(fixture,loader_id)};if(!fixture_loader.value||env->ExceptionCheck())return false;
 Local base_loader{env,env->CallObjectMethod(base,loader_id)};if(!base_loader.value||env->ExceptionCheck()||!env->IsSameObject(fixture_loader.value,base_loader.value)||env->ExceptionCheck())return Fail(e,"Receiver fixture/Base defining loaders differ or are absent");
 auto method=env->GetMethodID(base,"body","()I");if(!method||env->ExceptionCheck())return false;
 auto field=env->GetFieldID(base,"value","I");if(!field||env->ExceptionCheck())return false;
 jclass fixture_root=static_cast<jclass>(env->NewGlobalRef(fixture));
 if(!fixture_root||env->ExceptionCheck()){if(fixture_root)env->DeleteGlobalRef(fixture_root);return false;}
 jclass base_root=static_cast<jclass>(env->NewGlobalRef(base));
 if(!base_root||env->ExceptionCheck()){if(base_root)env->DeleteGlobalRef(base_root);env->DeleteGlobalRef(fixture_root);return false;}
 bool reserved=false;
 {std::lock_guard<std::mutex>lock(registry_lock);if(!owner&&phase_==Phase::Idle){owner=this;vm_=vm;fixture_=fixture_root;base_=base_root;method_=method;field_=field;phase_=Phase::Installing;reserved=true;}}
 if(!reserved){env->DeleteGlobalRef(base_root);env->DeleteGlobalRef(fixture_root);return Fail(e,"Receiver fixture entry already has a process owner");}
 JNINativeMethod entries[]={{const_cast<char*>("jniMethod"),const_cast<char*>(kReceiver),reinterpret_cast<void*>(Method)}, {const_cast<char*>("jniRead"),const_cast<char*>(kReceiver),reinterpret_cast<void*>(Read)}, {const_cast<char*>("jniWrite"),const_cast<char*>(kWrite),reinterpret_cast<void*>(Write)}, {const_cast<char*>("nativeInvoke"),const_cast<char*>(kReceiver),reinterpret_cast<void*>(Invoke)}};
 const auto registered=env->RegisterNatives(fixture_root,entries,4);
 if(registered!=JNI_OK||env->ExceptionCheck()){
  // Installing readers never borrow cached members; detach must retire even a
  // partially installed prefix, preserving the original pending exception.
  Exception preserve(env);if(!Detach(env,e))return Fail(e,"Receiver fixture registration rollback failed; native owner/roots retained");
  return Fail(e,"Receiver fixture RegisterNatives failed and actual registration rolled back");
 }
 {std::lock_guard<std::mutex>lock(registry_lock);if(owner!=this||phase_!=Phase::Installing)std::abort();phase_=Phase::Registered;}
 return true;
}
bool JniFixtureEntries::Detach(JNIEnv*env,std::string*e){
 Phase prior; jclass fixture=nullptr;
 {std::lock_guard<std::mutex>lock(registry_lock);if(owner!=this||phase_==Phase::Idle||phase_==Phase::Retiring||readers_)return Fail(e,"Receiver fixture teardown requires exact owner and no in-flight calls");prior=phase_;phase_=Phase::Retiring;fixture=fixture_;}
 auto result=env->UnregisterNatives(fixture);
 if(result!=JNI_OK||env->ExceptionCheck()){std::lock_guard<std::mutex>lock(registry_lock);phase_=prior==Phase::Installing?Phase::Broken:prior;return Fail(e,"Receiver fixture UnregisterNatives failed; owner and Class roots retained");}
 jclass base=nullptr;
 {std::lock_guard<std::mutex>lock(registry_lock);if(owner!=this||phase_!=Phase::Retiring||readers_)std::abort();owner=nullptr;base=base_;base_=fixture_=nullptr;vm_=nullptr;method_=nullptr;field_=nullptr;phase_=Phase::Idle;}
 env->DeleteGlobalRef(base);env->DeleteGlobalRef(fixture);return true;
}
bool JniFixtureEntries::Unregister(JNIEnv*env,std::string*e){
 if(!env||env->ExceptionCheck())return Fail(e,"Receiver fixture teardown requires ordinary JNI without pending exception");
 JavaVM*vm=nullptr;if(env->GetJavaVM(&vm)!=JNI_OK||env->ExceptionCheck())return false;
 {std::lock_guard<std::mutex>lock(registry_lock);if(owner!=this||phase_==Phase::Idle||vm_!=vm)return Fail(e,"Receiver fixture teardown VM/owner differs");}
 return Detach(env,e);
}
}
