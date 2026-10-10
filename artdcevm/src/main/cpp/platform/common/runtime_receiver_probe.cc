#include "platform/common/runtime_receiver_probe.h"
#include "platform/abi/common/suspend_all.h"
#include "platform/abi/arm64/android11/jni_object_decode.h"
#include <cstring>
#include <atomic>
#include <mutex>
#include <sstream>
#include "platform/abi/common/jni_id_query_scope.h"
namespace artdcevm::runtime_receiver {
namespace {
// Fixed class_linker.h/handle.h: Handle<ClassLoader> is one StackReference slot
// pointer passed by value; ObjPtr<Class> is the trivial single-word return.
using FindClass=abi::arm64::android11::ReleaseObjectPointer (*)(void*,void*,const char*,void*);
using LengthFindClass=abi::arm64::android11::ReleaseObjectPointer (*)(void*,void*,const char*,size_t,void*);
using EncodeMethod=jmethodID (*)(void*,void*);
using SuspendThreads=void (*)(void*,const char*,bool);
constexpr char kSuspendThreads[]="_ZN3art10ThreadList10SuspendAllEPKcb";
constexpr char kFindClass[]="_ZN3art11ClassLinker9FindClassEPNS_6ThreadEPKcNS_6HandleINS_6mirror11ClassLoaderEEE";
constexpr char kLengthFindClass[]="_ZN3art11ClassLinker9FindClassEPNS_6ThreadEPKcmNS_6HandleINS_6mirror11ClassLoaderEEE";
constexpr char kEncodeMethod[]="_ZN3art3jni12JniIdManager14EncodeMethodIdEPNS_9ArtMethodE";
constexpr char kClass[]="com/github/ytlog/artdcevm/internal/CalibrationFixture";
constexpr char kDescriptor[]="Lcom/github/ytlog/artdcevm/internal/CalibrationFixture;";
struct Request {Receivers receivers;bool conflicting=false;
  unsigned finds=0,encodes=0,query_scopes=0,suspends=0;};
thread_local Request* active=nullptr;
FindClass original_find=nullptr;
LengthFindClass original_length_find=nullptr;
EncodeMethod original_encode=nullptr;
SuspendThreads original_suspend=nullptr;
abi::common::SuspendConstruct suspend_construct=nullptr;
abi::common::SuspendDestroy suspend_destroy=nullptr;
abi::common::JniIdQueryConstruct query_construct=nullptr;
abi::common::JniIdQueryDestroy query_destroy=nullptr;
std::atomic<FindClass> find_handler{nullptr};
std::atomic<LengthFindClass> length_find_handler{nullptr};
std::mutex installation;
bool attempted=false,installed=false;
void Observe(void*& slot,void* receiver,Request& request) {
  if(!receiver || (slot && slot!=receiver))request.conflicting=true;
  else slot=receiver;
}
void ObserveQueryScope(Request& request) {
  if(!query_construct || !query_destroy)return;
  // JNIImpl::FindClass already holds original ScopedObjectAccess: runnable and
  // shared mutator ownership. EndDefer explicitly forbids exclusive ownership.
  // Do not move this into ScopedSuspendAll or perform JNI/ID allocation here.
  abi::common::JniIdQueryScope scope{};
  query_construct(&scope);
  ++request.query_scopes;
  Observe(request.receivers.jni_id_manager,scope.manager,request);
  query_destroy(&scope);
}
abi::arm64::android11::ReleaseObjectPointer ObserveFind(void* linker,void* self,
                                                     const char* descriptor,void* loader) {
  if(active && descriptor && !std::strcmp(descriptor,kDescriptor))
    {++active->finds;Observe(active->receivers.class_linker,linker,*active);ObserveQueryScope(*active);}
  auto handler=find_handler.load(std::memory_order_acquire);
  return (handler?handler:original_find)(linker,self,descriptor,loader);
}
// Fixed Android16/17 class_linker.h: size_t is descriptor_length.
abi::arm64::android11::ReleaseObjectPointer ObserveLengthFind(void* linker,void* self,
    const char* descriptor,size_t length,void* loader) {
  if(active&&descriptor&&length==sizeof(kDescriptor)-1&&!std::memcmp(descriptor,kDescriptor,length))
    {++active->finds;Observe(active->receivers.class_linker,linker,*active);ObserveQueryScope(*active);}
  auto handler=length_find_handler.load(std::memory_order_acquire);
  return (handler?handler:original_length_find)(linker,self,descriptor,length,loader);
}
jmethodID ObserveEncode(void* manager,void* method) {
  if(active){++active->encodes;Observe(active->receivers.jni_id_manager,manager,*active);}
  return original_encode(manager,method);
}
void ObserveSuspend(void* threads,const char* cause,bool long_suspend) {
  if(active){++active->suspends;Observe(active->receivers.thread_list,threads,*active);}
  original_suspend(threads,cause,long_suspend);
}
bool Fail(std::string* error,const char* message) {if(error)*error=message;return false;}
bool Install(HookEngine& hooks,std::string* error) {
  std::lock_guard<std::mutex> lock(installation);
  if(installed)return true;
  if(attempted)return Fail(error,"Runtime receiver hook installation previously failed; restart process");
  auto find=hooks.FindSymbol("libart.so",kFindClass);
  const bool with_length=find==nullptr;
  if(with_length)find=hooks.FindSymbol("libart.so",kLengthFindClass);
  auto encode=hooks.FindSymbol("libart.so",kEncodeMethod);
  using namespace abi::common;
  auto query_c=reinterpret_cast<JniIdQueryConstruct>(hooks.FindSymbol("libart.so",kJniIdQueryConstruct));
  auto query_d=reinterpret_cast<JniIdQueryDestroy>(hooks.FindSymbol("libart.so",kJniIdQueryDestroy));
  const bool query_available=query_c && query_d;
  auto suspend=hooks.FindSymbol("libart.so",kSuspendThreads);
  auto construct=reinterpret_cast<SuspendConstruct>(hooks.FindSymbol("libart.so",kSuspendConstruct));
  auto destroy=reinterpret_cast<SuspendDestroy>(hooks.FindSymbol("libart.so",kSuspendDestroy));
  if(!find || (!encode && !query_available) || !suspend || !construct || !destroy)
    return Fail(error,"Missing named Runtime receiver observation functions");
  attempted=true;
  if(query_available){query_construct=query_c;query_destroy=query_d;}
  // Entry is a regular Native JNI call. ScopedSuspendAll obtains the original
  // ThreadList itself, so no uncalibrated Thread or Runtime receiver is passed.
  SuspendAll storage{};construct(&storage,"artdcevm Runtime receiver calibration",false);
  void* find_original=nullptr;void* encode_original=nullptr;
  auto find_token=hooks.Install(find,with_length?reinterpret_cast<void*>(ObserveLengthFind):
      reinterpret_cast<void*>(ObserveFind),&find_original,error);
  if(find_token) {
    if(with_length)original_length_find=reinterpret_cast<LengthFindClass>(find_original);
    else original_find=reinterpret_cast<FindClass>(find_original);
  }
  auto encode_token=encode && find_token && find_original?
      hooks.Install(encode,reinterpret_cast<void*>(ObserveEncode),&encode_original,error):nullptr;
  if(encode_token)original_encode=reinterpret_cast<EncodeMethod>(encode_original);
  void* suspend_original=nullptr;
  const bool encode_ready=!encode || (encode_token && original_encode);
  auto suspend_token=find_token && find_original && encode_ready?
      hooks.Install(suspend,reinterpret_cast<void*>(ObserveSuspend),&suspend_original,error):nullptr;
  if(suspend_token)original_suspend=reinterpret_cast<SuspendThreads>(suspend_original);
  installed=find_token && encode_ready && suspend_token &&
      find_original && original_suspend;
  if(installed){suspend_construct=construct;suspend_destroy=destroy;}
  destroy(&storage);
  if(!installed)return Fail(error,"Runtime receiver observation hooks unavailable; restart process");
  return true;
}
}
bool SetFindClassHandler(void* handler,void** original) {
  std::lock_guard<std::mutex> lock(installation);
  if(!installed || !original_find || (handler && !original))return false;
  if(original)*original=reinterpret_cast<void*>(original_find);
  find_handler.store(reinterpret_cast<FindClass>(handler),std::memory_order_release);
  return true;
}
bool SetLengthFindClassHandler(void* handler,void** original) {
  std::lock_guard<std::mutex> lock(installation);
  if(!installed||!original_length_find||(handler&&!original))return false;
  if(original)*original=reinterpret_cast<void*>(original_length_find);
  length_find_handler.store(reinterpret_cast<LengthFindClass>(handler),std::memory_order_release);
  return true;
}
bool Calibrate(HookEngine& hooks,JNIEnv* env,Receivers* output,
                              std::string* error) {
  if(error)error->clear();
  if(!env || !output || active)return Fail(error,"Invalid or reentrant Runtime receiver calibration");
  if(env->ExceptionCheck())return Fail(error,"Runtime receiver calibration requires no pending exception");
  if(!Install(hooks,error))return false;
  Request request;
  active=&request;
  auto klass=env->FindClass(kClass);
  auto method=klass?env->GetMethodID(klass,"compute","()I"):nullptr;
  // Repeat the original public FindClass after GetMethodID: pointer-ID paths
  // have no Encode callback, so both independently populated scope outputs must
  // agree before/after the ID operation. Never use the candidate Runtime slot.
  jclass repeated=nullptr;
  if(request.query_scopes && !env->ExceptionCheck())repeated=env->FindClass(kClass);
  // C2 obtains the real ThreadList internally. Observe its named SuspendAll
  // receiver, and immediately release the owned stop before any public JNI work.
  abi::common::SuspendAll storage{};
  suspend_construct(&storage,"artdcevm ThreadList receiver calibration",false);
  suspend_destroy(&storage);
  active=nullptr;
  if(klass)env->DeleteLocalRef(klass);
  if(repeated)env->DeleteLocalRef(repeated);
  if(!klass || !method || (request.query_scopes && (!repeated || request.query_scopes<2)) || env->ExceptionCheck() || request.conflicting ||
     !request.receivers.class_linker || !request.receivers.jni_id_manager || !request.receivers.thread_list)
    {
      if(error){std::ostringstream text;
        text<<"Public JNI did not establish consistent Runtime receivers: class="<<(klass!=nullptr)
            <<" method="<<(method!=nullptr)<<" exception="<<bool(env->ExceptionCheck())
            <<" conflict="<<request.conflicting<<" find="<<request.finds
            <<" encode="<<request.encodes<<" queryScope="<<request.query_scopes
            <<" suspend="<<request.suspends<<" linker="<<(request.receivers.class_linker!=nullptr)
            <<" manager="<<(request.receivers.jni_id_manager!=nullptr)
            <<" threadList="<<(request.receivers.thread_list!=nullptr);*error=text.str();}
      return false;
    }
  if(env->GetJavaVM(&request.receivers.java_vm)!=JNI_OK || !request.receivers.java_vm)
    return Fail(error,"Public JNI JavaVM receiver unavailable");
  *output=request.receivers;
  return true;
}
}
