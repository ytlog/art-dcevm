#include "platform/common/runnable_global_ref_bridge.h"
#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <thread>
using namespace artdcevm::runnable_jni;
using Pointer=artdcevm::abi::arm64::android11::ReleaseObjectPointer;
struct State {bool runnable=false,exception=false,skip=false,mismatch=false,null_root=false,identity=false,original_exception=false,throw_jni=false;int roots=0,originals=0,operations=0;};
thread_local State state;
GlobalRefBridge bridge;int vm;
jobject input=reinterpret_cast<jobject>(0x1000),duplicate=reinterpret_cast<jobject>(0x2000),local=reinterpret_cast<jobject>(0x3000);
void* Current(){return &state;}
jboolean Exception(JNIEnv*){return state.exception;}
jobjectRefType Type(JNIEnv*,jobject obj){return obj==input||obj==duplicate?JNIGlobalRefType:JNILocalRefType;}
jint VM(JNIEnv*,JavaVM** out){*out=reinterpret_cast<JavaVM*>(&vm);return JNI_OK;}
jboolean Same(JNIEnv*,jobject a,jobject b){assert(state.runnable&&state.roots>0);return !state.identity&&a==duplicate&&b==input;}
jobject Original(void*,void*,Pointer obj){assert(state.runnable&&obj.reference==0x4000);++state.originals;if(state.original_exception)state.exception=true;if(state.null_root)return nullptr;++state.roots;return duplicate;}
jobject New(JNIEnv*,jobject obj){assert(obj==input);bool old=state.runnable;state.runnable=true;struct Restore{bool old;~Restore(){state.runnable=old;}} restore{old};if(state.throw_jni)throw std::runtime_error("fixture");if(state.skip){++state.roots;return duplicate;}return bridge.OnAddGlobalRef(state.mismatch?nullptr:&vm,&state,{0x4000});}
void Delete(JNIEnv*,jobject obj){assert(obj==duplicate&&state.roots>0);--state.roots;}
bool Success(const Context& ctx,void*){assert(state.runnable&&state.roots>0&&ctx.self==&state&&ctx.vm==&vm);++state.operations;assert(bridge.Invoke(ctx.env,input,Success,nullptr)==Result::kReentrant);auto nested=ctx.env->NewGlobalRef(input);ctx.env->DeleteGlobalRef(nested);return true;}
bool Reject(const Context&,void*){assert(state.roots>0);++state.operations;return false;}
bool Throw(const Context&,void*){assert(state.roots>0);++state.operations;throw std::runtime_error("callback");}
bool JavaException(const Context&,void*){assert(state.roots>0);++state.operations;state.exception=true;return true;}
int main(){
 JNINativeInterface_ f{};f.ExceptionCheck=Exception;f.GetObjectRefType=Type;f.GetJavaVM=VM;f.IsSameObject=Same;f.NewGlobalRef=New;f.DeleteGlobalRef=Delete;JNIEnv env{&f};
 assert(GlobalRefBridge::Bind(reinterpret_cast<void*>(Original),reinterpret_cast<void*>(Current),&bridge));
 assert(!GlobalRefBridge::Bind(nullptr,reinterpret_cast<void*>(Current),&bridge));
 assert(bridge.Invoke(&env,input,Success,nullptr)==Result::kSuccess);assert(!state.runnable&&state.roots==0&&state.originals==2&&state.operations==1);
 state={};state.runnable=true;assert(bridge.Invoke(&env,input,Reject,nullptr)==Result::kOperationRejected);assert(state.runnable&&state.roots==0);
 state={};state.exception=true;assert(bridge.Invoke(&env,input,Success,nullptr)==Result::kPendingException);assert(state.roots==0&&!state.operations);
 state={};assert(bridge.Invoke(&env,local,Success,nullptr)==Result::kInvalidRequest);assert(!state.originals);
 state={};state.skip=true;assert(bridge.Invoke(&env,input,Success,nullptr)==Result::kLeafNotReached);assert(state.roots==0&&!state.operations);
 state={};state.mismatch=true;assert(bridge.Invoke(&env,input,Success,nullptr)==Result::kContextMismatch);assert(state.roots==0&&!state.operations);
 state={};state.identity=true;assert(bridge.Invoke(&env,input,Success,nullptr)==Result::kContextMismatch);assert(state.roots==0&&!state.operations);
 state={};state.null_root=true;assert(bridge.Invoke(&env,input,Success,nullptr)==Result::kOriginalRejected);assert(state.roots==0&&!state.operations);
 state={};state.original_exception=true;assert(bridge.Invoke(&env,input,Success,nullptr)==Result::kJniException);assert(state.roots==0&&state.exception&&!state.operations);
 state={};assert(bridge.Invoke(&env,input,Throw,nullptr)==Result::kOperationThrew);assert(state.roots==0&&state.operations==1);
 state={};assert(bridge.Invoke(&env,input,JavaException,nullptr)==Result::kJniException);assert(state.roots==0&&state.exception);
 state={};state.throw_jni=true;try{bridge.Invoke(&env,input,Success,nullptr);assert(false);}catch(const std::runtime_error&){}state={};assert(bridge.Invoke(&env,input,Success,nullptr)==Result::kSuccess);assert(state.roots==0);
 state={};std::thread worker([&]{JNIEnv e{&f};assert(bridge.Invoke(&e,input,Success,nullptr)==Result::kSuccess);assert(state.roots==0&&state.operations==1);});worker.join();assert(!state.operations);
 assert(bridge.Invoke(nullptr,input,Success,nullptr)==Result::kInvalidRequest);
 puts("Runnable global-ref bridge: real-scope fixture, roots on success/failure, identity, passthrough, reentrancy, exceptions and thread isolation passed");
}
