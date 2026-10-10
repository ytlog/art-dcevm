#include "platform/common/native_final_field_calibration.h"
#include <cassert>
#include <cstdarg>
#include <cstring>
#include <iostream>
using namespace artdcevm;
namespace {
struct Field {uint32_t flags;int value;void* reference=nullptr;};Field fields[4]={{1,41},{9,41},{1,0},{9,0}};
int native_method,target,writer,instance,handles,illegal,wrong,thread;int mode=0,callbacks=0,native_callbacks=0,restores=0,scopes=0;bool ready=false;
jthrowable pending=nullptr;final_field_service::Service* service=nullptr;
template<class T> T Ref(void* p){return reinterpret_cast<T>(p);}
bool ReadField(void*,void* raw,uint32_t* flags,void** owner){*flags=static_cast<Field*>(raw)->flags;*owner=&target;return true;}
bool ReadMethod(void*,void*,void** owner){*owner=&writer;return true;}
bool Same(void*,void* a,void* b){return a==b;}
void Raise(void*,void*,const char*,const char*){pending=Ref<jthrowable>(&illegal);}
bool MethodFlags(void*,void* method,uint32_t* flags){*flags=method==&native_method?0x100u:0u;return true;}
final_write_guard::Guard guard({&target,ReadField,ReadMethod,Same,Raise,nullptr,MethodFlags});
jclass JNICALL Find(JNIEnv*,const char* name){return Ref<jclass>(!std::strcmp(name,"java/lang/IllegalAccessError")?&illegal:std::strstr(name,"$Target")?&target:&writer);}
jobject JNICALL Global(JNIEnv*,jobject r){return r;}void JNICALL Delete(JNIEnv*,jobject){}
jmethodID JNICALL Method(JNIEnv*,jclass,const char* name,const char*){return Ref<jmethodID>(!std::strcmp(name,"create")?&instance:!std::strcmp(name,"prepareHandles")?&handles:!std::strcmp(name,"write")?&writer:!std::strcmp(name,"checkReferenceHandles")?&wrong:&target);}
jfieldID JNICALL InstanceField(JNIEnv*,jclass,const char* name,const char*){return Ref<jfieldID>(&fields[std::strstr(name,"Reference")?2:0]);}
jfieldID JNICALL StaticField(JNIEnv*,jclass,const char* name,const char*){return Ref<jfieldID>(&fields[std::strstr(name,"Reference")?3:1]);}
jobject JNICALL Object(JNIEnv*,jclass,jmethodID method,va_list){return method==Ref<jmethodID>(&instance)?Ref<jobject>(&instance):Ref<jobject>(&handles);}
void JNICALL JavaWrite(JNIEnv*,jclass,jmethodID,va_list args){(void)va_arg(args,jobject);int value=va_arg(args,int);jobject object=va_arg(args,jobject);bool statik=va_arg(args,int);bool reference=va_arg(args,int);auto& f=fields[unsigned(statik)+(reference?2u:0u)];
 if(f.flags&0x10u){if(mode!=1&&!(mode==6&&reference)){++callbacks;guard.FieldWritten(&thread,mode==9?&native_method:&writer,&f,reference&&mode!=7);}else pending=Ref<jthrowable>(&illegal);
  if(mode==2)pending=Ref<jthrowable>(&wrong);
  if(mode==3){f.value=value;f.reference=object;}
 }
 if(!pending){f.value=value;f.reference=object;}
}
jint JNICALL Paths(JNIEnv*,jclass,jmethodID method,va_list args){
 // The Java handle oracle may run unrelated original ART bytecode after the
 // first isolated proof. It must not acquire whole-process final enforcement.
 Field unrelated{0x10u,17};
 assert(!guard.FieldWritten(&thread,&writer,&unrelated));assert(!pending);
(void)va_arg(args,jobject);(void)va_arg(args,jobject);bool statik=va_arg(args,int);if(method==Ref<jmethodID>(&wrong)){(void)va_arg(args,jobject);fields[unsigned(statik)+2].reference=&handles;}else{int value=va_arg(args,int);fields[statik].value=value+1;}return mode==5?31:63;}
jint JNICALL Get(JNIEnv*,jobject,jfieldID field){return reinterpret_cast<Field*>(field)->value;}
jint JNICALL GetStatic(JNIEnv*,jclass,jfieldID field){return reinterpret_cast<Field*>(field)->value;}
void JNICALL Set(JNIEnv*,jobject,jfieldID field,jint value){if(reinterpret_cast<Field*>(field)->flags&0x10u)++native_callbacks;if(guard.FieldWritten(&thread,&native_method,field,false))return;assert(!pending);if(mode!=4||!(reinterpret_cast<Field*>(field)->flags&0x10u))reinterpret_cast<Field*>(field)->value=value;}
void JNICALL SetStatic(JNIEnv*,jclass,jfieldID field,jint value){Set(nullptr,nullptr,field,value);}
jobject JNICALL GetObject(JNIEnv*,jobject,jfieldID field){return Ref<jobject>(reinterpret_cast<Field*>(field)->reference);}
jobject JNICALL GetStaticObject(JNIEnv*,jclass,jfieldID field){return GetObject(nullptr,nullptr,field);}
void JNICALL SetObject(JNIEnv*,jobject,jfieldID field,jobject object){if(reinterpret_cast<Field*>(field)->flags&0x10u)++native_callbacks;if(guard.FieldWritten(&thread,&native_method,field,true))return;assert(!pending);if(mode!=8||!(reinterpret_cast<Field*>(field)->flags&0x10u))reinterpret_cast<Field*>(field)->reference=object;}
void JNICALL SetStaticObject(JNIEnv*,jclass,jfieldID field,jobject object){SetObject(nullptr,nullptr,field,object);}
jboolean JNICALL SameObject(JNIEnv*,jobject a,jobject b){return a==b;}
jboolean JNICALL Check(JNIEnv*){return pending!=nullptr;}jthrowable JNICALL Exception(JNIEnv*){return pending;}
void JNICALL Clear(JNIEnv*){pending=nullptr;}jint JNICALL Throw(JNIEnv*,jthrowable value){pending=value;return JNI_OK;}
jboolean JNICALL Is(JNIEnv*,jobject object,jclass klass){return object==klass;}
bool Scope(void*,JNIEnv* env,jobject root,runnable_jni::Operation operation,void* data){assert(root==Ref<jobject>(&instance));++scopes;return operation({&thread,env,&target},data);}
struct Engine:HookEngine {bool Initialize(std::string*)override{return true;}void* FindSymbol(const char*,const char*)override{return nullptr;}void* Install(void*,void*,void**,std::string*)override{return nullptr;}bool Remove(void*,std::string*)override{return false;}} engine;
}
namespace artdcevm::final_field_service {
struct Service::State {};
Service::Service(Bindings):state_(new State){}Service::~Service()=default;
bool Service::StageFixtureField(JNIEnv*,jclass owner,jfieldID id,bool statik,std::string*){return owner==Ref<jclass>(&target)&&(id==Ref<jfieldID>(&fields[statik])||id==Ref<jfieldID>(&fields[unsigned(statik)+2]));}
bool Service::PrepareEnvironment(JNIEnv*,std::string*){return true;}
bool Service::PrepareProofMethod(jmethodID method,std::string*){return method==Ref<jmethodID>(&writer);}
bool Service::RegisterStrong(JNIEnv*,jobject ref,std::string*){return ref!=nullptr;}
void Service::ForgetStrong(jobject){}
bool Service::Bind(HookEngine&,void* actual,std::string*){return actual==&target&&guard.BeginCalibration();}
bool Service::InstallAtExclusiveBoundary(std::string*){return true;}
bool Service::FixtureField(void* actual,jclass t,jclass w,jmethodID m,jfieldID f,bool statik,final_write_guard::Oracle* out,uint32_t* flags,std::string*)const{
 assert(actual==&thread&&t==Ref<jclass>(&target)&&w==Ref<jclass>(&writer)&&m==Ref<jmethodID>(&writer)&&(f==Ref<jfieldID>(&fields[statik])||f==Ref<jfieldID>(&fields[unsigned(statik)+2])));auto* field=reinterpret_cast<Field*>(f);*out={actual,&writer,field,field>=fields+2};*flags=field->flags;return true;}
bool Service::SetFixtureFlags(const final_write_guard::Oracle& o,uint32_t expected,uint32_t desired,std::string*){auto& f=*static_cast<Field*>(o.field);assert(f.flags==expected&&(expected^desired)==0x10u);f.flags=desired;if(!(desired&0x10u))++restores;return true;}
bool Service::BeginProof(const final_write_guard::Oracle& oracle){return guard.BeginProof(oracle);}
bool Service::EndProof(bool exception,bool unchanged){return guard.EndProof(exception,unchanged);}
bool Service::AlreadyCalibrated()const{return false;}
void Service::AbortCalibration(){guard.ResetUncommittedProof();ready=false;}
bool Service::CompleteCalibration(){ready=guard.Proven()&&guard.CompleteCalibration();return ready;}
bool Service::RefreshPlan(const source_commit::Plan&,std::string*)const{return false;}
bool Service::VerifyAliases(const source_commit::Plan&,std::string*)const{return false;}
bool Service::Verify(const source_commit::Plan&,std::string*)const{return false;}
bool Service::Verify(const source_commit::Plan&,size_t,void*,const evolution::ClassSchema&,const evolution::ClassSchema&,const native_schema_policy::Requirement&,std::string*)const{return false;}
void Service::BeginCommit(const source_commit::Plan&)const{}
}
int main(int argc,char** argv){assert(argc==2);mode=argv[1][0]-'0';
 JNINativeInterface_ functions{};functions.FindClass=Find;functions.NewGlobalRef=Global;functions.DeleteGlobalRef=Delete;functions.DeleteLocalRef=Delete;
 functions.GetStaticMethodID=Method;functions.GetFieldID=InstanceField;functions.GetStaticFieldID=StaticField;functions.CallStaticObjectMethodV=Object;functions.CallStaticVoidMethodV=JavaWrite;functions.CallStaticIntMethodV=Paths;
 functions.GetIntField=Get;functions.GetStaticIntField=GetStatic;functions.SetIntField=Set;functions.SetStaticIntField=SetStatic;
 functions.GetObjectField=GetObject;functions.GetStaticObjectField=GetStaticObject;functions.SetObjectField=SetObject;functions.SetStaticObjectField=SetStaticObject;functions.IsSameObject=SameObject;
 functions.ExceptionCheck=Check;functions.ExceptionOccurred=Exception;functions.ExceptionClear=Clear;functions.Throw=Throw;functions.IsInstanceOf=Is;
 JNIEnv env{&functions};final_field_service::Service owner({});service=&owner;std::string error;
 final_field_calibration::Bindings bindings{&target,&owner,&engine,&target,Scope,Scope};
 bool pass=final_field_calibration::Run(&env,bindings,&error);
 assert(pass==(mode==0)&&ready==pass&&guard.Proven()==pass);assert(fields[0].flags==1&&fields[1].flags==9);assert(restores==(mode==0?4:mode>=6&&mode!=9?3:1));assert(fields[2].flags==1&&fields[3].flags==9);
 if(mode==0)assert(native_callbacks==4&&callbacks==4&&fields[0].value==41&&fields[1].value==41);
 if(mode==2)assert(pending==Ref<jthrowable>(&wrong));else assert(!pending);
 assert(scopes>=4);std::cout<<"Actual permission calibration TU: mode "<<mode<<" rejects missing event/exception/store/legal-path proof and restores exact flags passed\n";
}
