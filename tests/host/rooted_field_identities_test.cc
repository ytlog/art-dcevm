#include "platform/common/rooted_field_identities.h"
#include "platform/common/native_schema_guard_verifier.cc"
#include <cassert>
#include <cstdarg>
#include <cstdlib>
#include <new>
#include <iostream>
using namespace artdcevm;
namespace {bool no_alloc=false;}
namespace artdcevm::redefinition_gate {bool Owned(JavaVM*){return true;}}
// This regression reaches real public Stage/Clear, never native Seal/Verify.
// Unused virtual-table collaborators abort if accidentally invoked; they do
// not attest an ART layout or grant a native capability in this host test.
namespace artdcevm::modern_field_metadata {
bool FieldMetadata::ClassIdentity(void*,field_copy::CopyClassView*,std::string*,const ReadContext*)const{std::abort();}
bool FieldMetadata::ClassIdentityVerified(void*,const char*,void*,const char*,const char*,field_copy::CopyClassView*,std::string*,const ReadContext*)const{std::abort();}
bool FieldMetadata::FieldIdentity(void*,field_copy::CopyFieldView*,std::string*,const ReadContext*)const{std::abort();}
void* FieldMetadata::FieldDeclaringClass(void*)const{std::abort();}
void* FieldMetadata::Reference(void*,size_t,bool)const{std::abort();}
}
namespace artdcevm::native_abstract_instances {
bool Service::Validate(const source_commit::Plan&,size_t,const modern_field_metadata::FieldMetadata&,std::string*,const modern_field_metadata::ReadContext*)const{std::abort();}
}
namespace artdcevm::native_schema_policy {
bool ReadNativeDeclarations(const modern_field_metadata::FieldMetadata&,const method_identity::Calibration&,void*,evolution::ClassSchema*,std::string*,native_method_table::View*,const modern_field_metadata::ReadContext*){std::abort();}
}

void* operator new(size_t n){if(no_alloc)throw std::bad_alloc();if(void* p=std::malloc(n))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,size_t)noexcept{std::free(p);}
namespace {
struct NativeField {void* owner;};int native_a,native_b,moved_a,field_type,thread,vm,declaring,modifiers,native_c,name_method,component_method,proxy_method;
struct Object {void* native=nullptr;Object* owner=nullptr;NativeField* field=nullptr;jfieldID id=nullptr;bool statik=false;
 bool array=false;std::vector<Object*> elements{};size_t roots=0;std::string class_name{};bool proxy=false;};
struct Root {Object* object;bool global;};
Object class_api{},proxy_api{};std::vector<Object> public_names;bool fail_proxy=false;
Object a{&native_a},b{&native_b},type{&field_type},c{&native_c};
NativeField fa{&native_a},fb{&native_b},fs{&native_a},fc{&native_c},fn{&native_a};
Object ma{nullptr,&a,&fa,reinterpret_cast<jfieldID>(4)},mb{nullptr,&b,&fb,reinterpret_cast<jfieldID>(4)},ms{nullptr,&a,&fs,reinterpret_cast<jfieldID>(8),true};
Object mc{nullptr,&c,&fc,reinterpret_cast<jfieldID>(4)},mn{nullptr,&a,&fn,reinterpret_cast<jfieldID>(16)};
Object* fields[]={&ma,&mb,&ms,&mc,&mn};std::vector<Object> stress;std::vector<NativeField> stress_fields;
size_t globals=0,peak=0,arrays=0,jni_calls=0,scope_calls=0;bool wrong_id=false,move_oracle=false,move_array=false,fail_container=false,wrong_element=false,fail_set=false,pending_exception=false;Object exception{&vm};void* oracle_move_target=&moved_a;
Root* R(jobject ref){return reinterpret_cast<Root*>(ref);}Object* Array(jobject ref){auto* o=R(ref)->object;return static_cast<Object*>(o->native);}
jobject Local(Object* object){auto* root=static_cast<Root*>(std::malloc(sizeof(Root)));assert(root);*root={object,false};++object->roots;return reinterpret_cast<jobject>(root);}
jobject JNICALL LocalCopy(JNIEnv*,jobject ref){++jni_calls;return ref?Local(R(ref)->object):nullptr;}
jobject JNICALL Global(JNIEnv*,jobject ref){++jni_calls;if(globals==51200)return nullptr;auto* root=new Root{R(ref)->object,true};++root->object->roots;++globals;peak=std::max(peak,globals);return reinterpret_cast<jobject>(root);}
void JNICALL Delete(JNIEnv*,jobject ref){++jni_calls;if(ref){auto* o=R(ref)->object;if(R(ref)->global)--globals;assert(o->roots);--o->roots;delete R(ref);if(o->array&&!o->roots){if(o->native!=o)delete static_cast<Object*>(o->native);delete o;--arrays;}}}
bool refuse_public_mirror=false,bad_lazy_local=false;size_t public_mirror_calls=0,lazy_calls=0;
jobject JNICALL To(JNIEnv*,jclass owner,jfieldID id,jboolean statik){++jni_calls;++public_mirror_calls;if(refuse_public_mirror){pending_exception=true;return nullptr;}for(auto* f:fields)if(f->owner==R(owner)->object&&f->id==id&&f->statik==bool(statik))return Local(f);
 uintptr_t value=reinterpret_cast<uintptr_t>(id);if(value>=4096&&value%4==0){size_t index=value/4-1024;if(index<stress.size()&&stress[index].owner==R(owner)->object&&!statik)return Local(&stress[index]);}return nullptr;}
jobjectRefType JNICALL RefType(JNIEnv*,jobject root){return R(root)->global?JNIGlobalRefType:JNILocalRefType;}
bool LazyMirror(void*,JNIEnv* env,jclass owner,jfieldID id,bool statik,jobject*out,std::string*){
 ++lazy_calls;assert(!pending_exception);*out=nullptr;
 for(auto* f:fields)if(f->owner==R(owner)->object&&f->id==id&&f->statik==statik){auto local=Local(f);if(bad_lazy_local){*out=local;return true;}*out=Global(env,local);Delete(env,local);return true;}return false;
}
jfieldID JNICALL From(JNIEnv*,jobject mirror){++jni_calls;return wrong_id?reinterpret_cast<jfieldID>(64):R(mirror)->object->id;}
jclass JNICALL Find(JNIEnv*,const char* name){++jni_calls;Object* result=&type;
 if(std::strcmp(name,"java/lang/Class")==0)result=&class_api;
 if(std::strcmp(name,"java/lang/reflect/Proxy")==0)result=&proxy_api;
 return static_cast<jclass>(Local(result));}
jclass JNICALL Type(JNIEnv*,jobject){++jni_calls;return static_cast<jclass>(Local(&type));}
jmethodID JNICALL Method(JNIEnv*,jclass klass,const char* name,const char* signature){++jni_calls;
 if(R(klass)->object==&class_api){
  if(std::strcmp(name,"getName")==0&&std::strcmp(signature,"()Ljava/lang/String;")==0)return reinterpret_cast<jmethodID>(&name_method);
  if(std::strcmp(name,"getComponentType")==0&&std::strcmp(signature,"()Ljava/lang/Class;")==0)return reinterpret_cast<jmethodID>(&component_method);
  std::abort();
 }
 return reinterpret_cast<jmethodID>(std::string(name)=="getDeclaringClass"?&declaring:&modifiers);}
jmethodID JNICALL StaticMethod(JNIEnv*,jclass klass,const char* name,const char* signature){++jni_calls;
 assert(R(klass)->object==&proxy_api&&std::strcmp(name,"isProxyClass")==0&&std::strcmp(signature,"(Ljava/lang/Class;)Z")==0);
 return reinterpret_cast<jmethodID>(&proxy_method);}
jboolean JNICALL ProxyClass(JNIEnv*,jclass klass,jmethodID method,const jvalue* arguments){++jni_calls;
 assert(R(klass)->object==&proxy_api&&method==reinterpret_cast<jmethodID>(&proxy_method)&&arguments&&arguments[0].l);
 if(fail_proxy){pending_exception=true;return JNI_FALSE;}
 return R(arguments[0].l)->object->proxy?JNI_TRUE:JNI_FALSE;}
jobject JNICALL ClassObject(JNIEnv*,jobject klass,jmethodID method,const jvalue*){++jni_calls;
 Object* actual=R(klass)->object;assert(!actual->class_name.empty());
 if(method==reinterpret_cast<jmethodID>(&component_method))return nullptr; // These are ordinary non-array Classes.
 assert(method==reinterpret_cast<jmethodID>(&name_method));
 for(auto& name:public_names)if(name.owner==actual)return Local(&name);
 std::abort();}
const char* JNICALL StringChars(JNIEnv*,jstring string,jboolean*){++jni_calls;auto* actual=R(string)->object;assert(actual->owner);return actual->class_name.c_str();}
void JNICALL ReleaseChars(JNIEnv*,jstring string,const char* chars){++jni_calls;assert(chars==R(string)->object->class_name.c_str());}
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
 if(move_oracle&&o==&ma){a.native=oracle_move_target;fa.owner=oracle_move_target;fs.owner=oracle_move_target;move_oracle=false;}
 if(move_array&&o->array){auto* copy=new Object{};copy->array=true;copy->elements=static_cast<Object*>(o->native)->elements;copy->native=copy;if(o->native!=o)delete static_cast<Object*>(o->native);o->native=copy;move_array=false;}
 return Decode(nullptr,root,out);}
void* Field(void*,void* mirror){return static_cast<Object*>(mirror)->field;}
void* Owner(void*,void* field){return static_cast<NativeField*>(field)->owner;}
void* Element(void*,void* array,size_t index){auto* object=static_cast<Object*>(array);return wrong_element||index>=object->elements.size()?nullptr:object->elements[index];}
bool Pair(void*owner,const runnable_jni::Context&c,jobject a,jobject b,void**left,void**right){return Oracle(owner,c,a,left)&&Oracle(owner,c,b,right);}
bool Scope(void*,JNIEnv* env,jobject root,runnable_jni::Operation operation,void* data){assert(root&&R(root)->global);++scope_calls;const auto before_native=jni_calls;const bool pass=operation({&thread,env,reinterpret_cast<JavaVM*>(&vm)},data);assert(jni_calls==before_native);if(fail_container){assert(pass);no_alloc=true;}return pass;}
std::vector<Object>* loaded_objects=nullptr;
jclass JNICALL Parent(JNIEnv*,jclass){return static_cast<jclass>(Local(&loaded_objects->front()));}
jboolean JNICALL Assignable(JNIEnv*,jclass x,jclass y){return R(x)->object==R(y)->object;}
jvmtiError JNICALL Loaded(jvmtiEnv*,jint* count,jclass** classes){*count=jint(loaded_objects->size());*classes=static_cast<jclass*>(std::malloc(sizeof(jclass)*size_t(*count)));for(jint i=0;i<*count;++i)(*classes)[i]=static_cast<jclass>(Local(&(*loaded_objects)[size_t(i)]));return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Deallocate(jvmtiEnv*,unsigned char* p){std::free(p);return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Signature(jvmtiEnv*,jclass klass,char** signature,char**){const auto text="L"+R(klass)->object->class_name+";";*signature=static_cast<char*>(std::malloc(text.size()+1));std::memcpy(*signature,text.c_str(),text.size()+1);return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Status(jvmtiEnv*,jclass,jint* status){*status=JVMTI_CLASS_STATUS_PREPARED;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL ClassFlags(jvmtiEnv*,jclass,jint* flags){*flags=1;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Methods(jvmtiEnv*,jclass,jint* count,jmethodID** values){*count=0;*values=nullptr;return JVMTI_ERROR_NONE;}
bool declaration_failure=false;
jvmtiError JNICALL Fields(jvmtiEnv*,jclass,jint* count,jfieldID** values){*count=declaration_failure?1:0;*values=nullptr;if(*count){*values=static_cast<jfieldID*>(std::malloc(sizeof(jfieldID)));(*values)[0]=ma.id;}return JVMTI_ERROR_NONE;}
jvmtiError JNICALL DeclarationName(jvmtiEnv*,jclass,jfieldID,char** name,char** signature,char**){*name=strdup("optional");*signature=strdup("Loptional/Missing;");return JVMTI_ERROR_NONE;}
jvmtiError JNICALL DeclarationFlags(jvmtiEnv*,jclass,jfieldID,jint* flags){*flags=1;return JVMTI_ERROR_NONE;}
bool DeclaringFailure(void*,JNIEnv*,jclass,jfieldID,bool,std::string*error){pending_exception=true;if(error)*error="Public contextual Field round trip unavailable";return false;}
jvmtiError JNICALL Loader(jvmtiEnv*,jclass,jobject* value){*value=Local(&loaded_objects->front());return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Interfaces(jvmtiEnv*,jclass,jint* count,jclass** values){*count=1;*values=static_cast<jclass*>(std::malloc(sizeof(jclass)));(*values)[0]=static_cast<jclass>(Local(&loaded_objects->front()));return JVMTI_ERROR_NONE;}

}
int main(){JNINativeInterface_ api{};api.GetObjectRefType=RefType;api.FindClass=Find;api.ExceptionCheck=Check;api.ExceptionOccurred=Occurred;api.ExceptionClear=ClearException;api.Throw=ThrowException;api.NewGlobalRef=Global;api.NewLocalRef=LocalCopy;api.DeleteGlobalRef=Delete;api.DeleteLocalRef=Delete;api.ToReflectedField=To;api.FromReflectedField=From;api.GetObjectClass=Type;api.GetMethodID=Method;api.CallObjectMethodV=CallObject;api.CallIntMethodV=CallInt;api.IsSameObject=Same;api.NewObjectArray=NewArray;api.GetArrayLength=Length;api.SetObjectArrayElement=SetElement;api.GetObjectArrayElement=GetElement;JNIEnv env{&api};
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
 assert(store.ReadNative(a.native,ma.id)==&fa&&store.ReadNative(b.native,mb.id)==&fb);
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
 // The same production ObjectArray/barrier protocol now holds Class,
 // ClassLoader, superclass and interface references in the schema verifier.
 // Retain 180000 distinct roots, not a truncation or a weak Class lease.
 bounded_managed_roots::Store roots({&vm,Scope,Oracle,Decode,Element,Pair});
 constexpr size_t root_count=180000;std::vector<Object> objects(root_count);
 std::vector<bounded_managed_roots::Slot> slots(root_count);
 for(size_t i=0;i<root_count;++i)objects[i].native=&objects[i];
 for(int cycle=0;cycle<3;++cycle){globals=existing_class_roots;peak=globals;
  const size_t scope_before=scope_calls;
  if(cycle==2){
   for(size_t start=0;start<root_count;start+=bounded_managed_roots::Store::kCohortSize){
    std::vector<jobject> refs;for(size_t i=start;i<std::min(root_count,start+bounded_managed_roots::Store::kCohortSize);++i)refs.push_back(Local(&objects[i]));
    std::vector<bounded_managed_roots::Slot> published;assert(roots.StageMany(&env,refs,&published,&error));assert(published.size()==refs.size());
    for(size_t i=0;i<refs.size();++i){slots[start+i]=published[i];Delete(&env,refs[i]);}
   }
   assert(scope_calls-scope_before==(root_count+127)/128);
  }else for(size_t i=0;i<root_count;++i){auto ref=Local(&objects[i]);assert(cycle==1?roots.Stage(runnable_jni::Context{&thread,&env,reinterpret_cast<JavaVM*>(&vm)},ref,&slots[i],&error):roots.Stage(&env,ref,&slots[i],&error));Delete(&env,ref);}
  const size_t chunks=(root_count+1023)/1024;assert(globals==existing_class_roots+chunks&&peak<=globals+2&&peak<51200);
  const size_t calls=jni_calls;no_alloc=true;for(size_t i=0;i<root_count;++i){void* native=nullptr;assert(roots.Read(slots[i],&native)&&native==objects[i].native);}no_alloc=false;assert(jni_calls==calls);
  move_array=true;auto ref=Local(&objects[0]);bounded_managed_roots::Slot gc_slot;assert(roots.Stage(&env,ref,&gc_slot,&error)&&!move_array);Delete(&env,ref);void* native=nullptr;assert(roots.Read(gc_slot,&native)&&native==objects[0].native);
  roots.Clear(&env);assert(globals==existing_class_roots&&arrays==0&&roots.Empty());globals=0;
 }
 bounded_managed_roots::Slot rejected;wrong_element=true;assert(!roots.Stage(&env,ar,&rejected,&error)&&!rejected);wrong_element=false;assert(globals==0&&arrays==0);
 fail_set=true;assert(!roots.Stage(&env,ar,&rejected,&error)&&pending_exception&&!rejected);assert(globals==0&&arrays==0);ClearException(&env);
 auto object_ref=Local(&objects[0]);assert(roots.Stage(&env,object_ref,&rejected,&error));assert(roots.Same(&env,rejected,object_ref));Delete(&env,object_ref);size_t baseline=globals;
 wrong_element=true;bounded_managed_roots::Slot skipped;assert(!roots.Stage(&env,br,&skipped,&error)&&!skipped&&globals==baseline);wrong_element=false;
 void* native=nullptr;assert(roots.Read(rejected,&native)&&native==objects[0].native);roots.Clear(&env);assert(globals==0);
 // A cohort crossing the retained array boundary refunds both the old tail
 // and newly created chunk, preserves pending exception and publishes nothing.
 {
  std::vector<jobject> refs;for(size_t i=0;i<128;++i)refs.push_back(Local(&objects[i]));
  std::vector<bounded_managed_roots::Slot> published;
  for(size_t i=0;i<7;++i)assert(roots.StageMany(&env,refs,&published,&error));
  std::vector<jobject> tail(refs.begin(),refs.begin()+120);assert(roots.StageMany(&env,tail,&published,&error));
  assert(published.back().index==1015);auto retained=published.front();const auto baseline=globals;
  move_array=true;assert(roots.StageMany(&env,refs,&published,&error)&&!move_array);
  assert(published.front().index==1016&&published.back().chunk==1);
  void* value=nullptr;assert(roots.Read(retained,&value)&&value==&objects[0]);
  roots.Clear(&env);assert(globals==0&&arrays==0);
  for(size_t i=0;i<7;++i)assert(roots.StageMany(&env,refs,&published,&error));
  assert(roots.StageMany(&env,tail,&published,&error));const auto old=published;assert(globals==baseline);
  wrong_element=true;std::vector<bounded_managed_roots::Slot> rejected_many;
  assert(!roots.StageMany(&env,refs,&rejected_many,&error)&&rejected_many.empty());wrong_element=false;
  assert(globals==baseline&&arrays==1&&roots.Read(old.front(),&value)&&value==&objects[0]);
  fail_set=true;assert(!roots.StageMany(&env,refs,&rejected_many,&error)&&pending_exception&&rejected_many.empty());
  assert(globals==baseline&&arrays==1);ClearException(&env);
  assert(roots.StageMany(&env,refs,&published,&error)&&published.front().index==1016);
  pending_exception=true;const auto calls=scope_calls;
  assert(!roots.StageMany(&env,refs,&rejected_many,&error)&&scope_calls==calls&&pending_exception);ClearException(&env);
  std::vector<jobject> oversize(129,refs.front());assert(!roots.StageMany(&env,oversize,&rejected_many,&error));
  roots.Clear(&env);for(auto ref:refs)Delete(&env,ref);assert(globals==0&&arrays==0);
 }
 // The production Store retains every identity through its source-lazy path
 // without invoking the public force-resolving factory for unrelated fields.
 {rooted_field_identities::Store lazy({&vm,Scope,Oracle,Decode,Field,Owner,Element,LazyMirror});
  const auto force_calls=public_mirror_calls;refuse_public_mirror=true;
  assert(lazy.Stage(&env,ar,ma.id,false,&error)&&!pending_exception&&lazy.Read(ar,ma.id)==ma.field);
  assert(public_mirror_calls==force_calls&&lazy_calls==1&&globals==1);
  // Exact duplicate reuses the strong mirror only AFTER public ID/owner/static
  // and actual native array-slot observations. Movable roots are reread.
  auto duplicates=lazy_calls;assert(a.native==&moved_a);oracle_move_target=&native_a;move_oracle=true;move_array=true;
  assert(lazy.Stage(&env,ar,ma.id,false,&error)&&!move_oracle&&!move_array&&a.native==&native_a);oracle_move_target=&moved_a;
  assert(lazy_calls==duplicates&&public_mirror_calls==force_calls&&globals==1);
  // Same opaque ID in a different declaring namespace must produce a distinct
  // original mirror once, then validate its own entry without another factory.
  assert(lazy.Stage(&env,br,mb.id,false,&error)&&lazy_calls==duplicates+1);
  duplicates=lazy_calls;assert(lazy.Stage(&env,br,mb.id,false,&error)&&lazy_calls==duplicates);
  wrong_element=true;assert(!lazy.Stage(&env,ar,ma.id,false,&error));wrong_element=false;
  assert(lazy_calls==duplicates);
  ma.statik=true;assert(!lazy.Stage(&env,ar,ma.id,false,&error));ma.statik=false;
  assert(lazy_calls==duplicates);
  auto* prior_field=ma.field;fn.owner=a.native;ma.field=&fn;
  assert(!lazy.Stage(&env,ar,ma.id,false,&error));ma.field=prior_field;
  assert(lazy_calls==duplicates);
  const auto prior_owner=fa.owner;fa.owner=b.native;
  assert(!lazy.Stage(&env,ar,ma.id,false,&error));fa.owner=prior_owner;
  assert(lazy_calls==duplicates);

  wrong_id=true;assert(!lazy.Stage(&env,ar,ma.id,false,&error));wrong_id=false;assert(globals==1&&lazy_calls==duplicates);
  bad_lazy_local=true;assert(!lazy.Stage(&env,ar,mn.id,false,&error));bad_lazy_local=false;assert(globals==1);
  const auto calls=lazy_calls;pending_exception=true;assert(!lazy.Stage(&env,ar,ma.id,false,&error)&&lazy_calls==calls&&pending_exception);
  ClearException(&env);lazy.Clear(&env);refuse_public_mirror=false;assert(globals==0&&arrays==0);
 }
 // Exercise Verifier::Stage/Clear itself, including every formerly separate
 // root, loader, superclass and interface global publication path.
 api.GetSuperclass=Parent;api.IsAssignableFrom=Assignable;loaded_objects=&objects;
 api.GetStaticMethodID=StaticMethod;api.CallStaticBooleanMethodA=ProxyClass;api.CallObjectMethodA=ClassObject;api.GetStringUTFChars=StringChars;api.ReleaseStringUTFChars=ReleaseChars;
 public_names.resize(root_count);
 for(size_t i=0;i<root_count;++i){objects[i].class_name="Test"+std::to_string(i);public_names[i].class_name=objects[i].class_name;public_names[i].owner=&objects[i];public_names[i].native=&public_names[i];}
 // Include a real proxy classification/name path, with a distinct String root.
 objects.front().proxy=true;
 jvmtiInterface_1 jvmti_api{};jvmti_api.GetLoadedClasses=Loaded;jvmti_api.Deallocate=Deallocate;jvmti_api.GetClassSignature=Signature;jvmti_api.GetClassStatus=Status;jvmti_api.GetClassModifiers=ClassFlags;jvmti_api.GetClassMethods=Methods;jvmti_api.GetClassFields=Fields;jvmti_api.GetClassLoader=Loader;jvmti_api.GetImplementedInterfaces=Interfaces;jvmtiEnv ti{&jvmti_api};
 native_schema_guard_verifier::Verifier verifier;native_schema_guard_verifier::Bindings bindings;bindings.vm=reinterpret_cast<JavaVM*>(&vm);bindings.roots={&vm,Scope,Oracle,Decode,Element};
 auto input=static_cast<jclass>(Local(&objects.front()));
 globals=existing_class_roots;peak=globals;
 const auto stage_scopes=scope_calls;
 assert(verifier.Stage(&env,&ti,{input},bindings,&error));const auto graph_scopes=scope_calls-stage_scopes;assert(graph_scopes<root_count/16);assert(globals==existing_class_roots+(4*root_count+2+1023)/1024&&peak<51200);
 assert(verifier.MatchesAffected(&env,{input},&error));
 auto unequal=static_cast<jclass>(Local(&objects[1]));
 assert(!verifier.MatchesAffected(&env,{unequal},&error));
 assert(!verifier.MatchesAffected(&env,{input,input},&error));Delete(&env,unequal);
 verifier.Clear(&env);assert(globals==existing_class_roots&&arrays==0);globals=0;
 fail_proxy=true;assert(!verifier.Stage(&env,&ti,{input},bindings,&error)&&pending_exception);
 assert(error=="Public proxy Class classification failed");ClearException(&env);fail_proxy=false;
 verifier.Clear(&env);assert(globals==0&&arrays==0);
 // The complete public graph reports the exact unrelated declaration whose
 // public mirror leaf failed, without clearing its original JNI exception.
 declaration_failure=true;bindings.stage_field=DeclaringFailure;
 jvmti_api.GetFieldName=DeclarationName;jvmti_api.GetFieldModifiers=DeclarationFlags;
 assert(!verifier.Stage(&env,&ti,{input},bindings,&error)&&pending_exception);
 assert(error=="Public contextual Field stage [class=LTest0; field=optional type=Loptional/Missing;] Public contextual Field round trip unavailable");
 ClearException(&env);declaration_failure=false;verifier.Clear(&env);assert(globals==0&&arrays==0);Delete(&env,input);
 Delete(&env,ar);Delete(&env,br);Delete(&env,cr);
 std::cout<<"Root publication scopes: 180000 identities require "<<(root_count+127)/128<<" cohort scopes; full 180000-Class Verifier::Stage uses "<<graph_scopes<<" scopes (all class/loader/parent/interface roots retained)\n";
 std::cout<<"Actual rooted Field store: 60000 complete identities +15140 existing Class globals under51200 cap, shared slot/GC proof, noJNI/noalloc final reads, duplicate/refund and three clean cycles; shared schema roots 180000 distinct slots, GC and exception refunds; public Proxy/name roots, unequal/duplicate Holder rejection and classification exception refund PASS\n";
}
