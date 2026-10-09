#include "platform/common/rooted_field_identities.h"
#include <cassert>
#include <cstdarg>
#include <cstdlib>
#include <new>
#include <iostream>
using namespace artdcevm;
namespace {bool no_alloc=false;}
void* operator new(size_t n){if(no_alloc)throw std::bad_alloc();if(void* p=std::malloc(n))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,size_t)noexcept{std::free(p);}
namespace {
struct NativeField {void* owner;};int native_a,native_b,moved_a,field_type,thread,vm,declaring,modifiers,native_c;
struct Object {void* native;Object* owner;NativeField* field;jfieldID id;bool statik;};
struct Root {Object* object;bool global;};
Object a{&native_a,nullptr,nullptr,nullptr,false},b{&native_b,nullptr,nullptr,nullptr,false},type{&field_type,nullptr,nullptr,nullptr,false},c{&native_c,nullptr,nullptr,nullptr,false};
NativeField fa{&native_a},fb{&native_b},fs{&native_a},fc{&native_c},fn{&native_a};
Object ma{nullptr,&a,&fa,reinterpret_cast<jfieldID>(4),false},mb{nullptr,&b,&fb,reinterpret_cast<jfieldID>(4),false},ms{nullptr,&a,&fs,reinterpret_cast<jfieldID>(8),true};
Object mc{nullptr,&c,&fc,reinterpret_cast<jfieldID>(4),false},mn{nullptr,&a,&fn,reinterpret_cast<jfieldID>(16),false};
Object* fields[]={&ma,&mb,&ms,&mc,&mn};size_t globals=0;bool wrong_id=false,move_oracle=false,fail_container=false;
Root* R(jobject ref){return reinterpret_cast<Root*>(ref);}jobject Local(Object* object){auto* root=static_cast<Root*>(std::malloc(sizeof(Root)));assert(root);*root={object,false};return reinterpret_cast<jobject>(root);}
jobject JNICALL Global(JNIEnv*,jobject ref){++globals;return reinterpret_cast<jobject>(new Root{R(ref)->object,true});}
void JNICALL Delete(JNIEnv*,jobject ref){if(ref){if(R(ref)->global)--globals;delete R(ref);}}
jobject JNICALL To(JNIEnv*,jclass owner,jfieldID id,jboolean statik){for(auto* f:fields)if(f->owner==R(owner)->object&&f->id==id&&f->statik==bool(statik))return Local(f);return nullptr;}
jfieldID JNICALL From(JNIEnv*,jobject mirror){return wrong_id?reinterpret_cast<jfieldID>(64):R(mirror)->object->id;}
jclass JNICALL Type(JNIEnv*,jobject){return static_cast<jclass>(Local(&type));}
jmethodID JNICALL Method(JNIEnv*,jclass,const char* name,const char*){return reinterpret_cast<jmethodID>(std::string(name)=="getDeclaringClass"?&declaring:&modifiers);}
jobject JNICALL CallObject(JNIEnv*,jobject mirror,jmethodID id,va_list){assert(id==reinterpret_cast<jmethodID>(&declaring));return Local(R(mirror)->object->owner);}
jint JNICALL CallInt(JNIEnv*,jobject mirror,jmethodID id,va_list){assert(id==reinterpret_cast<jmethodID>(&modifiers));return R(mirror)->object->statik?9:1;}
jboolean JNICALL Same(JNIEnv*,jobject x,jobject y){return R(x)->object==R(y)->object;}
jboolean JNICALL Check(JNIEnv*){return false;}
bool Decode(void*,jobject root,void** out){if(!root)return false;Object* o=R(root)->object;*out=o->field?static_cast<void*>(o):o->native;return *out!=nullptr;}
bool Oracle(void*,const runnable_jni::Context&,jobject root,void** out){if(move_oracle&&R(root)->object==&ma){a.native=&moved_a;fa.owner=&moved_a;fs.owner=&moved_a;move_oracle=false;}return Decode(nullptr,root,out);}
void* Field(void*,void* mirror){return static_cast<Object*>(mirror)->field;}
void* Owner(void*,void* field){return static_cast<NativeField*>(field)->owner;}
bool Scope(void*,JNIEnv* env,jobject root,runnable_jni::Operation operation,void* data){assert(root&&R(root)->global);const bool pass=operation({&thread,env,reinterpret_cast<JavaVM*>(&vm)},data);if(fail_container){assert(pass);no_alloc=true;}return pass;}
}
int main(){JNINativeInterface_ api{};api.ExceptionCheck=Check;api.NewGlobalRef=Global;api.DeleteGlobalRef=Delete;api.DeleteLocalRef=Delete;api.ToReflectedField=To;api.FromReflectedField=From;api.GetObjectClass=Type;api.GetMethodID=Method;api.CallObjectMethodV=CallObject;api.CallIntMethodV=CallInt;api.IsSameObject=Same;JNIEnv env{&api};
 auto ar=static_cast<jclass>(Local(&a)),br=static_cast<jclass>(Local(&b)),cr=static_cast<jclass>(Local(&c));std::string error;
 rooted_field_identities::Store store({&vm,Scope,Oracle,Decode,Field,Owner});
 move_oracle=true;assert(store.Stage(&env,ar,ma.id,false,&error));assert(a.native==&moved_a);assert(store.Stage(&env,br,mb.id,false,&error));assert(store.Stage(&env,ar,ms.id,true,&error));
 assert(store.Stage(&env,ar,ma.id,false,&error));assert(globals==3);
 fn.owner=a.native;std::string oom_error;fail_container=true;assert(!store.Stage(&env,ar,mn.id,false,&oom_error));no_alloc=false;assert(globals==3);
 assert(!store.Stage(&env,cr,mc.id,false,nullptr));no_alloc=false;fail_container=false;assert(globals==3);
 assert(!store.Read(ar,mn.id)&&!store.Read(cr,mc.id));
 wrong_id=true;assert(!store.Stage(&env,ar,ma.id,false,&error));wrong_id=false;
 assert(!store.Stage(&env,ar,ms.id,false,&error));
 auto* saved_native=ma.field;fn.owner=a.native;ma.field=&fn;assert(!store.Stage(&env,ar,ma.id,false,&error));ma.field=saved_native;assert(globals==3);
 const auto saved=fa.owner;fa.owner=&native_b;assert(!store.Stage(&env,ar,ma.id,false,&error));fa.owner=saved;
 no_alloc=true;
 assert(store.Read(ar,ma.id)==&fa&&store.Read(br,mb.id)==&fb&&store.Read(ar,ms.id)==&fs);
 assert(!store.Read(br,ms.id)&&!store.Read(ar,reinterpret_cast<jfieldID>(24)));
 auto* original=ma.field;ma.field=&fs;assert(!store.Read(ar,ma.id));ma.field=original;
 fa.owner=&native_b;assert(!store.Read(ar,ma.id));fa.owner=saved;
 assert(store.Read(ar,ma.id)==&fa);
 no_alloc=false;store.Clear(&env);assert(globals==0);assert(!store.Read(ar,ma.id));Delete(&env,ar);Delete(&env,br);Delete(&env,cr);
 std::cout<<"Actual rooted Field store: opaque even-ID collisions by owner, public roundtrip/storage refusal, moved owner after public oracle, exact mirror/native recheck, allocation-failure refundable roots and allocation-free final reads PASS\n";
}
