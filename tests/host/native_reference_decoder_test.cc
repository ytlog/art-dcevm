#include "platform/common/native_reference_decoder.h"
#include <cassert>
#include <cstdarg>
#include <cstring>
#include <memory>
#include <vector>
#include <iostream>
using namespace artdcevm;using namespace native_reference_decoder;
namespace {
struct Object{bool live=true;};struct Ref{jobjectRefType kind;Object* object;bool valid=true;};
std::vector<std::unique_ptr<Ref>> refs;std::vector<std::unique_ptr<Object>> objects;
JavaVM vm{};int thread,wrong,sentinel;int mode=0,strong_calls=0,weak_calls=0,clear_calls=0,current_calls=0,gcs=0;bool clear_during=false,move_during_clear=false;
Ref& Reference(jobject ref){auto& r=*reinterpret_cast<Ref*>(ref);assert(r.valid);return r;}
jobject Reference(Object* object,jobjectRefType kind){refs.push_back(std::make_unique<Ref>(Ref{kind,object}));return reinterpret_cast<jobject>(refs.back().get());}
Object* NewObject(){objects.push_back(std::make_unique<Object>());return objects.back().get();}
jobject JNICALL Global(JNIEnv*,jobject ref){return Reference(Reference(ref).object,JNIGlobalRefType);}
jweak JNICALL WeakRef(JNIEnv*,jobject ref){return Reference(Reference(ref).object,JNIWeakGlobalRefType);}
void JNICALL Delete(JNIEnv*,jobject ref){Reference(ref).valid=false;}
jobject JNICALL Local(JNIEnv*,jobject ref){auto& r=Reference(ref);return r.object->live?Reference(r.object,JNILocalRefType):nullptr;}
jboolean JNICALL Same(JNIEnv*,jobject a,jobject b){return Reference(a).object==Reference(b).object;}
jboolean JNICALL Exception(JNIEnv*){return false;}
jint JNICALL VM(JNIEnv*,JavaVM** result){*result=&vm;return JNI_OK;}
jobjectRefType JNICALL Kind(JNIEnv*,jobject ref){return Reference(ref).kind;}
jclass JNICALL Find(JNIEnv*,const char*){return reinterpret_cast<jclass>(Reference(NewObject(),JNILocalRefType));}
jmethodID JNICALL Method(JNIEnv*,jclass,const char*,const char*){return reinterpret_cast<jmethodID>(&thread);}
jobject JNICALL Construct(JNIEnv*,jclass,jmethodID,va_list){return Reference(NewObject(),JNILocalRefType);}
void JNICALL GC(JNIEnv*,jclass,jmethodID,va_list){++gcs;if(mode==5)return;
 for(auto& object:objects){bool rooted=false;for(auto& ref:refs)if(ref->valid&&ref->object==object.get()&&(ref->kind==JNIGlobalRefType||ref->kind==JNILocalRefType))rooted=true;if(!rooted)object->live=false;}}
void* Current(void*){++current_calls;return &thread;}
using Carrier=abi::arm64::android11::ReleaseObjectPointer;
Carrier Weak(void* receiver,void* self,void* raw){assert(receiver==&vm&&self==&thread);++weak_calls;auto& r=Reference(reinterpret_cast<jobject>(raw));assert(r.kind==JNIWeakGlobalRefType);
 if(clear_during)r.object->live=false;
 return {reinterpret_cast<uintptr_t>(mode==3?static_cast<void*>(&wrong):r.object->live?static_cast<void*>(r.object):static_cast<void*>(&sentinel))};}
bool Cleared(void* receiver,void* self,void* raw){assert(receiver==&vm&&self==&thread);++clear_calls;
 if(move_during_clear){move_during_clear=false;auto* old=Reference(reinterpret_cast<jobject>(raw)).object;auto* moved=NewObject();moved->live=old->live;for(auto& r:refs)if(r->valid&&r->object==old)r->object=moved;old->live=false;}
 return mode==4?false:!Reference(reinterpret_cast<jobject>(raw)).object->live;}
Carrier Strong(const void* self,jobject ref){assert(self==&thread);auto& r=Reference(ref);
 if(r.kind==JNIWeakGlobalRefType){auto result=Weak(&vm,const_cast<void*>(self),ref);return !r.object->live?Carrier{0}:result;}
 assert(r.kind==JNIGlobalRefType);++strong_calls;return {reinterpret_cast<uintptr_t>(mode==2?static_cast<void*>(&wrong):static_cast<void*>(r.object))};}
bool ThreadCleared(const void* self,jweak ref){return Cleared(&vm,const_cast<void*>(self),ref);}
bool Oracle(void*,const runnable_jni::Context& context,jobject ref,void** out){assert(context.self==&thread);*out=Reference(ref).object;return true;}
bool EnterScope(void*,JNIEnv* env,jobject root,runnable_jni::Operation operation,void* data){assert(Reference(root).kind==JNIGlobalRefType);return operation({mode==6?&wrong:&thread,env,&vm},data);}
struct Engine:HookEngine {
 bool Initialize(std::string*)override{return true;}void* Install(void*,void*,void**,std::string*)override{return nullptr;}bool Remove(void*,std::string*)override{return false;}
 void* FindSymbol(const char*,const char* name)override{
  if(!std::strcmp(name,"_ZNK3art6Thread19DecodeGlobalJObjectEP8_jobject"))return mode==1?nullptr:reinterpret_cast<void*>(Strong);
  if(!std::strcmp(name,"_ZNK3art6Thread13DecodeJObjectEP8_jobject"))return reinterpret_cast<void*>(Strong);
  if(!std::strcmp(name,"_ZNK3art6Thread14IsJWeakClearedEP8_jobject"))return mode==8?nullptr:reinterpret_cast<void*>(ThreadCleared);
  if(!std::strcmp(name,"_ZN3art9JavaVMExt19IsWeakGlobalClearedEPNS_6ThreadEPv"))return reinterpret_cast<void*>(Cleared);
  return nullptr;
 }
};
}
int main(int argc,char** argv){assert(argc==2);mode=argv[1][0]-'0';
 JNINativeInterface_ functions{};functions.NewGlobalRef=Global;functions.NewWeakGlobalRef=WeakRef;functions.DeleteGlobalRef=Delete;functions.DeleteWeakGlobalRef=Delete;functions.DeleteLocalRef=Delete;functions.NewLocalRef=Local;functions.IsSameObject=Same;functions.ExceptionCheck=Exception;functions.GetJavaVM=VM;functions.GetObjectRefType=Kind;functions.FindClass=Find;functions.GetMethodID=Method;functions.GetStaticMethodID=Method;functions.NewObjectV=Construct;functions.CallStaticVoidMethodV=GC;JNIEnv env{&functions};
 jobject a=Reference(NewObject(),JNIGlobalRefType),b=Reference(NewObject(),JNIGlobalRefType);Engine hooks;Decoder decoder;std::string error;
 assert(decoder.Bind(hooks,{&thread,mode==7?reinterpret_cast<JavaVM*>(&wrong):&vm,Current,Oracle,EnterScope},&error));
 bool pass=decoder.Calibrate(&env,a,b,&error);assert(pass==(mode==0||mode==1||mode==8));assert(decoder.Ready()==pass);
 if(!pass){void* out=nullptr;int before=weak_calls;assert(!decoder.Weak(&thread,Scope::Runnable,reinterpret_cast<jweak>(a),&out)&&weak_calls==before);if(mode==4)assert(gcs>0);std::cout<<"JNI decoder calibration refusal mode "<<mode<<" passed\n";return 0;}
 assert(!decoder.Bind(hooks,{&thread,&vm,Current,Oracle,EnterScope},&error));
 jobject local=Local(&env,a);assert(!decoder.RegisterStrong(&env,local,&error));Delete(&env,local);
 assert(decoder.RegisterStrong(&env,a,&error)&&decoder.RegisterStrong(&env,a,&error));jweak wa=WeakRef(&env,a);assert(decoder.RegisterWeak(&env,wa,&error)&&!decoder.RegisterStrong(&env,wa,&error));
 runnable_jni::Context context{&thread,&env,&vm};void* out=nullptr;
 assert(decoder.Strong(context,Scope::NoSuspend,a,&out)&&out==Reference(a).object);
 decoder.Forget(a);assert(decoder.Strong(context,Scope::Runnable,a,&out));decoder.Forget(a);assert(!decoder.Strong(context,Scope::NoSuspend,a,&out));
 int before_weak=weak_calls,before_clear=clear_calls,before_current=current_calls;
 assert(!decoder.Weak(&thread,Scope::NoSuspend,wa,&out)&&weak_calls==before_weak&&clear_calls==before_clear&&current_calls==before_current);
 assert(!decoder.Weak(&wrong,Scope::Runnable,wa,&out)&&weak_calls==before_weak);assert(decoder.Weak(&thread,Scope::Runnable,wa,&out)&&out==Reference(a).object);
 // The independent witness may block/checkpoint before decode. Relocate all
 // rooted handles there; only the subsequent source leaf's fresh value may
 // escape. Exactly one witness proves no raw pointer crosses a second call.
 before_clear=clear_calls;before_weak=weak_calls;move_during_clear=true;
 assert(decoder.Weak(&thread,Scope::Runnable,wa,&out)&&out==Reference(a).object&&clear_calls==before_clear+1&&weak_calls==before_weak+1);
 jobject temporary=Reference(NewObject(),JNILocalRefType);jweak dead=WeakRef(&env,temporary);Delete(&env,temporary);assert(decoder.RegisterWeak(&env,dead,&error));clear_during=true;
 assert(decoder.Weak(&thread,Scope::Runnable,dead,&out)&&out==nullptr&&Local(&env,dead)==nullptr); // clear AFTER blocking decoder's first witness
 decoder.Forget(dead);Delete(&env,dead);decoder.Forget(wa);before_weak=weak_calls;assert(!decoder.Weak(&thread,Scope::Runnable,wa,&out)&&weak_calls==before_weak);Delete(&env,wa);
 std::cout<<"Actual JNI decoder TU: source named receivers, registered types, live/GC cleared oracles, source-normalized clear, predecode witness relocation, no postdecode witness, NoSuspend refusal and registration retirement passed\n";
}
