#include "platform/common/native_method_handle_source_calibration.h"
#include "platform/common/modern_method_handle_guards.h"
#include "platform/common/runnable_executor.h"
#include <array>
namespace artdcevm::receiver_guard {
namespace {
struct Roots {
 JNIEnv* env; std::vector<jobject> globals;
 ~Roots(){for(auto root:globals)env->DeleteGlobalRef(root);}
 jobject Keep(jobject local){if(!local)return nullptr;auto global=env->NewGlobalRef(local);
  if(global){try{globals.push_back(global);}catch(...){env->DeleteGlobalRef(global);throw;}}return global;}
};
struct Local {
 JNIEnv* env;jobject object;
 ~Local(){if(object)env->DeleteLocalRef(object);}
};
struct Prepared {
 MethodHandleSourceOracle oracle;
 jobject reflected=nullptr;jmethodID method=nullptr;jfieldID field=nullptr;
};
bool Entry(void*,void*self,void*handle,uint32_t*kind,void**member,bool*field)noexcept{
 return retirement::ReadMethodHandleReceiverEntry(self,handle,kind,member,field);
}
struct Request {
 MethodHandleReceiverSource* source;
 PublicHandleMemberDecoder decoder;
 std::vector<Prepared>* samples;
 PublicHandleObservationWindow window;
};
bool Observe(const runnable_jni::Context& context,void* payload){
 auto&r=*static_cast<Request*>(payload);
 for(auto& sample:*r.samples){auto&o=sample.oracle;o.thread=context.self;
  if(!o.member||!r.source->Observe(o))return false;
 }
 return r.source->Seal();
}
bool InWindow(const runnable_jni::Context&context,void*payload){
 auto&r=*static_cast<Request*>(payload);
 // Public ID decoders may lock or safepoint. Finish ALL decodes before the
 // read window borrows any managed identity. Reflected globals keep native
 // member declarations live, and only native member addresses escape here.
 for(auto&sample:*r.samples){if(!r.decoder.decode(r.decoder.owner,context,
     sample.reflected,sample.method,sample.field,&sample.oracle.member)||!sample.oracle.member)return false;}
 return r.window.with(r.window.owner,context,Observe,payload);
}
}
bool PrepareMethodHandleReceiverSource(JNIEnv*env,jobject carrier,
 const std::vector<PublicHandleSample>&samples,MethodHandleSourceAccess access,
 PublicHandleMemberDecoder decoder,PublicHandleObservationWindow window,MethodHandleReceiverSource*output,std::string*error){
 auto fail=[&](const char*message){if(error)*error=message;return false;};
 if(!env||!carrier||!output||!decoder.decode||!window.prepare||!window.with||samples.size()!=9)return fail("MethodHandle source requires nine public families and an independent member decoder");
 if(env->ExceptionCheck())return fail("MethodHandle source entered with pending exception");
 uint32_t families=0;
 for(const auto&s:samples){
  if(!s.handle||!s.reflected_member||s.kind>11||(s.kind>=5&&s.kind<=7)||
     (families&(uint32_t{1}<<s.kind)))return fail("MethodHandle public family missing or duplicated");
  families|=uint32_t{1}<<s.kind;
 }
 if(families!=0x0f1f)return fail("MethodHandle public family inventory incomplete");
 Roots roots{env,{}};roots.globals.reserve(64);
 Local handle_class{env,env->FindClass("java/lang/invoke/MethodHandle")};
 if(!handle_class.object||env->ExceptionCheck())return fail("MethodHandle public class unavailable");
 Local type_class{env,env->FindClass("java/lang/invoke/MethodType")};
 if(!type_class.object||env->ExceptionCheck())return fail("MethodType public class unavailable");
 jmethodID type=env->GetMethodID(static_cast<jclass>(handle_class.object),"type","()Ljava/lang/invoke/MethodType;");
 if(!type||env->ExceptionCheck())return fail("MethodHandle.type unavailable");
 jmethodID count=env->GetMethodID(static_cast<jclass>(type_class.object),"parameterCount","()I");
 if(!count||env->ExceptionCheck())return fail("MethodType.parameterCount unavailable");
 jmethodID parameter=env->GetMethodID(static_cast<jclass>(type_class.object),"parameterType","(I)Ljava/lang/Class;");
 if(!parameter||env->ExceptionCheck())return fail("MethodType.parameterType unavailable");
 std::vector<Prepared> prepared;prepared.reserve(samples.size());
 for(const auto&s:samples){Prepared p;p.oracle.kind=s.kind;p.oracle.field=s.kind>=8;
  p.oracle.handle=roots.Keep(s.handle);if(!p.oracle.handle||env->ExceptionCheck())return fail("MethodHandle root failed");
  p.reflected=roots.Keep(s.reflected_member);if(!p.reflected||env->ExceptionCheck())return fail("MethodHandle reflected member root failed");
  if(p.oracle.field)p.field=env->FromReflectedField(s.reflected_member);else p.method=env->FromReflectedMethod(s.reflected_member);
  if((p.oracle.field?!p.field:!p.method)||env->ExceptionCheck())return fail("MethodHandle public member ID failed");
  Local member_class{env,env->GetObjectClass(s.reflected_member)};
  if(!member_class.object||env->ExceptionCheck())return fail("MethodHandle reflected member class failed");
  jmethodID declaring=env->GetMethodID(static_cast<jclass>(member_class.object),"getDeclaringClass","()Ljava/lang/Class;");
  if(!declaring||env->ExceptionCheck())return fail("MethodHandle member.getDeclaringClass unavailable");
  Local declaration{env,env->CallObjectMethod(s.reflected_member,declaring)};
  if(!declaration.object||env->ExceptionCheck())return fail("MethodHandle public declaring class failed");
  p.oracle.declaring=roots.Keep(declaration.object);if(!p.oracle.declaring||env->ExceptionCheck())return fail("MethodHandle declaring class root failed");
  Local method_type{env,env->CallObjectMethod(s.handle,type)};
  if(!method_type.object||env->ExceptionCheck())return fail("MethodHandle public type failed");
  p.oracle.type=roots.Keep(method_type.object);if(!p.oracle.type||env->ExceptionCheck())return fail("MethodHandle type root failed");
  jint n=env->CallIntMethod(method_type.object,count);
  if(env->ExceptionCheck()||n<0||n>255)return fail("MethodHandle public parameter count failed");
  p.oracle.parameter_classes.reserve(static_cast<size_t>(n));
  for(jint i=0;i<n;++i){Local klass{env,env->CallObjectMethod(method_type.object,parameter,i)};
   if(!klass.object||env->ExceptionCheck())return fail("MethodHandle public parameter class failed");
   jobject root=roots.Keep(klass.object);if(!root||env->ExceptionCheck())return fail("MethodHandle parameter class root failed");
   p.oracle.parameter_classes.push_back(root);
  }
  if(s.kind==1){if(p.oracle.parameter_classes.empty())return fail("MethodHandle super oracle has no special caller");
   jboolean same=env->IsSameObject(p.oracle.declaring,p.oracle.parameter_classes.front());
   if(env->ExceptionCheck()||same)return fail("MethodHandle super oracle must distinguish special caller from declaring class");
  }
  prepared.push_back(std::move(p));
 }
 // All roots and vectors now complete. There is no JNI after first native
 // borrow, until the source-owned callback returns to ordinary cleanup.
 if(!window.prepare(window.owner))return fail("MethodHandle ordinary source window preparation failed");
 access.entry=Entry;MethodHandleReceiverSource candidate(access);
 Request request{&candidate,decoder,&prepared,window};
 if(runnable_executor::With(env,carrier,InWindow,&request)!=runnable_jni::Result::kSuccess)
  return fail("MethodHandle original Runnable/source semantic oracle rejected");
 if(env->ExceptionCheck())return fail("MethodHandle source callback raised exception");
 *output=candidate;return true;
}
}
