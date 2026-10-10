#include "native_receiver_source_fixture.inc"
#include "platform/common/native_method_handle_source_calibration.h"
#include <cstdarg>
namespace {
borrowed_jni_callback::Bridge bridge;
size_t globals=0,locals=0,operations=0,fail_at=0,borrow_callbacks=0;
bool pending=false,deny_decode=false,omit_parameter=false,window_prepared=false,window_entered=false,deny_window=false,nonnull_failure=false;
JNIEnv_* actual_env=nullptr;
int tokens[8];
jobject Token(size_t i){return reinterpret_cast<jobject>(&tokens[i]);}
jmethodID Method(size_t i){return reinterpret_cast<jmethodID>(&tokens[i]);}
bool Fault(){assert(!no_alloc);++operations;if(fail_at&&operations==fail_at){pending=true;return !nonnull_failure;}return false;}
jboolean Same(JNIEnv*,jobject a,jobject b){if(Fault())return JNI_FALSE;return a==b;}
jboolean Pending(JNIEnv*){return pending;}
jclass Find(JNIEnv*,const char*){if(Fault())return nullptr;++locals;return reinterpret_cast<jclass>(Token(0));}
jclass ObjectClass(JNIEnv*,jobject){if(Fault())return nullptr;++locals;return reinterpret_cast<jclass>(Token(0));}
jobject NewGlobal(JNIEnv*,jobject o){if(Fault())return nullptr;++globals;return o;}
void DeleteGlobal(JNIEnv*,jobject){assert(globals);--globals;}
void DeleteLocal(JNIEnv*,jobject){assert(locals);--locals;}
jmethodID GetMethod(JNIEnv*,jclass,const char*name,const char*){if(Fault())return nullptr;
 if(!std::strcmp(name,"type"))return Method(1);
 if(!std::strcmp(name,"parameterCount"))return Method(2);
 if(!std::strcmp(name,"parameterType"))return Method(3);
 assert(!std::strcmp(name,"getDeclaringClass"));return Method(4);}
void* Original(const abi::common::RunnableJniScope&,jobject member){return reinterpret_cast<void*>(member);}
jmethodID Reflected(JNIEnv*env,jobject member){if(Fault())return nullptr;
 return reinterpret_cast<jmethodID>(bridge.OnFromReflectedMethod({current,env,reinterpret_cast<void*>(1)},member));}
jfieldID Field(JNIEnv*,jobject member){if(Fault())return nullptr;return reinterpret_cast<jfieldID>(member);}
jobject CallObject(JNIEnv*,jobject object,jmethodID method,va_list args){if(Fault())return nullptr;void*out=nullptr;
 for(auto&s:samples){if(method==Method(1)&&object==reinterpret_cast<jobject>(&s.handle))out=&s.type;
  if(method==Method(4)&&object==reinterpret_cast<jobject>(&s.member))out=&s.declaring;
  if(method==Method(3)&&object==reinterpret_cast<jobject>(&s.type)){int i=va_arg(args,int);out=i?static_cast<void*>(&s.parameter):static_cast<void*>(&s.caller);if(omit_parameter)out=&samples[0].declaring;}}
 assert(out);++locals;return reinterpret_cast<jobject>(out);}
jint CallInt(JNIEnv*,jobject object,jmethodID method,va_list){if(Fault())return 0;assert(method==Method(2));
 for(auto&s:samples)if(object==reinterpret_cast<jobject>(&s.type))return s.parameters.count;std::abort();}
bool PrepareWindow(void*){assert(!no_alloc&&!pending&&globals>0);window_prepared=true;return !deny_window;}
bool WithWindow(void*,const runnable_jni::Context&ctx,runnable_jni::Operation op,void*p){assert(no_alloc&&window_prepared&&!window_entered);window_entered=true;bool result=op(ctx,p);window_entered=false;return result;}
bool Decode(void*,const runnable_jni::Context&ctx,jobject reflected,jmethodID method,jfieldID field,void**out)noexcept{
 assert(no_alloc&&!window_entered&&ctx.self==current&&ctx.env==actual_env&&ctx.vm);if(deny_decode)return false;
 assert((method==nullptr)!=(field==nullptr));assert(reinterpret_cast<void*>(method?reinterpret_cast<void*>(method):reinterpret_cast<void*>(field))==reinterpret_cast<void*>(reflected));
 *out=reinterpret_cast<void*>(reflected);return true;}
}
namespace artdcevm::runnable_executor {
runnable_jni::Result With(JNIEnv*env,jobject carrier,runnable_jni::Operation op,void*payload){
 struct R {runnable_jni::Operation op;void*payload;};R r{op,payload};
 return bridge.Invoke(env,carrier,[](const runnable_jni::Context&ctx,void*p){auto&r=*static_cast<R*>(p);
  ++borrow_callbacks;no_alloc=true;bool result=r.op(ctx,r.payload);no_alloc=false;return result;},&r);
}
}
int main(){
 retirement::state.installed=true;retirement::state.field_kinds_calibrated=true;retirement::state.binding.current=Current;retirement::state.binding.visit=SourceVisit;retirement::state.binding.kind=abi::arm64::android_common::MethodHandleKind;
 for(size_t i=0;i<samples.size();++i){samples[i].handle.kind=kinds[i];if(kinds[i]==3||kinds[i]==4||kinds[i]==10)samples[i].parameters.count=0;if(kinds[i]==9)samples[i].parameters.count=2;}
 JNINativeInterface_ table{};table.ExceptionCheck=Pending;table.IsSameObject=Same;table.FindClass=Find;table.GetObjectClass=ObjectClass;table.NewGlobalRef=NewGlobal;table.DeleteGlobalRef=DeleteGlobal;table.DeleteLocalRef=DeleteLocal;table.GetMethodID=GetMethod;table.FromReflectedMethod=Reflected;table.FromReflectedField=Field;table.CallObjectMethodV=CallObject;table.CallIntMethodV=CallInt;
 JNIEnv_ env{&table};actual_env=&env;assert(borrowed_jni_callback::Bridge::Bind(reinterpret_cast<void*>(Original),&bridge));
 std::vector<receiver_guard::PublicHandleSample> public_samples;
 for(size_t i=0;i<samples.size();++i)public_samples.push_back({reinterpret_cast<jobject>(&samples[i].handle),reinterpret_cast<jobject>(&samples[i].member),kinds[i]});
 receiver_guard::MethodHandleSourceAccess access{nullptr,IsCurrent,Extent,Ref,Strong,nullptr,Declaring,Special};
 auto prepare=[&](receiver_guard::MethodHandleReceiverSource*out){return receiver_guard::PrepareMethodHandleReceiverSource(&env,public_samples[0].reflected_member,public_samples,access,{nullptr,Decode},{nullptr,PrepareWindow,WithWindow},out,nullptr);};
 receiver_guard::MethodHandleReceiverSource output(access);assert(prepare(&output)&&output.Proven()&&borrow_callbacks==1&&globals==0&&locals==0&&!pending);
 size_t successful_operations=operations;
 // Every public JNI failure is observed by the actual production helper. Both
 // non-null roots from earlier calls and currently returned locals are released.
 for(bool mode:{false,true}){nonnull_failure=mode;for(size_t i=1;i<=successful_operations;++i){operations=0;fail_at=i;receiver_guard::MethodHandleReceiverSource rejected(access);
  assert(!prepare(&rejected)&&!rejected.Proven()&&pending&&globals==0&&locals==0);pending=false;}}
 nonnull_failure=false;fail_at=0;operations=0;
 pending=true;assert(!prepare(&output)&&pending&&output.Proven()&&globals==0&&locals==0);pending=false;
 auto saved=public_samples[8];public_samples[8]=public_samples[7];assert(!prepare(&output)&&output.Proven()&&globals==0&&locals==0);public_samples[8]=saved;
 for(bool*flag:{&deny_decode,&omit_parameter,&deny_window}){*flag=true;receiver_guard::MethodHandleReceiverSource rejected(access);assert(!prepare(&rejected)&&!rejected.Proven()&&!pending&&globals==0&&locals==0);*flag=false;}
 visits=false;receiver_guard::MethodHandleReceiverSource rejected(access);assert(!prepare(&rejected)&&!rejected.Proven()&&globals==0&&locals==0);visits=true;
 uint32_t kind=99;void*caller=nullptr;void*member=nullptr;bool field=false;assert(Entry(nullptr,current,&samples[0].handle,&kind,&member,&field)&&member==&samples[0].member&&!field);
 auto oracle=Oracle(0);auto partial_access=access;partial_access.entry=Entry;receiver_guard::MethodHandleReceiverSource partial(partial_access);assert(partial.Observe(oracle)&&!partial.Seal());
 retirement::state.binding.kind=Kind;assert(!prepare(&rejected));retirement::state.binding.kind=abi::arm64::android_common::MethodHandleKind;
 no_alloc=true;assert(output.Read(current,&samples[1].handle,&kind,&caller)&&kind==1&&caller==&samples[1].caller&&caller!=&samples[1].declaring);no_alloc=false;
 std::printf("Production ordinary public-JNI calibration + real Runnable Bridge + sole native VisitTarget owner: nine families, special caller identity, %zu JNI failure positions, pending preservation, no callback allocation, root/local refund PASS; host source leaves do not establish device calibration or permanent access lifetime\n",successful_operations);
}
