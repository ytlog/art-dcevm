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
struct Object {void* native=nullptr;Object* owner=nullptr;NativeField* field=nullptr;jfieldID id=nullptr;bool statik=false;
 bool array=false;std::vector<Object*> elements{};size_t roots=0;};
struct Root {Object* object;bool global;};
Object a{&native_a},b{&native_b},type{&field_type},c{&native_c};
NativeField fa{&native_a},fb{&native_b},fs{&native_a},fc{&native_c},fn{&native_a};
Object ma{nullptr,&a,&fa,reinterpret_cast<jfieldID>(4)},mb{nullptr,&b,&fb,reinterpret_cast<jfieldID>(4)},ms{nullptr,&a,&fs,reinterpret_cast<jfieldID>(8),true};
Object mc{nullptr,&c,&fc,reinterpret_cast<jfieldID>(4)},mn{nullptr,&a,&fn,reinterpret_cast<jfieldID>(16)};
Object* fields[]={&ma,&mb,&ms,&mc,&mn};std::vector<Object> stress;std::vector<NativeField> stress_fields;
size_t globals=0,peak=0,arrays=0,jni_calls=0;bool wrong_id=false,move_oracle=false,move_array=false,fail_container=false,wrong_element=false,fail_set=false,pending_exception=false;Object exception{&vm};
Root* R(jobject ref){return reinterpret_cast<Root*>(ref);}Object* Array(jobject ref){auto* o=R(ref)->object;return static_cast<Object*>(o->native);}
jobject Local(Object* object){auto* root=static_cast<Root*>(std::malloc(sizeof(Root)));assert(root);*root={object,false};++object->roots;return reinterpret_cast<jobject>(root);}
jobject JNICALL Global(JNIEnv*,jobject ref){++jni_calls;if(globals==51200)return nullptr;auto* root=new Root{R(ref)->object,true};++root->object->roots;++globals;peak=std::max(peak,globals);return reinterpret_cast<jobject>(root);}
void JNICALL Delete(JNIEnv*,jobject ref){++jni_calls;if(ref){auto* o=R(ref)->object;if(R(ref)->global)--globals;assert(o->roots);--o->roots;delete R(ref);if(o->array&&!o->roots){if(o->native!=o)delete static_cast<Object*>(o->native);delete o;--arrays;}}}
jobject JNICALL To(JNIEnv*,jclass owner,jfieldID id,jboolean statik){++jni_calls;for(auto* f:fields)if(f->owner==R(owner)->object&&f->id==id&&f->statik==bool(statik))return Local(f);
 uintptr_t value=reinterpret_cast<uintptr_t>(id);if(value>=4096&&value%4==0){size_t index=value/4-1024;if(index<stress.size()&&stress[index].owner==R(owner)->object&&!statik)return Local(&stress[index]);}return nullptr;}
jfieldID JNICALL From(JNIEnv*,jobject mirror){++jni_calls;return wrong_id?reinterpret_cast<jfieldID>(64):R(mirror)->object->id;}
jclass JNICALL Type(JNIEnv*,jobject){++jni_calls;return static_cast<jclass>(Local(&type));}
jmethodID JNICALL Method(JNIEnv*,jclass,const char* name,const char*){++jni_calls;return reinterpret_cast<jmethodID>(std::string(name)=="getDeclaringClass"?&declaring:&modifiers);}
jobject JNICALL CallObject(JNIEnv*,jobject mirror,jmethodID id,va_list){++jni_calls;assert(id==reinterpret_cast<jmethodID>(&declaring));return Local(R(mirror)->object->owner);}
jint JNICALL CallInt(JNIEnv*,jobject mirror,jmethodID id,va_list){++jni_calls;assert(id==reinterpret_cast<jmethodID>(&modifiers));return R(mirror)->object->statik?9:1;}
jboolean JNICALL Same(JNIEnv*,jobject x,jobject y){++jni_calls;return R(x)->object==R(y)->object;}
jboolean JNICALL Check(JNIEnv*){++jni_calls;return pending_exception;}
jthrowable JNICALL Occurred(JNIEnv*){++jni_calls;return pending_exception?static_cast<jthrowable>(Local(&exception)):nullptr;}
void JNICALL ClearException(JNIEnv*){++jni_calls;pending_exception=false;}
jint JNICALL ThrowException(JNIEnv*,jthrowable value){++jni_calls;assert(R(value)->object==&exception);pending_exception=true;return JNI_OK;}
jobjectArray JNICALL NewArray(JNIEnv*,jsize size,jclass,jobject){++jni_calls;auto* object=new Object{};object->array=true;object->native=object;object->elements.resize(size,nullptr);++arrays;return static_cast<jobjectArray>(Local(object));}
jsize JNICALL Length(JNIEnv*,jarray array){++jni_calls;return jsize(Array(array)->elements.size());}
void JNICALL SetElement(JNIEnv*,jobjectArray array,jsize index,jobject value){++jni_calls;Array(array)->elements.at(index)=value?R(value)->object:nullptr;if(value&&fail_set){fail_set=false;pending_exception=true;}}
jobject JNICALL GetElement(JNIEnv*,jobjectArray array,jsize index){++jni_calls;Object* value=Array(array)->elements.at(index);return value?Local(value):nullptr;}
bool Decode(void*,jobject root,void** out){if(!root)return false;Object* o=R(root)->object;*out=o->field?static_cast<void*>(o):o->native;return *out!=nullptr;}
bool Oracle(void*,const runnable_jni::Context&,jobject root,void** out){Object* o=R(root)->object;
 if(move_oracle&&o==&ma){a.native=&moved_a;fa.owner=&moved_a;fs.owner=&moved_a;move_oracle=false;}
 if(move_array&&o->array){auto* copy=new Object{};copy->array=true;copy->elements=static_cast<Object*>(o->native)->elements;copy->native=copy;if(o->native!=o)delete static_cast<Object*>(o->native);o->native=copy;move_array=false;}
 return Decode(nullptr,root,out);}
void* Field(void*,void* mirror){return static_cast<Object*>(mirror)->field;}
void* Owner(void*,void* field){return static_cast<NativeField*>(field)->owner;}
void* Element(void*,void* array,size_t index){auto* object=static_cast<Object*>(array);return wrong_element||index>=object->elements.size()?nullptr:object->elements[index];}
bool Scope(void*,JNIEnv* env,jobject root,runnable_jni::Operation operation,void* data){assert(root&&R(root)->global);const bool pass=operation({&thread,env,reinterpret_cast<JavaVM*>(&vm)},data);if(fail_container){assert(pass);no_alloc=true;}return pass;}
}
int main(){JNINativeInterface_ api{};api.ExceptionCheck=Check;api.ExceptionOccurred=Occurred;api.ExceptionClear=ClearException;api.Throw=ThrowException;api.NewGlobalRef=Global;api.DeleteGlobalRef=Delete;api.DeleteLocalRef=Delete;api.ToReflectedField=To;api.FromReflectedField=From;api.GetObjectClass=Type;api.GetMethodID=Method;api.CallObjectMethodV=CallObject;api.CallIntMethodV=CallInt;api.IsSameObject=Same;api.NewObjectArray=NewArray;api.GetArrayLength=Length;api.SetObjectArrayElement=SetElement;api.GetObjectArrayElement=GetElement;JNIEnv env{&api};
 auto ar=static_cast<jclass>(Local(&a)),br=static_cast<jclass>(Local(&b)),cr=static_cast<jclass>(Local(&c));std::string error;
 rooted_field_identities::Store store({&vm,Scope,Oracle,Decode,Field,Owner,Element});
 move_oracle=true;move_array=true;assert(store.Stage(&env,ar,ma.id,false,&error));assert(a.native==&moved_a&&!move_array);assert(store.Stage(&env,br,mb.id,false,&error));assert(store.Stage(&env,ar,ms.id,true,&error));
 assert(store.Stage(&env,ar,ma.id,false,&error));assert(globals==1&&arrays==1);
 fn.owner=a.native;std::string oom_error;fail_container=true;assert(!store.Stage(&env,ar,mn.id,false,&oom_error));no_alloc=false;assert(globals==1);
 assert(!store.Stage(&env,cr,mc.id,false,nullptr));no_alloc=false;fail_container=false;assert(globals==1);
 assert(!store.Read(ar,mn.id)&&!store.Read(cr,mc.id));
 wrong_element=true;assert(!store.Stage(&env,ar,mn.id,false,&error));wrong_element=false;assert(globals==1&&!store.Read(ar,mn.id));
 fail_set=true;assert(!store.Stage(&env,ar,mn.id,false,&error)&&pending_exception);assert(globals==1&&!store.Read(ar,mn.id));ClearException(&env);
 wrong_id=true;assert(!store.Stage(&env,ar,ma.id,false,&error));wrong_id=false;
 assert(!store.Stage(&env,ar,ms.id,false,&error));
 auto* saved_native=ma.field;fn.owner=a.native;ma.field=&fn;assert(!store.Stage(&env,ar,ma.id,false,&error));ma.field=saved_native;assert(globals==1);
 const auto saved=fa.owner;fa.owner=&native_b;assert(!store.Stage(&env,ar,ma.id,false,&error));fa.owner=saved;
 const size_t before=jni_calls;no_alloc=true;
 assert(store.Read(ar,ma.id)==&fa&&store.Read(br,mb.id)==&fb&&store.Read(ar,ms.id)==&fs);
 assert(!store.Read(br,ms.id)&&!store.Read(ar,reinterpret_cast<jfieldID>(24)));
 auto* original=ma.field;ma.field=&fs;assert(!store.Read(ar,ma.id));ma.field=original;
 fa.owner=&native_b;assert(!store.Read(ar,ma.id));fa.owner=saved;assert(store.Read(ar,ma.id)==&fa);
 no_alloc=false;assert(jni_calls==before);store.Clear(&env);assert(globals==0&&arrays==0);
 wrong_element=true;assert(!store.Stage(&env,ar,ma.id,false,&error));wrong_element=false;assert(globals==0&&arrays==0);store.Clear(&env);
 fail_set=true;assert(!store.Stage(&env,ar,ma.id,false,&error)&&pending_exception);assert(globals==0&&arrays==0);ClearException(&env);store.Clear(&env);
 constexpr size_t count=60000,existing_class_roots=15140;stress.resize(count);stress_fields.resize(count);
 for(size_t i=0;i<count;++i){stress_fields[i].owner=a.native;stress[i]={nullptr,&a,&stress_fields[i],reinterpret_cast<jfieldID>((i+1024)*4)};}
 for(int cycle=0;cycle<3;++cycle){globals=existing_class_roots;peak=globals;
  for(const auto& mirror:stress)assert(store.Stage(&env,ar,mirror.id,false,&error));
  const size_t chunks=(count+rooted_field_identities::Store::kChunkSize-1)/rooted_field_identities::Store::kChunkSize;
  assert(globals==existing_class_roots+chunks&&peak==existing_class_roots+chunks+2&&peak<51200&&arrays==chunks);
  const size_t calls=jni_calls;no_alloc=true;for(size_t i=0;i<count;++i)assert(store.Read(ar,stress[i].id)==&stress_fields[i]);no_alloc=false;assert(jni_calls==calls);
  store.Clear(&env);assert(globals==existing_class_roots&&arrays==0);globals=0;
 }
 Delete(&env,ar);Delete(&env,br);Delete(&env,cr);
 std::cout<<"Actual rooted Field store: 60000 complete identities +15140 existing Class globals under51200 cap, shared slot/GC proof, noJNI/noalloc final reads, duplicate/refund and three clean cycles PASS\n";
}
