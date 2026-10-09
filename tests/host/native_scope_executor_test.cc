#include "platform/common/native_scope_executor.cc"
#include <cassert>
#include <cstdlib>
#include <new>
#include <thread>
#include <type_traits>
#include <iostream>
using namespace artdcevm;
using namespace native_scope_executor;
static bool no_allocate=false;
void* operator new(size_t n){if(no_allocate)throw std::bad_alloc();if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete[](void* p)noexcept{::operator delete(p);}
namespace {
struct Thread {uint32_t flags=0;};
thread_local Thread self,other;
thread_local Thread* current_thread=&self;
size_t mapped_extent=sizeof(Thread);uint32_t shift=16,runnable=67;
bool pending=false,in_operation=false,bad_vm=false,bad_class=false,bad_env=false;
int calls=0,operations=0,jni_calls=0,releases=0;bool fail_register=false;
JNIEnv env,wrong_env;JavaVM vm,wrong_vm;
void (*registered)(JNIEnv*,jclass,jlong)=nullptr;
enum class Mode{Normal,None,WrongNonce,WrongThread,WrongEnv,WrongState,WrongReturn,Duplicate,JavaException};
Mode mode=Mode::Normal;
void SetState(uint32_t value){self.flags=value<<shift;}
void* Current(){return current_thread;}
void Stop(void*,const char*,bool){std::abort();}void Resume(void*){std::abort();}
struct Hooks:HookEngine {
 bool Initialize(std::string*)override{return true;}
 void* Install(void*,void*,void**,std::string*)override{std::abort();}
 bool Remove(void*,std::string*)override{std::abort();}
 void* FindSymbol(const char*,const char* n)override{return std::string(n)==abi::arm64::android11::kCurrentThread?reinterpret_cast<void*>(Current):nullptr;}
};
void Jni(){assert(!in_operation);++jni_calls;}
jboolean Exception(JNIEnv*){Jni();return pending;}
jint GetVm(JNIEnv*,JavaVM** out){Jni();*out=bad_vm?&wrong_vm:&vm;return JNI_OK;}
jint GetEnv(JavaVM*,void** out,jint version){Jni();assert(version==JNI_VERSION_1_6);*out=bad_env?&wrong_env:&env;return JNI_OK;}
jclass Find(JNIEnv*,const char* n){Jni();assert(std::string(n)=="com/github/ytlog/artdcevm/internal/NativeScopeBridge");return reinterpret_cast<jclass>(4);}
jmethodID Method(JNIEnv*,jclass,const char* n,const char* signature){Jni();assert(std::string(n)=="enter"&&std::string(signature)=="(J)V");return reinterpret_cast<jmethodID>(8);}
jobject Global(JNIEnv*,jobject){Jni();return reinterpret_cast<jobject>(16);}
void DeleteLocal(JNIEnv*,jobject){Jni();}void DeleteGlobal(JNIEnv*,jobject){Jni();++releases;}
jboolean Same(JNIEnv*,jobject a,jobject b){Jni();return !bad_class&&a==reinterpret_cast<jobject>(4)&&b==reinterpret_cast<jobject>(16);}
jint Register(JNIEnv*,jclass,const JNINativeMethod* methods,jint count){Jni();assert(count==1&&std::string(methods[0].name)=="enter"&&std::string(methods[0].signature)=="(J)V");registered=reinterpret_cast<decltype(registered)>(methods[0].fnPtr);if(fail_register){pending=true;return JNI_ERR;}return JNI_OK;}
void Call(JNIEnv*,jclass,jmethodID,const jvalue* args){
 Jni();++calls;const auto nonce=args[0].j;SetState(mode==Mode::WrongState?5:92);
 if(mode==Mode::JavaException)pending=true;
 else if(mode==Mode::WrongThread){std::thread child([&]{registered(&env,reinterpret_cast<jclass>(4),nonce);});child.join();}
 else if(mode!=Mode::None){registered(mode==Mode::WrongEnv?&wrong_env:&env,reinterpret_cast<jclass>(4),mode==Mode::WrongNonce?nonce+1:nonce);if(mode==Mode::Duplicate)registered(&env,reinterpret_cast<jclass>(4),nonce);}
 SetState(mode==Mode::WrongReturn?92:runnable);
}
const Context* expired=nullptr;Executor* executing=nullptr;runnable_jni::Context runnable_context{};
bool Work(const Context& context,void*){
 in_operation=true;no_allocate=true;
 assert(context.Active()&&Executor::Active(&context)&&context.Self()==&self&&context.Env()==&env&&context.Vm()==&vm&&context.Extent()==sizeof(Thread));
 const auto before=jni_calls;
 assert(executing->WithNative(runnable_context,Work,nullptr)==Result::Reentrant);
 assert(jni_calls==before);++operations;expired=&context;
 no_allocate=false;in_operation=false;return true;
}
bool Reject(const Context& context,void*){assert(context.Active());return false;}
bool Throw(const Context& context,void*){assert(context.Active());throw 17;}
void Reset(){mode=Mode::Normal;pending=bad_vm=bad_env=bad_class=false;current_thread=&self;mapped_extent=sizeof(Thread);SetState(runnable);}
}
namespace artdcevm::method_identity {
bool ReadableMappingExtent(const void* address,size_t* out){assert(!in_operation);if(address!=&self)return false;*out=mapped_extent;return mapped_extent!=0;}
}
int main(){
 static_assert(!std::is_copy_constructible_v<Context>);
 static_assert(!std::is_constructible_v<Context,void*,JNIEnv*,JavaVM*,size_t,uint64_t>);
 JNINativeInterface_ table{};table.ExceptionCheck=Exception;table.GetJavaVM=GetVm;table.FindClass=Find;table.GetStaticMethodID=Method;table.NewGlobalRef=Global;table.DeleteLocalRef=DeleteLocal;table.DeleteGlobalRef=DeleteGlobal;table.IsSameObject=Same;table.RegisterNatives=Register;table.CallStaticVoidMethodA=Call;env.functions=&table;wrong_env.functions=&table;
 JNIInvokeInterface_ invoke{};invoke.GetEnv=GetEnv;vm.functions=&invoke;wrong_vm.functions=&invoke;Hooks hooks;std::string error;
 for(auto declaration:{thread_suspend_service::Declaration::kLegacy16,thread_suspend_service::Declaration::kModern24}){
  shift=declaration==thread_suspend_service::Declaration::kLegacy16?16:24;runnable=declaration==thread_suspend_service::Declaration::kLegacy16?67:0;
  thread_suspend_service::Service state;assert(thread_suspend_service::Service::Bind(declaration,[](const char* n)->void*{using namespace abi::arm64::android_common;if(std::string(n)==kSuspendConstruct)return reinterpret_cast<void*>(Stop);if(std::string(n)==kSuspendDestroy)return reinterpret_cast<void*>(Resume);return nullptr;},&state));assert(!state.CanTransitionRunnable());
  Executor executor;executing=&executor;Reset();SetState(92);assert(executor.Prepare(hooks,&env,state,&error)&&!executor.Ready());SetState(runnable);
  runnable_context={&self,&env,&vm};assert(executor.WithNative(runnable_context,Work,nullptr)==Result::Unavailable);
  mode=Mode::None;assert(executor.Calibrate(runnable_context)==Result::NoCallback&&!executor.Ready());Reset();
  assert(executor.Calibrate(runnable_context)==Result::Success&&executor.Ready());
  assert(executor.WithNative(runnable_context,Work,nullptr)==Result::Success);assert(!Executor::Active(expired));
  assert(executor.WithNative(runnable_context,Reject,nullptr)==Result::OperationRejected);
  assert(executor.WithNative(runnable_context,Throw,nullptr)==Result::OperationThrew);
  assert(executor.WithNative(runnable_context,Work,nullptr)==Result::Success);
  for(auto test:{Mode::None,Mode::WrongNonce,Mode::WrongThread,Mode::WrongEnv,Mode::WrongState,Mode::WrongReturn,Mode::Duplicate,Mode::JavaException}){
   Reset();mode=test;const auto before=operations;auto result=executor.WithNative(runnable_context,Work,nullptr);assert(result!=Result::Success);
   if(test==Mode::WrongReturn||test==Mode::Duplicate)assert(operations==before+1);else assert(operations==before);
   if(test==Mode::JavaException)assert(result==Result::JniException&&pending);assert(!Executor::Active(expired));
  }
  Reset();bad_class=true;assert(executor.WithNative(runnable_context,Work,nullptr)==Result::ContextMismatch);
  Reset();bad_env=true;assert(executor.WithNative(runnable_context,Work,nullptr)==Result::ContextMismatch);
  Reset();bad_vm=true;assert(executor.WithNative(runnable_context,Work,nullptr)==Result::ContextMismatch);
  Reset();mapped_extent=0;assert(executor.WithNative(runnable_context,Work,nullptr)==Result::StateMismatch);
  Reset();current_thread=&other;assert(executor.WithNative(runnable_context,Work,nullptr)==Result::StateMismatch);
  Reset();SetState(92);assert(executor.WithNative(runnable_context,Work,nullptr)==Result::StateMismatch); // Native caller cannot synthesize Runnable entry.
  Reset();pending=true;const auto before=calls;assert(executor.WithNative(runnable_context,Work,nullptr)==Result::PendingException&&calls==before&&pending);
  Reset();mode=Mode::None;assert(executor.Calibrate(runnable_context)==Result::NoCallback&&!executor.Ready());
  assert(executor.WithNative(runnable_context,Work,nullptr)==Result::Unavailable);
  Reset();assert(executor.Calibrate(runnable_context)==Result::Success&&executor.Ready());
  Reset();Executor bad;SetState(92);fail_register=true;const auto roots_before=releases;assert(!bad.Prepare(hooks,&env,state,&error)&&pending&&releases==roots_before+1&&!bad.Ready());fail_register=false;
  Reset();assert(executor.WithNative(runnable_context,Work,nullptr)==Result::Success);
 }
 sequence=uint64_t(std::numeric_limits<jlong>::max());assert(NextNonce()==uint64_t(std::numeric_limits<jlong>::max()));assert(!NextNonce()&&!NextNonce());
 std::cout<<"Actual Native executor TU: both state declarations without transition pairs, real registered callback closure, entry/return/env/VM/nonce/TLS proof, nonreentrancy/no-JNI/no-allocation operation, original exceptions and failure recovery PASS\n";
}
