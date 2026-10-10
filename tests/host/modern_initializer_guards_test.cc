#include "platform/common/modern_initializer_guards.cc"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <csetjmp>
using namespace artdcevm;
struct Member{bool constructor;};Member constructor_member{true},clinit_member{false},other_member{true};
struct Object{jclass klass;int value;};
int thread,other_thread,manager,class_storage,mirror_class_storage,object_class_storage,anchor_storage,context_storage,handle_storage,factory_storage,invoke_storage;
jclass fixture_class=reinterpret_cast<jclass>(&class_storage),mirror_class=reinterpret_cast<jclass>(&mirror_class_storage),object_class=reinterpret_cast<jclass>(&object_class_storage);
jobject mirror=reinterpret_cast<jobject>(&constructor_member),anchor=reinterpret_cast<jobject>(&anchor_storage);
Object fixture{fixture_class,static_cast<int>(0x81234567u)},allocated{fixture_class,static_cast<int>(0x81234567u)};
retirement::RetiredMethodRegistry registry;void*current_self=&thread;bool shared_owner=true,scope=true,wrong_leaf=false;
unsigned originals=0,allocations=0,throws=0,deliveries=0,globals=0,installs=0,stops=0;const char*exception=nullptr;bool early_delivery=false;std::jmp_buf escape;bool mh_ready=true,mh_active=false;unsigned mh_hits=0,mh_rejections=0;
namespace artdcevm::retirement {
bool MethodEntryGuardsOwn(const RetiredMethodRegistry&r){return shared_owner&&&r==&registry;}
bool MethodEntryGuardsOwn(const RetiredMethodRegistry&r,const void*m){return MethodEntryGuardsOwn(r)&&m==&manager;}
bool MethodHandleGuardsReady(const RetiredMethodRegistry&r){return mh_ready&&MethodEntryGuardsOwn(r);}
bool BeginConstructorMethodHandleTargetProof(JNIEnv*,jobject root,jmethodID id,std::string*){if(mh_active||root!=reinterpret_cast<jobject>(&handle_storage)||id!=reinterpret_cast<jmethodID>(&constructor_member))return false;mh_active=true;mh_hits=mh_rejections=0;return true;}
bool EndMethodHandleTargetProof(bool success,std::string*){const bool pass=success&&mh_active&&mh_hits&&!mh_rejections;mh_active=false;return pass;}
bool EndRetiredConstructorMethodHandleTargetProof(bool public_oracle,std::string*){const bool pass=public_oracle&&mh_active&&mh_rejections&&!mh_hits;mh_active=false;return pass;}
bool RejectRetiredMethod(void*m,void*self,void*result){assert(self==current_self);if(!registry.Contains(m))return false;if(result)std::memset(result,0,8);++throws;exception="Ljava/lang/NoSuchMethodError;";return true;}
}
namespace artdcevm::runnable_executor {
bool Available(){return true;}
runnable_jni::Result With(JNIEnv*env,jobject root,runnable_jni::Operation operation,void*data){assert(root==anchor);if(!scope)return runnable_jni::Result::kContextMismatch;runnable_jni::Context c{current_self,env,&manager};return operation(c,data)?runnable_jni::Result::kSuccess:runnable_jni::Result::kOperationRejected;}
}
void*Current(){return current_self;}void*Decode(void*owner,jmethodID id){assert(owner==&manager);return id;}
void*Reflected(const abi::common::RunnableJniScope&s,jobject ref){assert(s.self==current_self);return wrong_leaf?static_cast<void*>(&other_member):static_cast<void*>(ref);}
void*Deliver(void*self){assert(self==current_self);++deliveries;if(early_delivery)std::longjmp(escape,1);return &context_storage;}
void*ObsoleteOriginal(void*,void*self){assert(self==current_self);++originals;if(early_delivery)std::longjmp(escape,2);return &context_storage;}
bool CheckOriginal(void*,jmethodID){++originals;return true;}
jobject ConstructorOriginal(JNIEnv*,jobject,jobjectArray){++originals;++allocations;return reinterpret_cast<jobject>(&allocated);}
jobject NewAOriginal(JNIEnv*,jclass klass,jmethodID,const jvalue*){assert(klass==fixture_class);++originals;++allocations;return reinterpret_cast<jobject>(&allocated);}
jobject NewVOriginal(JNIEnv*,jclass klass,jmethodID,va_list){assert(klass==fixture_class);++originals;++allocations;return reinterpret_cast<jobject>(&allocated);}
void Stop(void*,const char*,bool){++stops;}void Resume(void*){--stops;}
class Hooks final:public HookEngine{public:bool Initialize(std::string*)override{return true;}const char*missing=nullptr;std::vector<void*>targets;
 void*FindSymbol(const char*,const char*name)override{if(missing&&std::strstr(name,missing))return nullptr;
 if(std::strstr(name,"CurrentFromGdb"))return reinterpret_cast<void*>(Current);if(std::strstr(name,"DecodeMethodId"))return reinterpret_cast<void*>(Decode);if(std::strstr(name,"FromReflectedMethod"))return reinterpret_cast<void*>(Reflected);if(!std::strcmp(name,"artDeliverPendingExceptionFromCode"))return reinterpret_cast<void*>(Deliver);if(!std::strcmp(name,"artInvokeObsoleteMethod"))return reinterpret_cast<void*>(ObsoleteOriginal);if(std::strstr(name,"CheckConstructor"))return reinterpret_cast<void*>(CheckOriginal);if(std::strstr(name,"Constructor_newInstance0"))return reinterpret_cast<void*>(ConstructorOriginal);if(std::strstr(name,"NewObjectA"))return reinterpret_cast<void*>(NewAOriginal);if(std::strstr(name,"NewObjectV"))return reinterpret_cast<void*>(NewVOriginal);if(!std::strcmp(name,abi::common::kSuspendConstruct))return reinterpret_cast<void*>(Stop);if(!std::strcmp(name,abi::common::kSuspendDestroy))return reinterpret_cast<void*>(Resume);return nullptr;}
 void*Install(void*target,void*,void**original,std::string*)override{assert(stops==1);*original=target;++installs;targets.push_back(target);return target;}
 bool Remove(void*,std::string*)override{return true;}
};
jobject Global(JNIEnv*,jobject ref){++globals;return ref;}void DeleteGlobal(JNIEnv*,jobject){--globals;}void DeleteLocal(JNIEnv*,jobject){}
jboolean Exception(JNIEnv*){return exception!=nullptr;}jthrowable Occurred(JNIEnv*){return exception?reinterpret_cast<jthrowable>(&context_storage):nullptr;}void ClearException(JNIEnv*){exception=nullptr;}jboolean Instance(JNIEnv*,jobject,jclass){return true;}jclass Class(JNIEnv*,jobject ref){if(ref==mirror)return mirror_class;return static_cast<Object*>(static_cast<void*>(ref))->klass;}
jmethodID Method(JNIEnv*,jclass,const char*name,const char*){if(!std::strcmp(name,"<init>"))return reinterpret_cast<jmethodID>(&constructor_member);return reinterpret_cast<jmethodID>(&other_member);}
jfieldID Field(JNIEnv*,jclass,const char*,const char*){return reinterpret_cast<jfieldID>(&class_storage);}
jobject ToReflected(JNIEnv*,jclass,jmethodID,jboolean){return mirror;}void Void(JNIEnv*,jobject,jmethodID,va_list){}
jclass Find(JNIEnv*,const char*name){return std::strstr(name,"InitializerGuardOracle")?fixture_class:object_class;}jint StaticInt(JNIEnv*,jclass,jfieldID){return static_cast<jint>(allocations);}jobjectArray Array(JNIEnv*,jsize,jclass,jobject){return reinterpret_cast<jobjectArray>(&object_class_storage);}
jboolean Same(JNIEnv*,jobject a,jobject b){return a==b;}jint Int(JNIEnv*,jobject obj,jfieldID){return static_cast<Object*>(static_cast<void*>(obj))->value;}
jobject PublicA(JNIEnv*env,jclass klass,jmethodID id,const jvalue*values){return retirement::initializer_entries::NewA<0>(env,klass,id,values);}
jobject PublicV(JNIEnv*env,jclass klass,jmethodID id,va_list args){return retirement::initializer_entries::NewV<1>(env,klass,id,args);}
jobject PublicReflect(JNIEnv*env,jobject ref,jmethodID,va_list){return retirement::initializer_entries::Constructor(env,ref,nullptr);}
jmethodID StaticMethod(JNIEnv*,jclass,const char*name,const char*){return reinterpret_cast<jmethodID>(!std::strcmp(name,"constructorHandle")?&factory_storage:&invoke_storage);}
jobject StaticObject(JNIEnv*env,jclass,jmethodID id,va_list args){if(id==reinterpret_cast<jmethodID>(&factory_storage))return reinterpret_cast<jobject>(&handle_storage);
 assert(id==reinterpret_cast<jmethodID>(&invoke_storage)&&mh_active);assert(va_arg(args,jobject)==reinterpret_cast<jobject>(&handle_storage));const int exact=va_arg(args,int);assert(exact==0||exact==1);
 if(retirement::RejectRetiredMethod(&constructor_member,current_self,nullptr)){++mh_rejections;return nullptr;}++mh_hits;return ConstructorOriginal(env,mirror,nullptr);
}
template<size_t indexed>jobject V(JNIEnv*env,jclass klass,jmethodID id,...){va_list args;va_start(args,id);auto result=retirement::initializer_entries::NewV<indexed>(env,klass,id,args);va_end(args);return result;}
int main(){using namespace retirement;using namespace initializer_entries;JNINativeInterface_ table{};table.NewGlobalRef=Global;table.DeleteGlobalRef=DeleteGlobal;table.DeleteLocalRef=DeleteLocal;table.ExceptionCheck=Exception;table.ExceptionOccurred=Occurred;table.ExceptionClear=ClearException;table.IsInstanceOf=Instance;table.GetStaticFieldID=Field;table.GetStaticIntField=StaticInt;table.GetObjectClass=Class;table.GetMethodID=Method;table.GetFieldID=Field;table.ToReflectedMethod=ToReflected;table.CallVoidMethodV=Void;table.FindClass=Find;table.NewObjectArray=Array;table.IsSameObject=Same;table.GetIntField=Int;table.NewObjectA=PublicA;table.NewObjectV=PublicV;table.CallObjectMethodV=PublicReflect;table.GetStaticMethodID=StaticMethod;table.CallStaticObjectMethodV=StaticObject;JNIEnv env{&table};Hooks hooks;std::string error;
 hooks.missing="NewObjectV";assert(!InstallInitializerEntryGuards(hooks,&env,anchor,&manager,registry,&error)&&!state.registry&&!globals&&!installs);hooks.missing=nullptr;
 assert(!InstallInitializerEntryGuards(hooks,&env,anchor,&other_thread,registry,&error));
 shared_owner=false;assert(!InstallInitializerEntryGuards(hooks,&env,anchor,&manager,registry,&error));shared_owner=true;
 assert(InstallInitializerEntryGuards(hooks,&env,anchor,&manager,registry,&error)&&state.installed&&globals==1&&installs==5&&!stops);
 assert(!InitializerEntryGuardsOwn(registry));mh_ready=false;assert(!CalibrateInitializerEntryGuards(&env,registry,&error)&&!InitializerEntryGuardsOwn(registry));mh_ready=true;wrong_leaf=true;assert(!CalibrateInitializerEntryGuards(&env,registry,&error)&&!InitializerEntryGuardsOwn(registry));wrong_leaf=false;
 assert(CalibrateInitializerEntryGuards(&env,registry,&error)&&InitializerEntryGuardsOwn(registry));RetiredMethodRegistry other;assert(!InitializerEntryGuardsOwn(other));
 // All actual allocation entry proxies must delegate live constructors and
 // reject cached removed constructors before class initialization/allocation.
 for(unsigned entry=0;entry<5;++entry){unsigned old=allocations;jobject result=entry==0?Constructor(&env,reinterpret_cast<jobject>(&other_member),nullptr):entry==1?NewA<0>(&env,fixture_class,reinterpret_cast<jmethodID>(&other_member),nullptr):entry==2?NewA<1>(&env,fixture_class,reinterpret_cast<jmethodID>(&other_member),nullptr):entry==3?V<0>(&env,fixture_class,reinterpret_cast<jmethodID>(&other_member)):V<1>(&env,fixture_class,reinterpret_cast<jmethodID>(&other_member));assert(result&&allocations==old+1);}
 assert(Check(nullptr,reinterpret_cast<jmethodID>(&other_member)));registry.Retire(&constructor_member);registry.Retire(&clinit_member);
 for(unsigned entry=0;entry<5;++entry){unsigned old=allocations,calls=originals;exception=nullptr;jobject result=entry==0?Constructor(&env,mirror,nullptr):entry==1?NewA<0>(&env,fixture_class,reinterpret_cast<jmethodID>(&constructor_member),nullptr):entry==2?NewA<1>(&env,fixture_class,reinterpret_cast<jmethodID>(&constructor_member),nullptr):entry==3?V<0>(&env,fixture_class,reinterpret_cast<jmethodID>(&constructor_member)):V<1>(&env,fixture_class,reinterpret_cast<jmethodID>(&constructor_member));assert(!result&&allocations==old&&originals==calls&&exception&&!std::strcmp(exception,"Ljava/lang/NoSuchMethodError;"));}
 exception=nullptr;assert(!Check(nullptr,reinterpret_cast<jmethodID>(&constructor_member)));exception=nullptr;unsigned calls=originals;assert(Obsolete(&clinit_member,&thread)==&context_storage&&originals==calls&&deliveries==1);assert(Obsolete(&other_member,&thread)==&context_storage&&originals==calls+1);
 early_delivery=true;if(setjmp(escape)==0){Obsolete(&constructor_member,&thread);assert(false);}assert(deliveries==2);early_delivery=false;
 // A failure to establish the real source JNI window cannot allocate either.
 exception=nullptr;scope=false;calls=originals;assert(!NewA<0>(&env,fixture_class,reinterpret_cast<jmethodID>(&other_member),nullptr)&&originals==calls);scope=true;
 // Existing bodies already executing are not entry calls: retirement neither
 // rewrites their program counter nor changes the existing object's fields.
 assert(fixture.value==static_cast<int>(0x81234567u));
 std::puts("Initializer actual named allocation/check/obsolete proxies: public JNI+reflection body/identity calibration; live delegation, cached retirement NSME before allocation, both delivery ABI behaviors, source/owner failures PASS; transaction initializer removal remains independently gated");
}
