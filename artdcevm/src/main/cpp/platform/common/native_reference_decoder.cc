#include "platform/common/native_reference_decoder.h"
#include "platform/abi/arm64/android14/jni_global_decode.h"
namespace artdcevm::native_reference_decoder {
namespace {
constexpr char kThreadCleared[]="_ZNK3art6Thread14IsJWeakClearedEP8_jobject";
constexpr char kCleared[]="_ZN3art9JavaVMExt19IsWeakGlobalClearedEPNS_6ThreadEPv";
bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
struct Proof {Decoder* decoder;JNIEnv* env;jobject a,b,copy;jweak wa,wb,dead;bool pass=false;};
struct Refs {Decoder* decoder;JNIEnv* env;jobject a=nullptr,b=nullptr,copy=nullptr;jweak wa=nullptr,wb=nullptr,dead=nullptr;
 ~Refs(){for(auto weak:{wa,wb,dead})if(weak){decoder->Forget(weak);env->DeleteWeakGlobalRef(weak);}for(auto strong:{a,b,copy})if(strong){decoder->Forget(strong);env->DeleteGlobalRef(strong);}}
};
}
bool Decoder::Bind(HookEngine& hooks,Bindings bindings,std::string* error){
 if(bound_)return Fail(error,"JNI reference decoder already owns a process receiver");
 if(!bindings.owner||!bindings.vm||!bindings.current_thread||!bindings.strong_oracle||!bindings.native_scope)
  return Fail(error,"JNI reference decoder lacks calibrated current Thread/public VM/strong-root oracle");
 candidates_[0]=reinterpret_cast<StrongLeaf>(hooks.FindSymbol("libart.so",abi::arm64::android14::kDecodeGlobalJObject));
 candidates_[1]=reinterpret_cast<StrongLeaf>(hooks.FindSymbol("libart.so",abi::arm64::android11::kDecodeJObject));
 thread_cleared_=reinterpret_cast<ThreadClearedLeaf>(hooks.FindSymbol("libart.so",kThreadCleared));
 cleared_=reinterpret_cast<ClearedLeaf>(hooks.FindSymbol("libart.so",kCleared));
 if((!candidates_[0]&&!candidates_[1])||(!thread_cleared_&&!cleared_))return Fail(error,"JNI reference decoder lacks fixed named strong/weak/cleared declarations");
 bindings_=bindings;bound_=true;return true;
}
bool Decoder::Register(JNIEnv* env,jobject ref,jobjectRefType kind,std::string* error){
 if(!bound_||!env||!ref||env->ExceptionCheck())return Fail(error,"JNI root registration unavailable");
 JavaVM* actual=nullptr;
 if(env->GetJavaVM(&actual)!=JNI_OK||actual!=bindings_.vm||env->GetObjectRefType(ref)!=kind||env->ExceptionCheck())
  return Fail(error,"JNI root registration differs from actual VM/public reference kind");
 // Public JNI calls finish before acquiring the native handle mutex.
 std::lock_guard<std::mutex> lock(handles_mutex_);auto found=handles_.find(ref);
 if(found!=handles_.end()){if(found->second.kind!=kind)return Fail(error,"JNI reference kind changed without retirement");++found->second.registrations;return true;}
 handles_.emplace(ref,Handle{kind,1});return true;
}
bool Decoder::RegisterStrong(JNIEnv* env,jobject ref,std::string* error){return Register(env,ref,JNIGlobalRefType,error);}
bool Decoder::RegisterWeak(JNIEnv* env,jweak ref,std::string* error){return Register(env,ref,JNIWeakGlobalRefType,error);}
void Decoder::Forget(jobject ref){std::lock_guard<std::mutex> lock(handles_mutex_);auto found=handles_.find(ref);if(found!=handles_.end()&&!--found->second.registrations)handles_.erase(found);}
bool Decoder::Registered(jobject ref,jobjectRefType kind)const{std::lock_guard<std::mutex> lock(handles_mutex_);auto found=handles_.find(ref);return found!=handles_.end()&&found->second.kind==kind;}
bool Decoder::Receiver(const runnable_jni::Context& context)const{return context.self&&context.vm==bindings_.vm&&bindings_.current_thread(bindings_.owner)==context.self;}
bool Decoder::Strong(const runnable_jni::Context& context,Scope,jobject ref,void** out)const{
 if(!ready_||!out||!Receiver(context)||!Registered(ref,JNIGlobalRefType))return false;
 auto result=strong_(context.self,ref);if(!result.reference)return false;
 *out=reinterpret_cast<void*>(result.reference);return true;
}
bool Decoder::IsCleared(void* thread,jweak ref)const{
 return thread_cleared_?thread_cleared_(thread,ref):cleared_(bindings_.vm,thread,static_cast<void*>(ref));
}
bool Decoder::WeakRaw(void* thread,jweak ref,void** out)const{
 if(IsCleared(thread,ref)){*out=nullptr;return true;}
 // Both fixed Thread entry points handle weak globals and normalize the
 // Runtime cleared sentinel themselves. No VM layout or sentinel is inferred.
 auto decoded=thread_weak_(thread,ref);
 // IsJWeakCleared also waits for weak access in fixed ART source. Never carry
 // this borrowed pointer across another witness/private call: the selected
 // Thread leaf already normalizes clearing after its own potentially waiting
 // decode, and that null behavior is independently calibrated against JNI GC.
 *out=reinterpret_cast<void*>(decoded.reference);return true;
}
bool Decoder::Weak(void* thread,Scope scope,jweak ref,void** out)const{
 if(scope==Scope::NoSuspend||!ready_||!out||!thread||bindings_.current_thread(bindings_.owner)!=thread||!Registered(ref,JNIWeakGlobalRefType))return false;
 return WeakRaw(thread,ref,out);
}
bool Decoder::LiveProof(const runnable_jni::Context& context,void* raw){auto& p=*static_cast<Proof*>(raw);auto& d=*p.decoder;
 if(context.env!=p.env||!d.Receiver(context))return false;
 StrongLeaf selected=nullptr;
 for(auto candidate:d.candidates_)if(candidate){bool matched=true;void *prior_a=nullptr,*prior_b=nullptr;
  for(auto ref:{p.a,p.b,p.copy}){void* expected=nullptr;
   if(!d.bindings_.strong_oracle(d.bindings_.owner,context,ref,&expected)||!expected||reinterpret_cast<void*>(candidate(context.self,ref).reference)!=expected){matched=false;break;}
   if(ref==p.a)prior_a=expected;else if(ref==p.b)prior_b=expected;else if(expected!=prior_a)matched=false;
  }
  if(matched&&prior_a!=prior_b){selected=candidate;break;}
 }
 if(!selected)return false;
 d.thread_weak_=selected;
 for(auto pair:{std::pair<jweak,jobject>{p.wa,p.a},{p.wb,p.b}}){void *decoded=nullptr,*expected=nullptr;
  if(!d.WeakRaw(context.self,pair.first,&decoded)||!decoded||!d.bindings_.strong_oracle(d.bindings_.owner,context,pair.second,&expected)||decoded!=expected)return false;
 }
 d.strong_=selected;p.pass=true;return true;
}
bool Decoder::ClearedProof(const runnable_jni::Context& context,void* raw){auto& p=*static_cast<Proof*>(raw);auto& d=*p.decoder;void* decoded=reinterpret_cast<void*>(1);
 p.pass=context.env==p.env&&d.Receiver(context)&&d.IsCleared(context.self,p.dead)&&d.thread_weak_(context.self,p.dead).reference==0&&d.WeakRaw(context.self,p.dead,&decoded)&&decoded==nullptr;return p.pass;
}
bool Decoder::Calibrate(JNIEnv* env,jobject a,jobject b,std::string* error){
 JavaVM* actual=nullptr;
 if(!bound_||!env||!a||!b||env->ExceptionCheck()||env->GetJavaVM(&actual)!=JNI_OK||actual!=bindings_.vm||env->GetObjectRefType(a)!=JNIGlobalRefType||env->GetObjectRefType(b)!=JNIGlobalRefType||env->IsSameObject(a,b))return Fail(error,"JNI reference calibration requires distinct live public globals from its actual VM");
 if(ready_)return true;
 Refs refs{this,env};
 refs.a=env->NewGlobalRef(a);if(!refs.a||env->ExceptionCheck())return false;
 refs.b=env->NewGlobalRef(b);if(!refs.b||env->ExceptionCheck())return false;
 refs.copy=env->NewGlobalRef(refs.a);if(!refs.copy||env->ExceptionCheck()||!env->IsSameObject(refs.a,refs.copy))return false;
 refs.wa=env->NewWeakGlobalRef(refs.a);if(!refs.wa||env->ExceptionCheck())return false;
 refs.wb=env->NewWeakGlobalRef(refs.b);if(!refs.wb||env->ExceptionCheck())return false;
 for(auto global:{refs.a,refs.b,refs.copy})if(!RegisterStrong(env,global,error))return false;
 for(auto weak:{refs.wa,refs.wb}){if(!RegisterWeak(env,weak,error))return false;jobject local=env->NewLocalRef(weak);
  bool same=local&&env->IsSameObject(local,weak);if(local)env->DeleteLocalRef(local);if(!same||env->ExceptionCheck())return false;}
 Proof proof{this,env,refs.a,refs.b,refs.copy,refs.wa,refs.wb,nullptr};
 if(!bindings_.native_scope(bindings_.owner,env,refs.a,LiveProof,&proof)||!proof.pass)return Fail(error,"JNI reference live/receiver/private representation oracle failed");
 jclass object=env->FindClass("java/lang/Object");if(!object||env->ExceptionCheck())return false;
 auto ctor=env->GetMethodID(object,"<init>","()V");jobject temporary=ctor?env->NewObject(object,ctor):nullptr;env->DeleteLocalRef(object);
 if(!temporary||env->ExceptionCheck())return false;
 refs.dead=env->NewWeakGlobalRef(temporary);env->DeleteLocalRef(temporary);if(!refs.dead||env->ExceptionCheck()||!RegisterWeak(env,refs.dead,error))return false;
 jclass system=env->FindClass("java/lang/System");auto gc=system?env->GetStaticMethodID(system,"gc","()V"):nullptr;
 if(!system||!gc||env->ExceptionCheck()){if(system)env->DeleteLocalRef(system);return false;}
 bool observed=false;
 for(size_t i=0;i<8&&!env->ExceptionCheck();++i){env->CallStaticVoidMethod(system,gc);if(env->ExceptionCheck())break;
  jobject local=env->NewLocalRef(refs.dead);if(local){env->DeleteLocalRef(local);continue;}
  if(!env->ExceptionCheck()){observed=true;break;}}
 env->DeleteLocalRef(system);if(!observed)return Fail(error,"Public JNI GC-cleared weak oracle was not observed");
 proof.dead=refs.dead;proof.pass=false;
 if(!bindings_.native_scope(bindings_.owner,env,refs.a,ClearedProof,&proof)||!proof.pass)return Fail(error,"Named cleared witness disagrees with independent public JNI weak/GC oracle");
 ready_=true;return true;
}
} // namespace artdcevm::native_reference_decoder
