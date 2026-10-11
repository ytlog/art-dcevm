// Exercise the real Stage/CheckWindow TU. Public JNI/JVMTI and source gate are
// boundary doubles; every shared-array slot still takes the real Store proof.
#include "platform/common/native_schema_guard_verifier.cc"
#include <cassert>
#include <iostream>
#include <memory>
namespace artdcevm::redefinition_gate {bool Owned(JavaVM*){return true;}}
namespace {
struct Object {std::string descriptor;bool descendant=false;bool array=false;std::vector<jobject> values;jobject component=nullptr,name=nullptr;bool proxy=false;jint status=JVMTI_CLASS_STATUS_PREPARED|JVMTI_CLASS_STATUS_VERIFIED;};
std::vector<std::unique_ptr<Object>> objects;std::vector<jclass> loaded;
size_t globals=0,peak=0,enumerations=0,staged_fields=0;bool add_descendant=false,reject_lazy=false;
Object object_type{"Ljava/lang/Object;",false,false,{}};
struct GlobalHandle {Object* live;};std::vector<std::unique_ptr<GlobalHandle>> handles;
Object* Cast(jobject o){for(const auto& h:handles)if(reinterpret_cast<jobject>(h.get())==o)return h->live;return reinterpret_cast<Object*>(o);}
jclass Add(const char* name,bool descendant=false){auto o=std::make_unique<Object>();o->descriptor=name;o->descendant=descendant;auto c=reinterpret_cast<jclass>(o.get());objects.push_back(std::move(o));loaded.push_back(c);return c;}
jobject JNICALL Global(JNIEnv*,jobject o){++globals;peak=std::max(peak,globals);auto h=std::make_unique<GlobalHandle>();h->live=Cast(o);auto result=reinterpret_cast<jobject>(h.get());handles.push_back(std::move(h));return result;}
void JNICALL DropGlobal(JNIEnv*,jobject o){assert(globals);--globals;auto i=std::find_if(handles.begin(),handles.end(),[&](const auto& h){return reinterpret_cast<jobject>(h.get())==o;});assert(i!=handles.end());handles.erase(i);}
jobject JNICALL Local(JNIEnv*,jobject object){return reinterpret_cast<jobject>(Cast(object));}
void JNICALL DropLocal(JNIEnv*,jobject){}
jboolean JNICALL Pending(JNIEnv*){return JNI_FALSE;}
jboolean JNICALL Same(JNIEnv*,jobject a,jobject b){return Cast(a)==Cast(b);}
jclass JNICALL Find(JNIEnv*,const char*){return reinterpret_cast<jclass>(&object_type);}
jobjectArray JNICALL Array(JNIEnv*,jsize count,jclass,jobject){auto o=std::make_unique<Object>();o->array=true;o->values.resize(count);auto a=reinterpret_cast<jobjectArray>(o.get());objects.push_back(std::move(o));return a;}
jsize JNICALL Length(JNIEnv*,jarray a){return Cast(a)->values.size();}
void JNICALL Set(JNIEnv*,jobjectArray a,jsize i,jobject v){Cast(a)->values.at(i)=v;}
jobject JNICALL Get(JNIEnv*,jobjectArray a,jsize i){return Cast(a)->values.at(i);}
jclass JNICALL Parent(JNIEnv*,jclass){return nullptr;}
jmethodID JNICALL PublicMethod(JNIEnv*,jclass,const char* name,const char*){return reinterpret_cast<jmethodID>(std::strcmp(name,"getName")==0?2:std::strcmp(name,"getComponentType")==0?3:1);}
jboolean JNICALL Proxy(JNIEnv*,jclass,jmethodID,const jvalue* args){return Cast(args[0].l)->proxy;}
jobject JNICALL ObjectMethod(JNIEnv*,jobject c,jmethodID id,const jvalue*){return id==reinterpret_cast<jmethodID>(2)?Cast(c)->name:Cast(c)->component;}
const char* JNICALL UTF(JNIEnv*,jstring s,jboolean*){return Cast(s)->descriptor.c_str();}
void JNICALL ReleaseUTF(JNIEnv*,jstring,const char*){}
jboolean JNICALL Assignable(JNIEnv*,jclass klass,jclass input){return klass==input||Cast(klass)->descendant;}
jvmtiError JNICALL Enumerate(jvmtiEnv*,jint* count,jclass** classes){++enumerations;*count=loaded.size();*classes=static_cast<jclass*>(std::malloc(loaded.size()*sizeof(jclass)));std::copy(loaded.begin(),loaded.end(),*classes);return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Free(jvmtiEnv*,unsigned char* p){std::free(p);return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Status(jvmtiEnv*,jclass c,jint* out){*out=Cast(c)->status;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Signature(jvmtiEnv*,jclass c,char** s,char**){*s=strdup(Cast(c)->descriptor.c_str());return JVMTI_ERROR_NONE;}
jvmtiError JNICALL ClassFlags(jvmtiEnv*,jclass,jint* flags){*flags=1;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Methods(jvmtiEnv*,jclass,jint* count,jmethodID** ids){*count=0;*ids=nullptr;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Fields(jvmtiEnv*,jclass c,jint* count,jfieldID** ids){if(Cast(c)->status&(JVMTI_CLASS_STATUS_ARRAY|JVMTI_CLASS_STATUS_PRIMITIVE)){*count=0;*ids=nullptr;return JVMTI_ERROR_NONE;}*count=1;*ids=static_cast<jfieldID*>(std::malloc(sizeof(jfieldID)));**ids=reinterpret_cast<jfieldID>(c);return JVMTI_ERROR_NONE;}
jvmtiError JNICALL FieldName(jvmtiEnv*,jclass,jfieldID,char** name,char** signature,char**){*name=strdup("value");*signature=strdup("Ljava/lang/Object;");return JVMTI_ERROR_NONE;}
jvmtiError JNICALL FieldFlags(jvmtiEnv*,jclass,jfieldID,jint* flags){*flags=1;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Loader(jvmtiEnv*,jclass,jobject* out){*out=nullptr;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Interfaces(jvmtiEnv*,jclass,jint* count,jclass** out){*count=0;*out=nullptr;return JVMTI_ERROR_NONE;}
bool Decode(void*,jobject root,void** out){*out=Cast(root);return root;}
bool Oracle(void*,const artdcevm::runnable_jni::Context&,jobject root,void** out){return Decode(nullptr,root,out);}
void* Element(void*,void* array,size_t index){return reinterpret_cast<Object*>(array)->values.at(index);}
bool Scope(void*,JNIEnv* env,jobject,artdcevm::runnable_jni::Operation operation,void* payload){return operation({nullptr,env,nullptr},payload);}
bool StageField(void*,JNIEnv*,jclass c,jfieldID,bool,std::string*){
 ++staged_fields;
 if(Cast(c)->descriptor=="LSeed;")Add("LLazyOne;",add_descendant);
 if(Cast(c)->descriptor=="LLazyOne;")Add("LLazyTwo;");
 if(Cast(c)->descriptor=="LLazyTwo;"){if(reject_lazy)return false;Add("LLazyThree;");}
 return true;
}
}
int main(){
 JNINativeInterface_ jni{};jni.NewLocalRef=Local;jni.NewGlobalRef=Global;jni.DeleteGlobalRef=DropGlobal;jni.DeleteLocalRef=DropLocal;jni.ExceptionCheck=Pending;jni.IsSameObject=Same;jni.FindClass=Find;jni.NewObjectArray=Array;jni.GetArrayLength=Length;jni.SetObjectArrayElement=Set;jni.GetObjectArrayElement=Get;jni.GetSuperclass=Parent;jni.GetMethodID=PublicMethod;jni.GetStaticMethodID=PublicMethod;jni.CallStaticBooleanMethodA=Proxy;jni.CallObjectMethodA=ObjectMethod;jni.GetStringUTFChars=UTF;jni.ReleaseStringUTFChars=ReleaseUTF;jni.IsAssignableFrom=Assignable;
 JNIEnv env{&jni};jvmtiInterface_1_ ti{};ti.GetLoadedClasses=Enumerate;ti.Deallocate=Free;ti.GetClassStatus=Status;ti.GetClassSignature=Signature;ti.GetClassModifiers=ClassFlags;ti.GetClassMethods=Methods;ti.GetClassFields=Fields;ti.GetFieldName=FieldName;ti.GetFieldModifiers=FieldFlags;ti.GetClassLoader=Loader;ti.GetImplementedInterfaces=Interfaces;jvmtiEnv jvmti{&ti};
 using namespace artdcevm::native_schema_guard_verifier;
 Bindings bindings{};bindings.roots={nullptr,Scope,Oracle,Decode,Element};bindings.stage_field=StageField;
 const artdcevm::runnable_jni::Context context{&env,&env,bindings.vm};
 for(bool descendant:{false,true}){
  for(int cycle=0;cycle<2;++cycle){
   loaded.clear();objects.clear();enumerations=0;staged_fields=0;peak=0;add_descendant=descendant;
   auto seed=Add("LSeed;");for(size_t i=0;i<2200;++i)Add(("LCold"+std::to_string(i)+";").c_str());
   Verifier verifier;std::string error;
   assert(verifier.Stage(context,&jvmti,{seed},bindings,&error));
   assert(loaded.size()==2204&&staged_fields==loaded.size()&&enumerations==5);
   // Root chunks, not one permanent JNI global for each loaded identity.
   assert(globals==3&&peak<=5);
   assert(verifier.MatchesAffected(&env,{seed},&error)==!descendant);
   std::vector<jclass> affected{seed};if(descendant)affected.push_back(loaded[2201]);
   assert(verifier.MatchesAffected(&env,affected,&error));
   // Moving GC changes each Object[] address while opaque JNI globals stay
   // fixed. Shared slot reads must follow the newly decoded array owner.
   for(auto& h:handles){assert(h->live->array);auto moved=std::make_unique<Object>(*h->live);h->live=moved.get();objects.push_back(std::move(moved));}
   assert(verifier.CheckWindow(&env,&jvmti,affected,Window::Verified,&error));
   // A same-descriptor foreign identity after freeze remains forbidden.
   Add("LLazyOne;");
   assert(!verifier.CheckWindow(&env,&jvmti,affected,Window::Collected,&error));
   assert(error.find("descriptor=LLazyOne;")!=std::string::npos);
   loaded.pop_back();Add("LExternalUnrelated;");
   assert(!verifier.CheckWindow(&env,&jvmti,affected,Window::Collected,&error));
   assert(error.find("descriptor=LExternalUnrelated;")!=std::string::npos);
   loaded.pop_back();assert(verifier.CheckWindow(&env,&jvmti,affected,Window::Collected,&error));
   verifier.Clear(&env);assert(globals==0);
  }
 }
 // Failure during a later closure pass refunds all already-published chunks;
 // a fresh operation can then establish the complete graph normally.
 loaded.clear();objects.clear();staged_fields=0;enumerations=0;reject_lazy=true;add_descendant=false;
 auto seed=Add("LSeed;");Verifier rejected;std::string error;
 assert(!rejected.Stage(context,&jvmti,{seed},bindings,&error));
 assert(staged_fields==3&&enumerations==3&&globals==1);
 rejected.Clear(&env);assert(globals==0&&handles.empty());
 loaded.clear();objects.clear();reject_lazy=false;
 auto primitive=Add("I");Cast(primitive)->status=JVMTI_CLASS_STATUS_PRIMITIVE;
 auto array=Add("[I");Cast(array)->status=JVMTI_CLASS_STATUS_ARRAY;Cast(array)->component=primitive;
 auto proxy=Add("Lcustom/Generated;");Cast(proxy)->proxy=true;
 auto public_name=std::make_unique<Object>();public_name->descriptor="custom.Generated";Cast(proxy)->name=reinterpret_cast<jobject>(public_name.get());objects.push_back(std::move(public_name));
 Verifier kinds;assert(kinds.Stage(context,&jvmti,{proxy},bindings,&error));
 assert(kinds.MatchesAffected(&env,{proxy},&error));
 assert(kinds.CheckWindow(&env,&jvmti,{proxy},Window::Verified,&error));
 assert(kinds.CheckWindow(&env,&jvmti,{proxy},Window::Collected,&error));
 kinds.Clear(&env);assert(globals==0);
 std::cout<<"actual Stage closure: three lazy cascades, bounded 2204 Class roots, once-only fields, descendant preflight mismatch, external window rejection and repeated refund passed\n";
}
