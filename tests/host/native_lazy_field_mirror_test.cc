#include "platform/common/native_lazy_field_mirror.h"
#include <cassert>
#include <cstring>
#include <memory>
#include <iostream>
using namespace artdcevm;
namespace lazy=native_lazy_field_mirror;
struct Class{};struct Field {Class* owner;bool statik;bool missing_type=false;};struct Mirror {Field* field;};
static Class a,b,mirror_class;static int self,owner;static bool pending=false,oom=false,wrong_factory=false,foreign_context=false;
static size_t globals=0,calls=0,local_calls=0,public_calls=0;static std::vector<std::unique_ptr<Mirror>> mirrors;
static Field first{&a,false},second{&a,true},unresolved{&b,false,true};static Mirror m1{&first},m2{&second};
static uintptr_t Factory(void* thread,void* raw,bool force){assert(thread==&self&&!force);++calls;
 if(oom){pending=true;return 0;}auto* f=static_cast<Field*>(raw);
 // Original source force=false tolerates its own missing-type resolution and
 // clears that resolution exception internally. The service never clears it.
 if(f->missing_type){pending=true;pending=false;}
 auto m=std::make_unique<Mirror>();m->field=wrong_factory?&second:f;auto p=m.get();mirrors.push_back(std::move(m));return uintptr_t(p);
}
static jobject Local(JNIEnv*,void* m){assert(m);++local_calls;return reinterpret_cast<jobject>(m);}
static jobject JNICALL Global(JNIEnv*,jobject p){assert(!pending);if(p)++globals;return p;}
static void JNICALL DeleteGlobal(JNIEnv*,jobject p){if(p){assert(globals);--globals;}}
static void JNICALL DeleteLocal(JNIEnv*,jobject){}
static jboolean JNICALL Exception(JNIEnv*){return pending;}
static jfieldID JNICALL From(JNIEnv*,jobject p){assert(!pending);++public_calls;return reinterpret_cast<jfieldID>(reinterpret_cast<Mirror*>(p)->field);}
static jclass JNICALL GetClass(JNIEnv*,jobject){assert(!pending);++public_calls;return reinterpret_cast<jclass>(&mirror_class);}
static jmethodID JNICALL Method(JNIEnv*,jclass c,const char* n,const char*){assert(!pending&&c==reinterpret_cast<jclass>(&mirror_class));++public_calls;return reinterpret_cast<jmethodID>(!std::strcmp(n,"getDeclaringClass")?1:2);}
static jobject JNICALL Call(JNIEnv*,jobject p,jmethodID id,va_list){assert(!pending&&id==reinterpret_cast<jmethodID>(1));++public_calls;return reinterpret_cast<jobject>(reinterpret_cast<Mirror*>(p)->field->owner);}
static jint JNICALL Int(JNIEnv*,jobject p,jmethodID id,va_list){assert(!pending&&id==reinterpret_cast<jmethodID>(2));++public_calls;return reinterpret_cast<Mirror*>(p)->field->statik?8:0;}
static jboolean JNICALL Same(JNIEnv*,jobject x,jobject y){assert(!pending);++public_calls;return x==y;}
class Hooks:public HookEngine {public:bool legacy=false,absent=false,four=false;bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char*l,const char*n)override{assert(!std::strcmp(l,"libart.so"));
  if(!std::strcmp(n,"_ZN3art9JNIEnvExt11NewLocalRefEPNS_6mirror6ObjectE"))return reinterpret_cast<void*>(Local);
  if(!std::strcmp(n,"_ZN3art6mirror5Field18CreateFromArtFieldEPNS_6ThreadEPNS_8ArtFieldEb"))return !absent&&!legacy&&!four?reinterpret_cast<void*>(Factory):nullptr;
  if(!std::strcmp(n,"_ZN3art6mirror5Field18CreateFromArtFieldILNS_11PointerSizeE8ELb0EEENS_6ObjPtrIS1_EEPNS_6ThreadEPNS_8ArtFieldEb"))return !absent&&legacy&&!four?reinterpret_cast<void*>(Factory):nullptr;
  if(!std::strcmp(n,"_ZN3art6mirror5Field18CreateFromArtFieldEPNS_6ThreadENS_6HandleINS0_5ClassEEEPNS_8ArtFieldEb"))return four?reinterpret_cast<void*>(Factory):nullptr;
  assert(false);return nullptr;
 }
 void* Install(void*,void*,void**,std::string*)override{assert(false);return nullptr;}bool Remove(void*,std::string*)override{assert(false);return false;}};
static bool Scope(void*,JNIEnv* e,jobject carrier,runnable_jni::Operation op,void*p){assert(carrier);runnable_jni::Context c{&self,foreign_context?nullptr:e,&owner};return op(c,p);}
static bool Oracle(void*,const runnable_jni::Context&,jobject p,void**out){*out=p;return true;}
static bool Decode(void*,jobject p,void**out){*out=p;return true;}
static void* FieldId(void*,const runnable_jni::Context&,void*c,jfieldID id){auto*f=reinterpret_cast<Field*>(id);return f->owner==c?f:nullptr;}
static void* MirrorField(void*,void*m){return static_cast<Mirror*>(m)->field;}
static void* Declaring(void*,void*f){return static_cast<Field*>(f)->owner;}
int main(){JNINativeInterface_ jni{};jni.NewGlobalRef=Global;jni.DeleteGlobalRef=DeleteGlobal;jni.DeleteLocalRef=DeleteLocal;jni.ExceptionCheck=Exception;jni.FromReflectedField=From;jni.GetObjectClass=GetClass;jni.GetMethodID=Method;jni.CallObjectMethodV=Call;jni.CallIntMethodV=Int;jni.IsSameObject=Same;JNIEnv env{&jni};
 lazy::Bindings binding{&owner,true,Scope,Oracle,Decode,FieldId,MirrorField,Declaring};
 std::vector<lazy::Oracle> samples{{reinterpret_cast<jobject>(&m1),reinterpret_cast<jclass>(&a),reinterpret_cast<jfieldID>(&first),&first,false},{reinterpret_cast<jobject>(&m2),reinterpret_cast<jclass>(&a),reinterpret_cast<jfieldID>(&second),&second,true}};
 std::string error;
 for(bool legacy:{false,true}){Hooks hooks;hooks.legacy=legacy;lazy::Service service;assert(service.Bind(hooks,binding,&error));jobject result=nullptr;
  assert(!service.Create(&env,reinterpret_cast<jclass>(&a),samples[0].id,false,&result,&error));
  auto duplicate=samples;duplicate[1]=duplicate[0];const auto before=calls;assert(!service.Calibrate(&env,duplicate,&error)&&calls==before);
  assert(service.Calibrate(&env,samples,&error)&&service.Ready()&&calls==before+2&&globals==1);
  assert(service.Create(&env,reinterpret_cast<jclass>(&b),reinterpret_cast<jfieldID>(&unresolved),false,&result,&error)&&result);env.DeleteGlobalRef(result);assert(globals==1);
  assert(!service.Create(&env,reinterpret_cast<jclass>(&b),reinterpret_cast<jfieldID>(&unresolved),true,&result,&error)&&globals==1);
  const auto c=calls,p=public_calls;pending=true;assert(!service.Create(&env,reinterpret_cast<jclass>(&b),reinterpret_cast<jfieldID>(&unresolved),false,&result,&error)&&pending&&calls==c&&public_calls==p);pending=false;
  assert(!service.Create(&env,reinterpret_cast<jclass>(&a),reinterpret_cast<jfieldID>(&unresolved),false,&result,&error)&&calls==c);
  oom=true;assert(!service.Create(&env,reinterpret_cast<jclass>(&b),reinterpret_cast<jfieldID>(&unresolved),false,&result,&error)&&pending);oom=false;pending=false;
  wrong_factory=true;assert(!service.Create(&env,reinterpret_cast<jclass>(&b),reinterpret_cast<jfieldID>(&unresolved),false,&result,&error));wrong_factory=false;assert(globals==1);
  foreign_context=true;assert(!service.Create(&env,reinterpret_cast<jclass>(&b),reinterpret_cast<jfieldID>(&unresolved),false,&result,&error));foreign_context=false;
  service.Clear(&env);assert(!globals);
 }
 Hooks absent;absent.absent=true;lazy::Service unsupported;assert(!unsupported.Bind(absent,binding,&error));
 Hooks four;four.four=true;lazy::Service missing;const auto before_four=calls;assert(!missing.Bind(four,binding,&error)&&calls==before_four);
 native_field_factory_observer::Service observer;source_handle_roots::Service uncalibrated;
 native_field_factory_observer::Trace primitive{},optional{};std::array<bool,4> roots{{true,true,true,true}};
 auto incomplete=binding;incomplete.observer=&observer;incomplete.roots=&uncalibrated;incomplete.primitive_trace=&primitive;incomplete.optional_trace=&optional;incomplete.public_roots=&roots;
 lazy::Service unproven;assert(!unproven.Bind(four,incomplete,&error)&&calls==before_four&&!unproven.Ready());
 Hooks hooks;lazy::Service wrong;assert(wrong.Bind(hooks,binding,&error));wrong_factory=true;assert(!wrong.Calibrate(&env,samples,&error)&&!wrong.Ready()&&!globals);wrong_factory=false;
 std::cout<<"Lazy source Field factories: two public/native oracles, opaque ID/owner/static identity, original false missing-type semantics, incoming/OOME preservation, wrong codec/namespace/context rejection and reference cleanup PASS\n";
}
