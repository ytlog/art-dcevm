#include "platform/common/native_receiver_jni_fixture.h"
#include <array>
#include <cassert>
#include <cstdarg>
#include <cstring>
#include <iostream>
#include <map>
#include <thread>
#include <atomic>
using namespace artdcevm;
namespace {
JNIEnv env{};JavaVM vm{},foreign{};int fixture,base,class_type,loader,other_loader,method_id,field_id,loader_id,receiver,illegal_class,original_error,rollback_error;
bool pending=false,inside=false,fail_register=false,fail_unregister=false,fail_global=false,foreign_vm=false,wrong_loader=false,reenter=false;
jthrowable exception=nullptr;std::map<jobject,unsigned> roots;unsigned registrations=0,unregistrations=0,body=0,writes=0,reads=0;int value=71;std::array<void*,4> natives{};
receiver_guard::JniFixtureEntries* live=nullptr;std::atomic<bool> block_call{false},entered_call{false},release_call{false};
void Public(){assert(!inside);}
jobject Global(JNIEnv*,jobject r){Public();if(fail_global&&r==reinterpret_cast<jobject>(&base)){pending=true;exception=reinterpret_cast<jthrowable>(&original_error);return r;}++roots[r];return r;}
void DeleteGlobal(JNIEnv*,jobject r){Public();if(fail_global&&r==reinterpret_cast<jobject>(&base))return;auto i=roots.find(r);assert(i!=roots.end()&&i->second);if(!--i->second)roots.erase(i);}
jint Register(JNIEnv*,jclass c,const JNINativeMethod*entries,jint count){Public();assert(c==reinterpret_cast<jclass>(&fixture)&&count==4);++registrations;for(int i=0;i<4;++i)natives[size_t(i)]=entries[i].fnPtr;if(fail_register){pending=true;exception=reinterpret_cast<jthrowable>(&original_error);return JNI_ERR;}return JNI_OK;}
jint Unregister(JNIEnv*,jclass c){Public();assert(c==reinterpret_cast<jclass>(&fixture));++unregistrations;if(fail_unregister){pending=true;exception=reinterpret_cast<jthrowable>(&rollback_error);return JNI_ERR;}natives={};return JNI_OK;}
jint Invoke(JNIEnv*e,jobject o,jmethodID m,va_list){Public();assert(o==reinterpret_cast<jobject>(&receiver)&&m==reinterpret_cast<jmethodID>(&method_id));++body;
 if(block_call.load()){entered_call.store(true);while(!release_call.load())std::this_thread::yield();}
 if(reenter){std::string error;assert(live&&!live->Unregister(e,&error)&&live->Registered());}return 113;
}
jint Nonvirtual(JNIEnv*e,jobject o,jclass c,jmethodID m,va_list args){Public();assert(c==reinterpret_cast<jclass>(&base));return Invoke(e,o,m,args);}
jint Read(JNIEnv*,jobject o,jfieldID f){Public();assert(o==reinterpret_cast<jobject>(&receiver)&&f==reinterpret_cast<jfieldID>(&field_id));++reads;return value;}
void Write(JNIEnv*,jobject o,jfieldID f,jint v){Public();assert(o==reinterpret_cast<jobject>(&receiver)&&f==reinterpret_cast<jfieldID>(&field_id));++writes;value=v;}
void Clear(){pending=false;exception=nullptr;}
void Run(){assert(natives[0]&&natives[1]&&natives[2]&&natives[3]);auto o=reinterpret_cast<jobject>(&receiver);auto c=reinterpret_cast<jclass>(&fixture);
 assert(reinterpret_cast<jint(*)(JNIEnv*,jclass,jobject)>(natives[0])(&env,c,o)==113);
 assert(reinterpret_cast<jint(*)(JNIEnv*,jclass,jobject)>(natives[1])(&env,c,o)==value);
 reinterpret_cast<void(*)(JNIEnv*,jclass,jobject,jint)>(natives[2])(&env,c,o,89);assert(value==89);
 assert(reinterpret_cast<jint(*)(JNIEnv*,jclass,jobject)>(natives[3])(&env,c,o)==113);
}
}
int main(){
 JNINativeInterface_ table{};env.functions=&table;
 table.ExceptionCheck=[](JNIEnv*){Public();return jboolean(pending);};table.ExceptionOccurred=[](JNIEnv*){Public();return exception;};table.ExceptionClear=[](JNIEnv*){Public();Clear();};table.Throw=[](JNIEnv*,jthrowable e){Public();pending=true;exception=e;return JNI_OK;};
 table.GetJavaVM=[](JNIEnv*,JavaVM**out){Public();*out=foreign_vm?&foreign:&vm;return JNI_OK;};table.GetObjectClass=[](JNIEnv*,jobject){Public();return reinterpret_cast<jclass>(&class_type);};
 table.GetMethodID=[](JNIEnv*,jclass c,const char*n,const char*s){Public();if(!std::strcmp(n,"getClassLoader")){assert(!std::strcmp(s,"()Ljava/lang/ClassLoader;"));return reinterpret_cast<jmethodID>(&loader_id);}assert(c==reinterpret_cast<jclass>(&base)&&!std::strcmp(n,"body")&&!std::strcmp(s,"()I"));return reinterpret_cast<jmethodID>(&method_id);};
 table.GetFieldID=[](JNIEnv*,jclass c,const char*n,const char*s){Public();assert(c==reinterpret_cast<jclass>(&base)&&!std::strcmp(n,"value")&&!std::strcmp(s,"I"));return reinterpret_cast<jfieldID>(&field_id);};
 table.CallObjectMethodV=[](JNIEnv*,jobject c,jmethodID m,va_list){Public();assert(m==reinterpret_cast<jmethodID>(&loader_id));return reinterpret_cast<jobject>(wrong_loader&&c==reinterpret_cast<jobject>(&base)?&other_loader:&loader);};table.IsSameObject=[](JNIEnv*,jobject a,jobject b){Public();return jboolean(a==b);};
 table.NewGlobalRef=Global;table.DeleteGlobalRef=DeleteGlobal;table.DeleteLocalRef=[](JNIEnv*,jobject){Public();};table.RegisterNatives=Register;table.UnregisterNatives=Unregister;table.CallIntMethodV=Invoke;table.CallNonvirtualIntMethodV=Nonvirtual;table.GetIntField=Read;table.SetIntField=Write;
 table.FindClass=[](JNIEnv*,const char*n){Public();assert(!std::strcmp(n,"java/lang/IllegalStateException"));return reinterpret_cast<jclass>(&illegal_class);};table.ThrowNew=[](JNIEnv*,jclass,const char*){Public();pending=true;exception=reinterpret_cast<jthrowable>(&original_error);return JNI_OK;};
 receiver_guard::JniFixtureEntries entries;live=&entries;std::string error;auto fixture_class=reinterpret_cast<jclass>(&fixture),base_class=reinterpret_cast<jclass>(&base);
 pending=true;exception=reinterpret_cast<jthrowable>(&original_error);assert(!entries.Register(&env,fixture_class,base_class,&error)&&pending&&roots.empty());Clear();
 wrong_loader=true;assert(!entries.Register(&env,fixture_class,base_class,&error)&&roots.empty());wrong_loader=false;
 fail_global=true;assert(!entries.Register(&env,fixture_class,base_class,&error)&&pending&&exception==reinterpret_cast<jthrowable>(&original_error)&&roots.empty());Clear();fail_global=false;
 fail_register=true;assert(!entries.Register(&env,fixture_class,base_class,&error)&&pending&&exception==reinterpret_cast<jthrowable>(&original_error)&&!entries.Registered()&&roots.empty()&&unregistrations==1);Clear();fail_register=false;
 // Actual prefix rollback failure retains the exact native owner + root set.
 fail_register=true;fail_unregister=true;assert(!entries.Register(&env,fixture_class,base_class,&error)&&pending&&exception==reinterpret_cast<jthrowable>(&original_error)&&!entries.Registered()&&entries.Owned()&&roots.size()==2);Clear();fail_register=false;fail_unregister=false;assert(entries.Unregister(&env,&error)&&roots.empty());
 assert(entries.Register(&env,fixture_class,base_class,&error)&&entries.Registered()&&roots.size()==2);
 receiver_guard::JniFixtureEntries foreign_owner;assert(!foreign_owner.Register(&env,fixture_class,base_class,&error)&&entries.Registered()&&roots.size()==2);
 reenter=true;Run();reenter=false;assert(body==2&&reads==1&&writes==1&&entries.Registered());
 // Reader acquisition and owner retirement share one mutex. An actual second
 // callback thread keeps its roots alive while teardown refuses immediately.
 block_call=true;JNIEnv callback_env{env.functions};auto callback=natives[0];
 std::thread caller([&]{assert(reinterpret_cast<jint(*)(JNIEnv*,jclass,jobject)>(callback)(&callback_env,fixture_class,reinterpret_cast<jobject>(&receiver))==113);});
 while(!entered_call.load())std::this_thread::yield();assert(!entries.Unregister(&env,&error)&&entries.Registered()&&entries.Owned()&&roots.size()==2);release_call=true;caller.join();block_call=false;
 auto saved=natives;pending=true;exception=reinterpret_cast<jthrowable>(&original_error);auto before=body;assert(reinterpret_cast<jint(*)(JNIEnv*,jclass,jobject)>(natives[0])(&env,fixture_class,reinterpret_cast<jobject>(&receiver))==0&&body==before&&pending);assert(!entries.Unregister(&env,&error)&&roots.size()==2);Clear();
 foreign_vm=true;assert(!entries.Unregister(&env,&error)&&roots.size()==2);assert(reinterpret_cast<jint(*)(JNIEnv*,jclass,jobject)>(natives[0])(&env,fixture_class,reinterpret_cast<jobject>(&receiver))==0&&body==before&&pending);Clear();foreign_vm=false;
 fail_unregister=true;assert(!entries.Unregister(&env,&error)&&pending&&entries.Registered()&&roots.size()==2);Clear();fail_unregister=false;
 assert(entries.Unregister(&env,&error)&&roots.empty()&&!entries.Registered()&&!entries.Owned());
 assert(reinterpret_cast<jint(*)(JNIEnv*,jclass,jobject)>(saved[0])(&env,fixture_class,reinterpret_cast<jobject>(&receiver))==0&&pending&&body==before);Clear();assert(!entries.Unregister(&env,&error));
 assert(entries.Register(&env,fixture_class,base_class,&error));assert(entries.Unregister(&env,&error)&&roots.empty());
 std::cout<<"Actual registered JNI callbacks + original cached Method/Field JNI operations: same-loader/Class roots, original pending preservation, partial-register rollback, rollback/unregister failure root retention, VM refusal, in-flight teardown refusal and complete teardown PASS; no Guard coverage/ICCE mocked"<<std::endl;
}
