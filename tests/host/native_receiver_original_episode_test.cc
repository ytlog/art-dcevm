#include "receiver_original_jni_fixture.inc"
#include "platform/common/native_receiver_original_episode.h"
#include <vector>
namespace {
int instance,receiver_type,superclass_id,execute_id,counters_id,receiver_id,write_id;
std::map<jobject,unsigned> local_refs;std::map<jobject,std::array<jlong,2>> arrays;
unsigned steps=0,fail_step=0;std::vector<unsigned>nullable_steps;bool pending_failure=true;int fail_path=-1,corrupt_path=-1;unsigned current_path=99;unsigned executions=0;
jlong sentinel=0x1020304050607080LL;
bool Step(bool nullable=false){assert(!pending);++steps;if(nullable&&fail_step==0)nullable_steps.push_back(steps);if(steps==fail_step){if(pending_failure){pending=true;exception=reinterpret_cast<jthrowable>(&original_error);}return true;}return false;}
jobject LocalRef(jobject object){if(object)++local_refs[object];return object;}
void LocalDrop(JNIEnv*,jobject object){auto at=local_refs.find(object);if(at!=local_refs.end()){assert(at->second);if(!--at->second)local_refs.erase(at);return;}auto a=arrays.find(object);if(a!=arrays.end()){arrays.erase(a);delete reinterpret_cast<int*>(object);return;}}
bool RejectNull(bool fail){return fail&&!pending_failure;}
jclass OriginalClass(JNIEnv*,jobject object){bool fail=Step(true);if(RejectNull(fail))return nullptr;auto*result=object==reinterpret_cast<jobject>(&instance)?&fixture:object==reinterpret_cast<jobject>(&receiver)?&receiver_type:&class_type;return static_cast<jclass>(LocalRef(reinterpret_cast<jobject>(result)));}
jmethodID OriginalMethod(JNIEnv*,jclass c,const char*n,const char*s){bool fail=Step(true);if(RejectNull(fail))return nullptr;int*id=nullptr;
 if(!std::strcmp(n,"getSuperclass")){assert(!std::strcmp(s,"()Ljava/lang/Class;"));id=&superclass_id;}
 else if(!std::strcmp(n,"execute")){assert(c==reinterpret_cast<jclass>(&fixture)&&!std::strcmp(s,"(I)I"));id=&execute_id;}
 else if(!std::strcmp(n,"getClassLoader")){assert(!std::strcmp(s,"()Ljava/lang/ClassLoader;"));id=&loader_id;}
 else{assert(c==reinterpret_cast<jclass>(&base)&&!std::strcmp(n,"body")&&!std::strcmp(s,"()I"));id=&method_id;}return reinterpret_cast<jmethodID>(id);
}
jfieldID OriginalField(JNIEnv*,jclass c,const char*n,const char*s){bool fail=Step(true);if(RejectNull(fail))return nullptr;int*id=nullptr;
 if(!std::strcmp(n,"receiver")){assert(c==reinterpret_cast<jclass>(&fixture)&&!std::strcmp(s,"Lcom/github/ytlog/artdcevm/internal/ReceiverGuardFixture$Receiver;"));id=&receiver_id;}
 else if(!std::strcmp(n,"writeValue")){assert(c==reinterpret_cast<jclass>(&fixture)&&!std::strcmp(s,"I"));id=&write_id;}
 else{assert(c==reinterpret_cast<jclass>(&base)&&!std::strcmp(n,"value")&&!std::strcmp(s,"I"));id=&field_id;}return reinterpret_cast<jfieldID>(id);
}
jobject OriginalObject(JNIEnv*,jobject object,jfieldID id){bool fail=Step(true);assert(object==reinterpret_cast<jobject>(&instance)&&id==reinterpret_cast<jfieldID>(&receiver_id));return RejectNull(fail)?nullptr:LocalRef(reinterpret_cast<jobject>(&receiver));}
jobject OriginalCallObject(JNIEnv*,jobject object,jmethodID id,va_list){bool fail=Step(true);if(RejectNull(fail))return nullptr;int*result=nullptr;
 if(id==reinterpret_cast<jmethodID>(&superclass_id)){assert(object==reinterpret_cast<jobject>(&receiver_type));result=&base;}else{assert(id==reinterpret_cast<jmethodID>(&loader_id));result=&loader;}return LocalRef(reinterpret_cast<jobject>(result));
}
jint OriginalInt(JNIEnv*e,jobject object,jfieldID id){bool fail=Step();if(fail&&!pending_failure)return -1;if(id==reinterpret_cast<jfieldID>(&write_id)){assert(object==reinterpret_cast<jobject>(&instance));return 89;}return Read(e,object,id);}
jint Execute(JNIEnv*e,jobject object,jmethodID id,va_list args){bool fail=Step();if(fail)return 0;if(id==reinterpret_cast<jmethodID>(&method_id))return Invoke(e,object,id,args);
 assert(id==reinterpret_cast<jmethodID>(&execute_id)&&object==reinterpret_cast<jobject>(&instance));int path=va_arg(args,jint);assert(path>=0&&path<16);current_path=unsigned(path);++executions;
 if(path==fail_path){pending=true;exception=reinterpret_cast<jthrowable>(&original_error);return 0;}
 const auto native_read=[&](unsigned at){return reinterpret_cast<jint(*)(JNIEnv*,jclass,jobject)>(natives[at])(e,reinterpret_cast<jclass>(&fixture),reinterpret_cast<jobject>(&receiver));};
 jint result=0;
 switch(path){case 0:case 1:case 7:case 10:case 13:++body;result=113;break;case 2:case 8:case 11:case 14:result=value;break;case 3:case 9:case 12:value=89;result=89;break;case 4:result=native_read(0);break;case 5:result=native_read(1);break;case 6:reinterpret_cast<void(*)(JNIEnv*,jclass,jobject,jint)>(natives[2])(e,reinterpret_cast<jclass>(&fixture),reinterpret_cast<jobject>(&receiver),89);result=89;break;case 15:result=native_read(3);break;default:std::abort();}
 if(path==corrupt_path)++sentinel;return result;
}
jobject Counters(JNIEnv*,jclass c,jmethodID id,va_list){bool fail=Step(true);assert(c==reinterpret_cast<jclass>(&fixture)&&id==reinterpret_cast<jmethodID>(&counters_id));if(RejectNull(fail))return nullptr;auto*object=new int;auto ref=reinterpret_cast<jobject>(object);arrays[ref]={jlong(body),sentinel};return ref;}
void Reset(){assert(!live->Owned()&&roots.empty()&&local_refs.empty()&&arrays.empty());Clear();steps=fail_step=0;pending_failure=true;fail_path=corrupt_path=-1;current_path=99;executions=0;body=reads=writes=0;value=71;sentinel=0x1020304050607080LL;}
}
int main(){
 JNINativeInterface_ table{};env.functions=&table;receiver_guard::JniFixtureEntries owner;live=&owner;std::string error;
 table.ExceptionCheck=[](JNIEnv*){return jboolean(pending);};table.ExceptionOccurred=[](JNIEnv*){return exception;};table.ExceptionClear=[](JNIEnv*){Clear();};table.Throw=[](JNIEnv*,jthrowable e){pending=true;exception=e;return JNI_OK;};
 table.GetJavaVM=[](JNIEnv*,JavaVM**out){bool fail=Step();*out=&vm;return fail&&!pending_failure?JNI_ERR:JNI_OK;};table.GetObjectClass=OriginalClass;table.GetMethodID=OriginalMethod;table.GetFieldID=OriginalField;table.GetObjectField=OriginalObject;table.CallObjectMethodV=OriginalCallObject;table.GetIntField=OriginalInt;table.CallIntMethodV=Execute;
 table.GetStaticMethodID=[](JNIEnv*,jclass c,const char*n,const char*s){bool fail=Step(true);assert(c==reinterpret_cast<jclass>(&fixture)&&!std::strcmp(n,"counters")&&!std::strcmp(s,"()[J"));return RejectNull(fail)?nullptr:reinterpret_cast<jmethodID>(&counters_id);};table.CallStaticObjectMethodV=Counters;
 table.GetArrayLength=[](JNIEnv*,jarray object){bool fail=Step();assert(arrays.count(object));return fail&&!pending_failure?-1:2;};table.GetLongArrayRegion=[](JNIEnv*,jlongArray object,jsize start,jsize n,jlong*out){bool fail=Step();assert(start==0&&n==2&&arrays.count(object));if(!fail||pending_failure){out[0]=arrays.at(object)[0];out[1]=arrays.at(object)[1];}else out[0]=-1;};
 table.NewGlobalRef=[](JNIEnv*e,jobject r){bool fail=Step(true);if(RejectNull(fail))return jobject(nullptr);return Global(e,r);};table.DeleteGlobalRef=DeleteGlobal;table.DeleteLocalRef=LocalDrop;table.IsSameObject=[](JNIEnv*,jobject a,jobject b){bool fail=Step();return jboolean(!RejectNull(fail)&&a==b);};table.RegisterNatives=Register;table.UnregisterNatives=Unregister;table.SetIntField=Write;table.CallNonvirtualIntMethodV=Nonvirtual;
 table.FindClass=[](JNIEnv*,const char*){return static_cast<jclass>(LocalRef(reinterpret_cast<jobject>(&illegal_class)));};table.ThrowNew=[](JNIEnv*,jclass,const char*){pending=true;exception=reinterpret_cast<jthrowable>(&original_error);return JNI_OK;};
 auto sample=reinterpret_cast<jobject>(&instance);auto ok=receiver_guard::RunOriginalReceiverEntries(&env,sample,owner,&error);assert(ok.passed&&ok.completed==16&&!owner.Owned()&&executions==16&&body==7&&value==89&&roots.empty()&&local_refs.empty()&&arrays.empty());const auto boundaries=steps;const auto null_boundaries=nullable_steps;Reset();
 // Every observed JNI boundary fails with its original exception intact.
 // A retained registrar must survive until caller handles and retires it.
 for(unsigned at=1;at<=boundaries;++at){fail_step=at;auto failed=receiver_guard::RunOriginalReceiverEntries(&env,sample,owner,&error);assert(!failed.passed&&pending&&exception==reinterpret_cast<jthrowable>(&original_error)&&local_refs.empty()&&arrays.empty());Clear();fail_step=0;if(owner.Owned())assert(owner.Unregister(&env,&error));Reset();}
 for(auto at:null_boundaries){fail_step=at;pending_failure=false;auto failed=receiver_guard::RunOriginalReceiverEntries(&env,sample,owner,&error);assert(!failed.passed&&!pending&&!owner.Owned()&&local_refs.empty()&&arrays.empty());Reset();}
 for(int path=0;path<16;++path){fail_path=path;auto failed=receiver_guard::RunOriginalReceiverEntries(&env,sample,owner,&error);assert(!failed.passed&&failed.completed==unsigned(path)&&pending&&owner.Owned()&&local_refs.empty()&&arrays.empty());Clear();assert(owner.Unregister(&env,&error));Reset();}
 for(int path=0;path<16;++path){corrupt_path=path;auto failed=receiver_guard::RunOriginalReceiverEntries(&env,sample,owner,&error);assert(!failed.passed&&failed.completed==unsigned(path)&&!pending&&!owner.Owned());Reset();}
 fail_unregister=true;auto teardown=receiver_guard::RunOriginalReceiverEntries(&env,sample,owner,&error);assert(!teardown.passed&&teardown.completed==16&&pending&&owner.Owned());Clear();fail_unregister=false;assert(owner.Unregister(&env,&error));Reset();
 pending=true;exception=reinterpret_cast<jthrowable>(&original_error);auto incoming=receiver_guard::RunOriginalReceiverEntries(&env,sample,owner,&error);assert(!incoming.passed&&incoming.completed==0&&steps==0&&pending);Clear();Reset();
 std::cout<<"Actual sixteen original public Java model entries + real registered cached JNI callbacks: independent field/body/sentinel readback, "<<boundaries<<" pending JNI boundary refusals plus "<<null_boundaries.size()<<" actual-null boundary refusals, all original path exceptions/sentinel corruption, retained owner until caller handles exception, teardown/root balance PASS; no Guard/ICCE/path-coverage grant"<<std::endl;
}
