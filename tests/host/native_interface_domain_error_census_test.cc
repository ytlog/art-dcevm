#include "platform/common/native_interface_domain_error_census.h"
#include "platform/common/native_interface_dispatch_domain.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <type_traits>
using namespace artdcevm;
namespace {
struct Class {const char*name;uint32_t flags;void*loader;Class*parent;std::vector<Class*>interfaces;};
Class object{"Ljava/lang/Object;",1,nullptr,nullptr,{}},iface{"LReadonlyTag;",0x601,nullptr,nullptr,{}},
 error_class{"LErroneous;",1,reinterpret_cast<void*>(0x1000),&object,{&iface}},
 foreign{"LForeign;",1,nullptr,&object,{}},moved=error_class;
struct Array {std::vector<jobject>values;};std::vector<Array*>arrays;
jclass Find(JNIEnv*,const char*){return reinterpret_cast<jclass>(&object);}
jboolean Pending(JNIEnv*){return false;}
jobject Ref(JNIEnv*,jobject o){return o;}void Delete(JNIEnv*,jobject){}
jboolean Same(JNIEnv*,jobject a,jobject b){return a==b;}
jobjectArray NewArray(JNIEnv*,jsize n,jclass,jobject){auto*a=new Array{std::vector<jobject>(size_t(n))};arrays.push_back(a);return reinterpret_cast<jobjectArray>(a);}
jsize Length(JNIEnv*,jarray a){return jsize(reinterpret_cast<Array*>(a)->values.size());}
void Set(JNIEnv*,jobjectArray a,jsize i,jobject value){reinterpret_cast<Array*>(a)->values.at(size_t(i))=value;}
jobject Get(JNIEnv*,jobjectArray a,jsize i){return reinterpret_cast<Array*>(a)->values.at(size_t(i));}
jclass Parent(JNIEnv*,jclass c){return reinterpret_cast<jclass>(reinterpret_cast<Class*>(c)->parent);}
bool allow_decode=true;
bool Decode(void*,jobject o,void**out){assert(allow_decode);*out=o;return o!=nullptr;}
bool Oracle(void*r,const runnable_jni::Context&,jobject o,void**out){return Decode(r,o,out);}
void*Element(void*,void*a,size_t i){return reinterpret_cast<Array*>(a)->values.at(i);}
bool Scope(void*,JNIEnv*e,jobject,runnable_jni::Operation op,void*d){return op({&object,e,nullptr},d);}
jvmtiError Signature(jvmtiEnv*,jclass c,char**out,char**){*out=strdup(reinterpret_cast<Class*>(c)->name);return JVMTI_ERROR_NONE;}
jvmtiError Flags(jvmtiEnv*,jclass c,jint*out){*out=jint(reinterpret_cast<Class*>(c)->flags);return JVMTI_ERROR_NONE;}
bool bad_loader=false;
jvmtiError Loader(jvmtiEnv*,jclass c,jobject*out){if(bad_loader)return JVMTI_ERROR_INTERNAL;*out=reinterpret_cast<jobject>(reinterpret_cast<Class*>(c)->loader);return JVMTI_ERROR_NONE;}
jvmtiError Interfaces(jvmtiEnv*,jclass c,jint*count,jclass**out){const auto&v=reinterpret_cast<Class*>(c)->interfaces;*count=jint(v.size());*out=v.empty()?nullptr:static_cast<jclass*>(std::malloc(v.size()*sizeof(jclass)));for(size_t i=0;i<v.size();++i)(*out)[i]=reinterpret_cast<jclass>(v[i]);return JVMTI_ERROR_NONE;}
jvmtiError Free(jvmtiEnv*,unsigned char*p){std::free(p);return JVMTI_ERROR_NONE;}
std::vector<void*>registered;
bool Register(void*,void*p,size_t*out){*out=registered.size();registered.push_back(p);return true;}
bool array_staging=true,array_readable=true;size_t arrays_released=0;
std::vector<void**>array_cells;
void*RetainArray(void*,void*array){auto*cell=new void*(array);array_cells.push_back(cell);return cell;}
bool ReadArray(void*,const void*cell,void**out)noexcept{if(!array_readable)return false;*out=*static_cast<void*const*>(cell);return *out!=nullptr;}
void ReleaseArray(void*,void*raw)noexcept{++arrays_released;delete static_cast<void**>(raw);}
bool ArrayStaging(void*)noexcept{return array_staging;}
bool allow_resolve=true;
void*Resolve(void*,size_t index)noexcept{return allow_resolve&&index<registered.size()?registered[index]:nullptr;}
}
int main(){
 static_assert(!std::is_default_constructible_v<native_interface_dispatch_domain::Certificate>);
 static_assert(!std::is_copy_constructible_v<native_interface_dispatch_domain::Certificate>);
 static_assert(!std::is_move_constructible_v<native_interface_dispatch_domain::Certificate>);
 JNINativeInterface_ jni{};jni.FindClass=Find;jni.ExceptionCheck=Pending;jni.NewGlobalRef=Ref;jni.DeleteGlobalRef=Delete;jni.DeleteLocalRef=Delete;jni.IsSameObject=Same;
 jni.NewObjectArray=NewArray;jni.GetArrayLength=Length;jni.SetObjectArrayElement=Set;jni.GetObjectArrayElement=Get;jni.GetSuperclass=Parent;
 JNIEnv env{&jni};jvmtiInterface_1 leaves{};leaves.GetClassSignature=Signature;leaves.GetClassModifiers=Flags;leaves.GetClassLoader=Loader;leaves.GetImplementedInterfaces=Interfaces;leaves.Deallocate=Free;jvmtiEnv ti{&leaves};
 bounded_managed_roots::Store roots({nullptr,Scope,Oracle,Decode,Element});
 native_interface_dispatch_domain::detail::ErrorCensus census;std::string reason;bool existing=false;
 const jint status=JVMTI_CLASS_STATUS_ERROR|JVMTI_CLASS_STATUS_PREPARED;
 const runnable_jni::Context context{&env,&env,nullptr};
 assert(!census.Stage(context,&ti,reinterpret_cast<jclass>(&error_class),JVMTI_CLASS_STATUS_ERROR,roots,&existing,&reason));
 assert(census.Stage(context,&ti,reinterpret_cast<jclass>(&error_class),status,roots,&existing,&reason)&&!existing&&census.Size()==1);
 assert(census.Stage(context,&ti,reinterpret_cast<jclass>(&error_class),status,roots,&existing,&reason)&&existing&&census.Size()==1);
 census.BeginCheck();assert(!census.EndCheck());
 assert(!census.Check(&env,&ti,reinterpret_cast<jclass>(&foreign),status,roots,&reason));
 bad_loader=true;assert(!census.Check(&env,&ti,reinterpret_cast<jclass>(&error_class),status,roots,&reason));bad_loader=false;
 error_class.interfaces.clear();assert(!census.Check(&env,&ti,reinterpret_cast<jclass>(&error_class),status,roots,&reason));error_class.interfaces={&iface};
 error_class.parent=nullptr;assert(!census.Check(&env,&ti,reinterpret_cast<jclass>(&error_class),status,roots,&reason));error_class.parent=&object;
 assert(census.Check(&env,&ti,reinterpret_cast<jclass>(&error_class),status,roots,&reason)&&census.EndCheck());
 assert(!census.Check(&env,&ti,reinterpret_cast<jclass>(&error_class),status,roots,&reason));
 const prepared_roots::Resolver resolver{nullptr,Resolve};
 assert(census.Freeze(roots,{nullptr,Register}));assert(roots.CollectManagedRoots({nullptr,Register}));assert(roots.RefreshManagedRoots(resolver));allow_decode=false;assert(census.Fresh(roots));
 // Move both independent public JNI roots and actual registered native slots.
 for(auto*a:arrays)for(auto&v:a->values)if(v==reinterpret_cast<jobject>(&error_class))v=reinterpret_cast<jobject>(&moved);
 assert(!census.Fresh(roots));for(auto&v:registered)if(v==&error_class)v=&moved;
 assert(census.CanRefresh(resolver));census.Refresh(resolver);assert(census.Fresh(roots));
 assert(census.Nodes()[0].klass==&moved&&census.Nodes()[0].native_loader==moved.loader&&census.Nodes()[0].native_parent==&object);
 // Move a retaining array independently of its contents. The original JNI
 // handle remains at its stale address; only the registered root is updated.
 for(auto& value:registered){auto found=std::find(arrays.begin(),arrays.end(),value);
  if(found!=arrays.end()){auto* relocated=new Array{(*found)->values};arrays.push_back(relocated);value=relocated;break;}}
 assert(census.Fresh(roots));
 // Once the native view is bound, invalidation cannot reopen the old JNI
 // decoder, even though its globals still exist and contain valid objects.
 allow_resolve=false;void* unreadable=nullptr;
 assert(!roots.RootsResolved()&&!roots.Read(census.Nodes()[0].root,&unreadable)&&!census.Fresh(roots));
 assert(!roots.RefreshManagedRoots(resolver));
 allow_resolve=true;assert(roots.RootsResolved()&&census.Fresh(roots));
 allow_decode=true;
 // A different Store with identical slot contents cannot own this census.
 bounded_managed_roots::Store other({nullptr,Scope,Oracle,Decode,Element});
 bounded_managed_roots::Slot slot;
 for(auto value:{reinterpret_cast<jobject>(&moved),reinterpret_cast<jobject>(moved.loader),reinterpret_cast<jobject>(&object),reinterpret_cast<jobject>(&iface)})assert(other.Stage(&env,value,&slot,&reason));
 assert(!census.Fresh(other));other.Clear(&env);
 const auto generation=roots.Generation();roots.Clear(&env);assert(roots.Generation()!=generation);
 assert(!census.Fresh(roots)&&!census.CanRefresh(resolver));
 // Refill the exact same slots and objects: address/value equality must not
 // resurrect ownership from a previous Store generation.
 for(auto value:{reinterpret_cast<jobject>(&moved),reinterpret_cast<jobject>(moved.loader),reinterpret_cast<jobject>(&object),reinterpret_cast<jobject>(&iface)})assert(roots.Stage(&env,value,&slot,&reason));
 assert(!census.Fresh(roots)&&!census.CanRefresh(resolver));
 census.BeginCheck();assert(!census.Check(&env,&ti,reinterpret_cast<jclass>(&moved),status,roots,&reason));
 census.Refresh(resolver);assert(!census.Fresh(roots));roots.Clear(&env);
 // A census still being staged cannot cross a generation either, even if
 // the new generation contains the identical class/loader/hierarchy slots.
 native_interface_dispatch_domain::detail::ErrorCensus staged;
 assert(staged.Stage(context,&ti,reinterpret_cast<jclass>(&moved),status,roots,&existing,&reason));
 staged.BeginCheck();assert(staged.Check(&env,&ti,reinterpret_cast<jclass>(&moved),status,roots,&reason));
 roots.Clear(&env);
 for(auto value:{reinterpret_cast<jobject>(&moved),reinterpret_cast<jobject>(moved.loader),reinterpret_cast<jobject>(&object),reinterpret_cast<jobject>(&iface)})assert(roots.Stage(&env,value,&slot,&reason));
 assert(!staged.Stage(context,&ti,reinterpret_cast<jclass>(&moved),status,roots,&existing,&reason));
 assert(!staged.Freeze(roots,{nullptr,Register}));roots.Clear(&env);
 // An empty complete census still binds to its genuine root store.
 native_interface_dispatch_domain::detail::ErrorCensus empty;
 empty.BeginCheck();assert(empty.EndCheck()&&empty.Freeze(roots,{nullptr,Register}));
 assert(roots.CollectManagedRoots({nullptr,Register}));assert(roots.RefreshManagedRoots(resolver));
 assert(empty.Fresh(roots)&&empty.CanRefresh(resolver));
 roots.Clear(&env);assert(!empty.Fresh(roots)&&!empty.CanRefresh(resolver));
 // First inspection occurs before final root registration. Its retaining
 // arrays already belong to native storage; JNI decoding is forbidden there.
 bounded_managed_roots::Store inspected({nullptr,Scope,Oracle,Decode,Element});
 bounded_managed_roots::Slot inspected_slot;
 assert(inspected.Stage(context,reinterpret_cast<jobject>(&moved),&inspected_slot,&reason));
 const bounded_managed_roots::Store::ArrayRoots native_arrays{nullptr,RetainArray,ReadArray,ReleaseArray,ArrayStaging};
 assert(inspected.RetainArrays(native_arrays)&&array_cells.size()==1);
 array_staging=false;allow_decode=false;void*value=nullptr;
 assert(inspected.Read(inspected_slot,&value)&&value==&moved);
 array_readable=false;assert(!inspected.Read(inspected_slot,&value));array_readable=true;
 auto*relocated_array=new Array{static_cast<Array*>(*array_cells[0])->values};arrays.push_back(relocated_array);
 *array_cells[0]=relocated_array;
 assert(inspected.Read(inspected_slot,&value)&&value==&moved);
 assert(inspected.CollectManagedRoots({nullptr,Register}));
 assert(inspected.RefreshManagedRoots(resolver)&&inspected.RootsResolved());
 array_staging=true;allow_decode=true;inspected.Clear(&env);assert(arrays_released==1);
 for(auto*a:arrays)delete a;
 std::cout<<"Read-only erroneous census: real bounded Store JNI slot proof, complete fresh membership/status/loader/parent/interface refusal, registered-root relocation and stale generation refusal passed\n";
}
