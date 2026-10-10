#include "platform/common/native_hierarchy_graph.h"
#include "platform/common/native_hierarchy_parent.h"
#include "platform/common/modern_field_metadata.h"
#include "platform/common/native_interface_graph.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/common/readable_mapping_scope.h"
#include <cassert>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <cstdarg>
#include <unordered_map>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
using namespace artdcevm;
using namespace native_hierarchy_graph;
// These leaf stubs deliberately do not prove ART metadata. This fixture tests
// complete public JNI staging and exact graph authority lifecycle; device/source
// owner proof remains a separate integration obligation.
struct Fake {
 class_metadata::Class11 bytes{};
 std::string descriptor,public_name;Fake* parent=nullptr;void* loader=nullptr;
 std::vector<Fake*> interfaces;std::vector<void*> native_interfaces;
 bool initialized=true,erroneous=false,prepared=true;uint32_t defining_flags=1;
 Fake(const char* d,Fake* p=nullptr,uint32_t flags=1):descriptor(d),parent(p){bytes.access_flags=flags;defining_flags=flags;public_name=descriptor.substr(1,descriptor.size()-2);std::replace(public_name.begin(),public_name.end(),'/','.');}
};
static std::vector<Fake*> loaded;
static bool deny_public_proxy=false;static bool forbid_alloc=false,forbid_os=false;static size_t context_reads=0,os_queries=0,snapshots=0;static bool deny_snapshot=false,deny_view=false;
template<class Operation> static void MustAbort(Operation operation){
 const bool allocation_guard=forbid_alloc;forbid_alloc=false;
 const pid_t child=fork();assert(child>=0);
 if(child==0){forbid_alloc=allocation_guard;operation();_exit(0);}
 int status=0;assert(waitpid(child,&status,0)==child);forbid_alloc=allocation_guard;
 assert(WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
}
void* operator new(size_t n){if(forbid_alloc)std::abort();if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
namespace artdcevm::method_identity {
bool ReadableMappingExtent(const void*,size_t* out){assert(!forbid_os);++os_queries;*out=4096;return true;}
ScopedReadableMappings::ScopedReadableMappings()noexcept{assert(!forbid_os);++os_queries;++snapshots;complete_=!deny_snapshot;}
ScopedReadableMappings::~ScopedReadableMappings()=default;
bool ScopedReadableMappings::Extent(const void*p,size_t*out)const noexcept{++context_reads;if(!complete_||deny_view||!p||!out)return false;*out=4096;return true;}
}
namespace artdcevm::modern_field_metadata {
bool FieldMetadata::Calibrate(const runnable_jni::Context&,jobject,const char*,const FieldOracle*,size_t,std::string*){calibrated_=true;return true;}
bool FieldMetadata::DefiningClassFlags(void*k,uint32_t*out,std::string*,const ReadContext*c)const{size_t extent=0;for(int i=0;i<7;++i)if(!ReadMappingExtent(c,k,&extent))return false;*out=static_cast<Fake*>(k)->defining_flags;return true;}
bool ReadMappingExtent(const ReadContext*c,const void*p,size_t*out){return c?c->extent&&c->extent(c->owner,p,out):method_identity::ReadableMappingExtent(p,out);}
bool FieldMetadata::ClassIdentity(void* k,field_copy::CopyClassView* out,std::string*,const ReadContext* context)const{size_t extent=0;if(context&&(!context->extent||!context->extent(context->owner,k,&extent)||extent<sizeof(class_metadata::Class11)))return false;auto* f=static_cast<Fake*>(k);*out={f->parent,f->loader,f->descriptor.c_str(),0,f->descriptor=="Ljava/lang/Object;"};return true;}
bool FieldMetadata::ClassIdentityVerified(void*k,const char*descriptor,void*name,const char*positive,const char*negative,field_copy::CopyClassView*out,std::string*e,const ReadContext*c)const{
 auto*f=static_cast<Fake*>(k);if(f->bytes.access_flags&0x40000u){if(name!=&f->public_name||!positive||!negative||f->public_name!=positive||f->public_name==negative||f->descriptor!=descriptor)return false;const size_t length=std::strlen(descriptor);if(length<2||std::strlen(positive)!=length-2)return false;for(size_t i=0;i<length-2;++i)if((positive[i]=='.'?'/':positive[i])!=descriptor[i+1])return false;}
 return ClassIdentity(k,out,e,c);
}
}
static void* deny_interface_read=nullptr;
namespace artdcevm::native_interface_graph {
bool Read(const modern_field_metadata::FieldMetadata&,void* k,std::vector<void*>* out,std::string*){*out=static_cast<Fake*>(k)->native_interfaces;return true;}
bool Matches(const modern_field_metadata::FieldMetadata&,void* k,const std::vector<void*>& expected,std::string*){return static_cast<Fake*>(k)->native_interfaces==expected;}
bool ReadInContext(const modern_field_metadata::FieldMetadata&m,void*k,std::vector<void*>*out,std::string*e,const modern_field_metadata::ReadContext*c){if(k==deny_interface_read){if(e)*e="Independent native IfTable prefix refused";return false;}size_t extent=0;for(int i=0;i<7;++i)if(!modern_field_metadata::ReadMappingExtent(c,k,&extent))return false;return Read(m,k,out,e);}
bool MatchesInContext(const modern_field_metadata::FieldMetadata&m,void*k,const std::vector<void*>&e,std::string*error,const modern_field_metadata::ReadContext*c){size_t extent=0;return (!c||(c->extent&&c->extent(c->owner,k,&extent)&&extent>=sizeof(class_metadata::Class11)))&&Matches(m,k,e,error);}
}
// Fake JNI implements the public graph APIs, not private ART allocations.
static size_t globals=0,peak_globals=0;
static void* boot_facade=nullptr;static Fake* masquerade=nullptr;
static std::unordered_map<jobject,int> locals;
struct Array {std::vector<Fake*> values;};
static std::unordered_map<Array*,size_t> array_globals;
static std::unordered_map<jobject,void*> moved_globals;
namespace artdcevm::modern_field_metadata {void* FieldMetadata::Reference(void* array,size_t offset,bool)const{auto*a=static_cast<Array*>(array);assert(array_globals.count(a)&&offset>=12&&(offset-12)%4==0);auto value=reinterpret_cast<jobject>(a->values.at((offset-12)/4));auto moved=moved_globals.find(value);return moved==moved_globals.end()?static_cast<void*>(value):moved->second;}}
static jclass JNICALL Find(JNIEnv*,const char*){return reinterpret_cast<jclass>(loaded.front());}
static jmethodID JNICALL Method(JNIEnv*,jclass,const char* name,const char*){return reinterpret_cast<jmethodID>(!std::strcmp(name,"getSuperclass")?1:!std::strcmp(name,"getInterfaces")?2:!std::strcmp(name,"getName")?5:3);}
static jmethodID JNICALL StaticMethod(JNIEnv*,jclass,const char*name,const char*){return reinterpret_cast<jmethodID>(!std::strcmp(name,"isProxyClass")?6:4);}
static jobject JNICALL LocalRef(JNIEnv*,jobject v){return v;}
static jobject JNICALL Global(JNIEnv*,jobject v){if(v){++globals;peak_globals=std::max(peak_globals,globals);assert(globals<51200);auto it=locals.find(v);if(it!=locals.end()&&it->second==1)++array_globals[reinterpret_cast<Array*>(v)];}return v;}
static void JNICALL DeleteGlobal(JNIEnv*,jobject v){if(v){assert(globals);--globals;auto it=array_globals.find(reinterpret_cast<Array*>(v));if(it!=array_globals.end()&&!--it->second){auto*a=it->first;array_globals.erase(it);if(!locals.count(v))delete a;}}}
static void JNICALL DeleteLocal(JNIEnv*,jobject v){auto it=locals.find(v);if(it==locals.end())return;if(it->second==1){if(!array_globals.count(reinterpret_cast<Array*>(v)))delete reinterpret_cast<Array*>(v);}else if(it->second==2)delete reinterpret_cast<std::string*>(v);locals.erase(it);}
static jboolean JNICALL Exception(JNIEnv*){return JNI_FALSE;}
static jobject JNICALL Call(JNIEnv*,jobject object,jmethodID method,va_list){auto* f=reinterpret_cast<Fake*>(object);if(reinterpret_cast<uintptr_t>(method)==5){auto value=reinterpret_cast<jobject>(&f->public_name);locals.emplace(value,3);return value;}if(reinterpret_cast<uintptr_t>(method)==1)return f->bytes.access_flags&0x200u?nullptr:reinterpret_cast<jobject>(f->parent);if(reinterpret_cast<uintptr_t>(method)==2){auto* a=new Array{f->interfaces};auto value=reinterpret_cast<jobject>(a);locals.emplace(value,1);return value;}return reinterpret_cast<jobject>((!f->loader||f==masquerade)?boot_facade:f->loader);}
static jobject JNICALL StaticCall(JNIEnv*,jclass,jmethodID,va_list args){auto text=va_arg(args,jstring);(void)va_arg(args,int);auto loader=va_arg(args,jobject);if(loader==boot_facade)loader=nullptr;auto name=*reinterpret_cast<std::string*>(text);std::replace(name.begin(),name.end(),'.','/');name="L"+name+";";for(auto* f:loaded)if(f->descriptor==name&&f->loader==loader)return reinterpret_cast<jobject>(f);return nullptr;}
static jsize JNICALL Length(JNIEnv*,jarray v){return jsize(reinterpret_cast<Array*>(v)->values.size());}
static jobject JNICALL Element(JNIEnv*,jobjectArray v,jsize n){return reinterpret_cast<jobject>(reinterpret_cast<Array*>(v)->values[size_t(n)]);}
static jobjectArray JNICALL NewArray(JNIEnv*,jsize n,jclass,jobject initial){auto*a=new Array{std::vector<Fake*>(size_t(n),reinterpret_cast<Fake*>(initial))};locals.emplace(reinterpret_cast<jobject>(a),1);return reinterpret_cast<jobjectArray>(a);}
static void JNICALL SetElement(JNIEnv*,jobjectArray array,jsize index,jobject value){reinterpret_cast<Array*>(array)->values.at(size_t(index))=reinterpret_cast<Fake*>(value);}
static jboolean JNICALL SameObject(JNIEnv*,jobject a,jobject b){return a==b;}
static jboolean JNICALL IsProxy(JNIEnv*,jclass,jmethodID,const jvalue*args){return !deny_public_proxy&&(reinterpret_cast<Fake*>(args[0].l)->bytes.access_flags&0x40000u)?JNI_TRUE:JNI_FALSE;}
static const char* JNICALL UtfChars(JNIEnv*,jstring value,jboolean*){return reinterpret_cast<std::string*>(value)->c_str();}
static void JNICALL ReleaseUtf(JNIEnv*,jstring,const char*){}
static jstring JNICALL String(JNIEnv*,const char* text){auto* s=new std::string(text);auto value=reinterpret_cast<jobject>(s);locals.emplace(value,2);return reinterpret_cast<jstring>(s);}
static jobjectRefType JNICALL RefType(JNIEnv*,jobject v){return v?JNIGlobalRefType:JNIInvalidRefType;}
static jvmtiError JNICALL Loaded(jvmtiEnv*,jint* count,jclass** result){*count=jint(loaded.size());*result=static_cast<jclass*>(std::malloc(loaded.size()*sizeof(jclass)));for(size_t i=0;i<loaded.size();++i)(*result)[i]=reinterpret_cast<jclass>(loaded[i]);return JVMTI_ERROR_NONE;}
static jvmtiError JNICALL Signature(jvmtiEnv*,jclass c,char** out,char**){*out=strdup(reinterpret_cast<Fake*>(c)->descriptor.c_str());return JVMTI_ERROR_NONE;}
static jvmtiError JNICALL Status(jvmtiEnv*,jclass c,jint* out){auto*f=reinterpret_cast<Fake*>(c);*out=(f->prepared?JVMTI_CLASS_STATUS_PREPARED:0)|(f->initialized?JVMTI_CLASS_STATUS_INITIALIZED:0)|(f->erroneous?JVMTI_CLASS_STATUS_ERROR:0);return JVMTI_ERROR_NONE;}
static jvmtiError JNICALL Modifiers(jvmtiEnv*,jclass c,jint* out){*out=jint(reinterpret_cast<Fake*>(c)->bytes.access_flags);return JVMTI_ERROR_NONE;}
static jvmtiError JNICALL Deallocate(jvmtiEnv*,unsigned char* p){std::free(p);return JVMTI_ERROR_NONE;}
static size_t root_borrows=0;
static bool Borrow(void*,const runnable_jni::Context& c,jobject value,void** out){assert(c.self&&c.env);++root_borrows;auto it=moved_globals.find(value);*out=it==moved_globals.end()?static_cast<void*>(value):it->second;return value!=nullptr;}
static size_t pair_reads=0;
static bool PairBorrow(void*owner,const runnable_jni::Context&c,jobject a,jobject b,void**left,void**right){++pair_reads;return Borrow(owner,c,a,left)&&Borrow(owner,c,b,right);}
struct FixtureOwner final:AllocationOwner,ReceiverOwner,CacheOwner {
 bool final_granted=false;
 bool VerifyFinal(const Graph&g,const source_commit::Plan&p)const noexcept override{return final_granted&&Verify(g,p);}
 bool granted=true;size_t commits=0;void* cache=nullptr;
 std::unordered_map<void*,void*> allocation_moves;
 bool RefreshAllocations(Graph*g,std::vector<Prepared>*values)const noexcept override{
  if(!granted)return false;
  const auto move=[&](void*&p){auto found=allocation_moves.find(p);if(found!=allocation_moves.end())p=found->second;};
  for(auto& value:*values){move(value.old_class);move(value.new_class);move(value.parent);move(value.cache);}
  for(auto& node:g->nodes)move(node.replacement);
  return true;
 }
 bool VerifyStaged(const Graph&,const std::vector<Input>&,std::string*)const override{return granted;}
 bool VerifyPrepared(const Graph&,const Prepared& p,std::string*)const override{return granted&&p.cache==cache&&p.class_index==2;}
 bool VerifyAllocation(const Graph&g,const Prepared&p,std::string*e)const override{return !p.new_class&&VerifyPrepared(g,p,e);}
 bool Verify(const Graph&,const source_commit::Plan&)const noexcept override{return granted;}
 void BeginCommit(const Graph&,const source_commit::Plan&)const noexcept override{++const_cast<FixtureOwner*>(this)->commits;}
};
static void Lifecycle(){
 Fake object("Ljava/lang/Object;"),a("Lpkg/A;",&object),b("Lpkg/B;",&a),parent("Lpkg/P;",&object),i("Lpkg/I;",&object,0x601);
 Fake facade("Lboot/Facade;"),foreign_loader("Lloader/Foreign;");boot_facade=&facade;
 loaded={&object,&a,&b,&parent,&i};JNINativeInterface_ jni{};jni.FindClass=Find;jni.GetMethodID=Method;jni.GetStaticMethodID=StaticMethod;jni.NewGlobalRef=Global;jni.NewLocalRef=LocalRef;jni.DeleteGlobalRef=DeleteGlobal;jni.DeleteLocalRef=DeleteLocal;jni.ExceptionCheck=Exception;jni.CallObjectMethodV=Call;jni.CallStaticObjectMethodV=StaticCall;jni.CallStaticBooleanMethodA=IsProxy;jni.GetStringUTFChars=UtfChars;jni.ReleaseStringUTFChars=ReleaseUtf;jni.GetArrayLength=Length;jni.GetObjectArrayElement=Element;jni.NewObjectArray=NewArray;jni.SetObjectArrayElement=SetElement;jni.IsSameObject=SameObject;jni.NewStringUTF=String;jni.GetObjectRefType=RefType;JNIEnv env{&jni};
 jvmtiInterface_1 funcs{};funcs.GetLoadedClasses=Loaded;funcs.GetClassSignature=Signature;funcs.GetClassStatus=Status;funcs.GetClassModifiers=Modifiers;funcs.Deallocate=Deallocate;jvmtiEnv ti{&funcs};runnable_jni::Context context{&a,&env,&ti};
 modern_field_metadata::FieldMetadata metadata;assert(metadata.Calibrate(context,nullptr,nullptr,nullptr,0,nullptr));FixtureOwner owner;owner.cache=&metadata;
 Bindings bindings{&metadata,nullptr,Borrow,&owner,&owner,&owner,PairBorrow};Input input;input.strong_original=reinterpret_cast<jclass>(&a);input.before={a.descriptor,object.descriptor,{},1,{},{}};input.after=input.before;input.after.superclass=parent.descriptor;input.after.interfaces={i.descriptor};input.after.access|=0x400u;
 Authority authority;std::string error;masquerade=&a;a.loader=&foreign_loader;assert(!authority.Stage(context,&ti,{input},bindings,&error));assert(!globals&&locals.empty());assert(error.find("Native/public Class namespace differs")!=std::string::npos);masquerade=nullptr;a.loader=nullptr;i.parent=nullptr;assert(!authority.Stage(context,&ti,{input},bindings,&error));assert(!globals&&locals.empty());i.parent=&object;deny_snapshot=true;assert(!authority.Stage(context,&ti,{input},bindings,&error));deny_snapshot=false;assert(!globals&&locals.empty());deny_view=true;auto denied_queries=os_queries;assert(!authority.Stage(context,&ti,{input},bindings,&error));assert(os_queries==denied_queries+1);deny_view=false;assert(!globals&&locals.empty());assert(authority.Stage(context,&ti,{input},bindings,&error));assert(globals==2&&locals.empty());jvmtiError expected=JVMTI_ERROR_NONE;assert(authority.CheckClass(context,input.strong_original,input.after,&expected));assert(expected==JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED);a.bytes.access_flags|=0x10u;assert(!authority.CheckClass(context,input.strong_original,input.after));a.bytes.access_flags&=~0x10u;auto wrong=input.after;wrong.methods.push_back({"x","()V",1,0});assert(!authority.CheckClass(context,input.strong_original,wrong));
 Fake moved_object=object,moved_a=a,moved_b=b,moved_parent=parent,moved_i=i;moved_a.parent=&moved_object;moved_b.parent=&moved_a;moved_parent.parent=&moved_object;moved_i.parent=&moved_object;
 moved_globals={{reinterpret_cast<jobject>(&object),&moved_object},{reinterpret_cast<jobject>(&a),&moved_a},{reinterpret_cast<jobject>(&b),&moved_b},{reinterpret_cast<jobject>(&parent),&moved_parent},{reinterpret_cast<jobject>(&i),&moved_i}};
 assert(authority.CheckClass(context,input.strong_original,input.after)); // Exact public globals survive GC; old raw pointers cannot authorize.
 Prepared p{&owner,&a,nullptr,&metadata,&parent,2,&input.after};assert(!authority.AuthorizeAllocation(context,p,&error));p.old_class=&moved_a;p.parent=&moved_parent;assert(authority.AuthorizeAllocation(context,p,&error));p.class_index=1;assert(!authority.AuthorizeAllocation(context,p,&error));p.class_index=2;
 Fake new_a(a.descriptor.c_str(),&moved_parent),new_b(b.descriptor.c_str(),&new_a);new_a.bytes.access_flags=input.after.access;new_a.native_interfaces={&moved_i};new_b.native_interfaces={&moved_i};p.new_class=&new_a;new_a.bytes.access_flags=1;assert(!authority.VerifyPrepared(context,p,&error));new_a.bytes.access_flags=input.after.access;assert(authority.VerifyPrepared(context,p,&error));
 auto implicit=input.before;implicit.descriptor=b.descriptor;implicit.superclass=a.descriptor;implicit.interfaces={};Prepared q{&owner,&moved_b,&new_b,&metadata,&new_a,2,&implicit};assert(authority.VerifyPrepared(context,q,&error));
 source_commit::Plan plan;plan.old_classes={&moved_a,&moved_b};plan.new_classes={&new_a,&new_b};
 // Actual Authority rejects every non-bijective Plan without freezing its root
 // layout. Independent old/new matches distinguish missing, duplicated and
 // stale identities; all failures are retryable with the exact complete Plan.
 auto rejected=plan;rejected.old_classes.resize(1);rejected.new_classes.resize(1);
 assert(!authority.PrepareRoots(rejected,&error));assert(error.find("descriptor=Lpkg/B;")!=std::string::npos&&error.find("graphAffected=2 planPairs=1 matches=0 oldMatches=0 newMatches=0")!=std::string::npos);
 rejected=plan;rejected.old_classes.push_back(&moved_a);rejected.new_classes.push_back(&new_a);
 assert(!authority.PrepareRoots(rejected,&error));assert(error.find("matches=2 oldMatches=2 newMatches=2")!=std::string::npos);
 rejected=plan;rejected.old_classes[0]=&a;
 assert(!authority.PrepareRoots(rejected,&error));assert(error.find("matches=0 oldMatches=0 newMatches=1")!=std::string::npos);
 rejected=plan;rejected.new_classes[0]=&a;
 assert(!authority.PrepareRoots(rejected,&error));assert(error.find("matches=0 oldMatches=1 newMatches=0")!=std::string::npos);
 // Holder capture can move both old public Classes and captured replacements
 // after VerifyPrepared. PrepareRoots must refresh both independent owners,
 // rather than compare stale graph addresses to a fresh Holder Plan.
 Fake collected_object=moved_object,collected_a=moved_a,collected_b=moved_b,collected_parent=moved_parent,collected_i=moved_i,collected_new_a=new_a,collected_new_b=new_b;
 collected_a.parent=&collected_object;collected_b.parent=&collected_a;collected_parent.parent=&collected_object;collected_i.parent=&collected_object;collected_new_a.parent=&collected_parent;collected_new_b.parent=&collected_new_a;collected_new_a.native_interfaces={&collected_i};collected_new_b.native_interfaces={&collected_i};
 moved_globals={{reinterpret_cast<jobject>(&object),&collected_object},{reinterpret_cast<jobject>(&a),&collected_a},{reinterpret_cast<jobject>(&b),&collected_b},{reinterpret_cast<jobject>(&parent),&collected_parent},{reinterpret_cast<jobject>(&i),&collected_i}};
 owner.allocation_moves={{&moved_a,&collected_a},{&moved_b,&collected_b},{&moved_parent,&collected_parent},{&new_a,&collected_new_a},{&new_b,&collected_new_b}};
 plan.old_classes={&collected_a,&collected_b};plan.new_classes={&collected_new_a,&collected_new_b};
 owner.granted=false;assert(!authority.PrepareRoots(plan,&error));owner.granted=true;
 // A later new-only move must refresh the captured replacement independently;
 // the stale Holder candidate still fails the exact pair guard.
 Fake latest_new_a=collected_new_a,latest_new_b=collected_new_b;latest_new_b.parent=&latest_new_a;
 owner.allocation_moves[&collected_new_a]=&latest_new_a;owner.allocation_moves[&collected_new_b]=&latest_new_b;
 owner.allocation_moves[&new_a]=&latest_new_a;owner.allocation_moves[&new_b]=&latest_new_b;
 assert(!authority.PrepareRoots(plan,&error));assert(error.find("oldMatches=1 newMatches=0")!=std::string::npos);
 plan.new_classes={&latest_new_a,&latest_new_b};
 assert(authority.PrepareRoots(plan,&error));owner.granted=false;assert(!authority.Verify(plan,0));assert(!authority.Seal(plan,&error));std::vector<void*> root_values;prepared_roots::Registrar registrar{&root_values,[](void* owner,void* value,size_t* slot){auto* roots=static_cast<std::vector<void*>*>(owner);*slot=roots->size();roots->push_back(value);return true;}};assert(authority.CollectManagedRoots(registrar));prepared_roots::Resolver resolver{&root_values,[](void* owner,size_t index)noexcept{return (*static_cast<std::vector<void*>*>(owner))[index];}};
 Fake final_object=collected_object,final_a=collected_a,final_b=collected_b,final_parent=collected_parent,final_i=collected_i,final_new_a=latest_new_a,final_new_b=latest_new_b;
 final_a.parent=&final_object;final_b.parent=&final_a;final_parent.parent=&final_object;final_i.parent=&final_object;final_new_a.parent=&final_parent;final_new_b.parent=&final_new_a;final_new_a.native_interfaces={&final_i};final_new_b.native_interfaces={&final_i};
 std::unordered_map<void*,void*> relocation{{&collected_object,&final_object},{&collected_a,&final_a},{&collected_b,&final_b},{&collected_parent,&final_parent},{&collected_i,&final_i},{&latest_new_a,&final_new_a},{&latest_new_b,&final_new_b}};
 for(auto& value:root_values){auto it=relocation.find(value);if(it!=relocation.end())value=it->second;}plan.old_classes={&final_a,&final_b};plan.new_classes={&final_new_a,&final_new_b};
 bool mapping_allowed=true;modern_field_metadata::ReadContext read_context{&mapping_allowed,[](void*owner,const void*,size_t*out){++context_reads;if(!*static_cast<bool*>(owner))return false;*out=4096;return true;}};authority.UseReadContext(read_context);forbid_os=true;
 forbid_alloc=true;assert(authority.CanRefresh(resolver));authority.Refresh(resolver);assert(!authority.Verify(plan,0));assert(!authority.Seal(plan,nullptr));owner.granted=true;mapping_allowed=false;assert(!authority.Seal(plan,nullptr));mapping_allowed=true;assert(authority.Seal(plan,nullptr));assert(!authority.Seal(plan,nullptr));
 // Real Stage/Seal/GC-root-refresh owner: Collected graph permission is
 // independent from the original final-only receiver proof.
 assert(authority.VerifyPrepared(plan,0));assert(!authority.VerifyFinal(plan,0));assert(!authority.Verify(plan,0));
 forbid_alloc=false;auto foreign_plan=plan;forbid_alloc=true;assert(!authority.VerifyPrepared(foreign_plan,0));assert(!authority.VerifyFinal(foreign_plan,0));
 MustAbort([&]{authority.BeginCommit(plan);});
 owner.final_granted=true;assert(authority.VerifyFinal(plan,0));
 owner.final_granted=false;MustAbort([&]{authority.BeginCommit(plan);});
 owner.final_granted=true;assert(authority.Verify(plan,0));final_new_a.bytes.access_flags=1;assert(!authority.Verify(plan,0));final_new_a.bytes.access_flags=input.after.access;final_new_a.parent=&final_object;assert(!authority.Verify(plan,0));final_new_a.parent=&final_parent;owner.granted=false;assert(!authority.Verify(plan,0));owner.granted=true;assert(authority.Verify(plan,1));authority.BeginCommit(plan);assert(!authority.VerifyPrepared(plan,0));assert(!authority.VerifyFinal(plan,0));MustAbort([&]{authority.BeginCommit(plan);});forbid_alloc=false;forbid_os=false;assert(context_reads>0&&owner.commits==3);
 authority.Clear(&env);moved_globals.clear();assert(!globals&&locals.empty());
 // A genuine public ERROR+PREPARED inventory Class retains all roots/edges
 // but never serves the affected or proposed hierarchy. Scalar native state
 // independently agrees with JVMTI; repeated native checks observe changes.
 Fake failure("Lfixture/Failure;",&object);failure.initialized=false;failure.erroneous=true;failure.bytes.status=2u<<28;loaded.push_back(&failure);
 assert(authority.Stage(context,&ti,{input},bindings,&error));
 assert(authority.Staged()->nodes.size()==6&&authority.Staged()->nodes.back().resolved_error&&!authority.Staged()->nodes.back().affected);
 assert(authority.CheckClass(context,input.strong_original,input.after));
 Fake moved_failure=failure;moved_globals={{reinterpret_cast<jobject>(&failure),&moved_failure}};
 assert(authority.CheckClass(context,input.strong_original,input.after));
 moved_failure.bytes.status=3u<<28;assert(!authority.CheckClass(context,input.strong_original,input.after));
 moved_failure.bytes.status=2u<<28;assert(authority.CheckClass(context,input.strong_original,input.after));
 authority.Clear(&env);moved_globals.clear();assert(!globals&&locals.empty());
 failure.parent=&a;assert(!authority.Stage(context,&ti,{input},bindings,&error));assert(error.find("Erroneous affected Class")!=std::string::npos);assert(!globals&&locals.empty());failure.parent=&object;
 auto saved_parent=input.after.superclass;input.after.superclass=failure.descriptor;
 assert(!authority.Stage(context,&ti,{input},bindings,&error));assert(error.find("Erroneous proposed parent")!=std::string::npos);assert(!globals&&locals.empty());input.after.superclass=saved_parent;
 failure.bytes.access_flags=0x601;failure.defining_flags=0x601;input.after.interfaces.push_back(failure.descriptor);
 assert(!authority.Stage(context,&ti,{input},bindings,&error));assert(error.find("Erroneous proposed interface")!=std::string::npos);assert(!globals&&locals.empty());input.after.interfaces.pop_back();failure.bytes.access_flags=1;failure.defining_flags=1;
 // Unresolved ERROR has no complete edge authority; do not hide it from the
 // inventory or pretend an opaque node cannot intersect the change closure.
 failure.prepared=false;failure.bytes.status=3u<<28;
 assert(!authority.Stage(context,&ti,{input},bindings,&error));assert(error.find("Unresolved erroneous Class")!=std::string::npos&&error.find(failure.descriptor)!=std::string::npos);assert(!globals&&locals.empty());
 failure.prepared=true;failure.bytes.status=3u<<28;assert(!authority.Stage(context,&ti,{input},bindings,&error));assert(error.find("error classification differs")!=std::string::npos);assert(!globals&&locals.empty());
 failure.bytes.status=2u<<28;deny_interface_read=&failure;assert(!authority.Stage(context,&ti,{input},bindings,&error));assert(error.find(failure.descriptor)!=std::string::npos);deny_interface_read=nullptr;assert(!globals&&locals.empty());
 loaded.pop_back();
 object.initialized=false;object.erroneous=true;object.bytes.status=2u<<28;
 assert(!authority.Stage(context,&ti,{input},bindings,&error));assert(error.find("Erroneous hierarchy dependency")!=std::string::npos);assert(!globals&&locals.empty());object.initialized=true;object.erroneous=false;object.bytes.status=0;
 a.initialized=false;a.erroneous=true;a.bytes.status=2u<<28;
 assert(!authority.Stage(context,&ti,{input},bindings,&error));assert(error.find("Erroneous affected Class")!=std::string::npos);assert(!globals&&locals.empty());a.initialized=true;a.erroneous=false;a.bytes.status=0;
 // Public Stage cyclic proposal rejects and releases every independently owned
 // global even though all individual lookups resolve valid Classes.
 input.after.superclass=b.descriptor;assert(!authority.Stage(context,&ti,{input},bindings,&error));assert(!globals&&locals.empty());
 // Exercise complete Authority Stage, not just its storage helper: every Class
 // remains strongly rooted while a single real loader is shared by 30k/60k
 // nodes. Same descriptors in different loaders retain separate identities.
 input.after.superclass=parent.descriptor;
 // A complete graph retains ordinary unchanged proxies, with independently
 // rooted public UTF8 names. Missing classification, wrong spelling/negative
 // oracle and stale name root refuse instead of dropping the proxy.
 Fake proxy("Lcustom/中Proxy;",&object,0x40011u);proxy.interfaces={&i};proxy.native_interfaces={&i};loaded.push_back(&proxy);proxy.loader=&facade;
 assert(authority.Stage(context,&ti,{input},bindings,&error));const auto*proxy_graph=authority.Staged();assert(proxy_graph&&proxy_graph->nodes.size()==6);const auto&proxy_node=proxy_graph->nodes.back();assert(proxy_node.proxy_name_root.Valid()&&proxy_node.proxy_name==&proxy.public_name&&!proxy_node.affected);assert(proxy_node.loader==&facade&&proxy_node.loader_root.Valid());assert(proxy_graph->nodes.front().loader==nullptr&&!proxy_graph->nodes.front().loader_root.owner);
 auto&mutable_proxy=const_cast<Node&>(proxy_node);const auto generation=mutable_proxy.proxy_name_root.generation;mutable_proxy.proxy_name_root.generation++;assert(!authority.CheckClass(context,input.strong_original,input.after));mutable_proxy.proxy_name_root.generation=generation;
 const auto negative=mutable_proxy.negative_proxy_name;mutable_proxy.negative_proxy_name=mutable_proxy.proxy_name_text;assert(!authority.CheckClass(context,input.strong_original,input.after));mutable_proxy.negative_proxy_name=negative;
 Fake moved_proxy=proxy;moved_globals={{reinterpret_cast<jobject>(&proxy),&moved_proxy},{reinterpret_cast<jobject>(&proxy.public_name),&moved_proxy.public_name}};assert(authority.CheckClass(context,input.strong_original,input.after));assert(mutable_proxy.klass==&moved_proxy&&mutable_proxy.proxy_name==&moved_proxy.public_name);moved_globals.clear();
 assert(authority.CheckClass(context,input.strong_original,input.after));proxy.public_name="custom.Other";assert(!authority.CheckClass(context,input.strong_original,input.after));proxy.public_name="custom.中Proxy";authority.Clear(&env);assert(!globals&&locals.empty());
 proxy.loader=nullptr;assert(authority.Stage(context,&ti,{input},bindings,&error));assert(authority.Staged()->nodes.back().loader==nullptr&&!authority.Staged()->nodes.back().loader_root.owner);authority.Clear(&env);assert(!globals&&locals.empty());
 deny_public_proxy=true;assert(!authority.Stage(context,&ti,{input},bindings,&error));deny_public_proxy=false;assert(!globals&&locals.empty());
 proxy.public_name="custom.Other";assert(!authority.Stage(context,&ti,{input},bindings,&error));proxy.public_name="custom.中Proxy";assert(!globals&&locals.empty());loaded.pop_back();
 // The complete public closure contains inherited diamond interfaces once.
 // Native row order can differ but exact identities, including namespace, cannot.
 Fake root_iface("Ldiamond/Root;",&object,0x601),left_iface("Ldiamond/Left;",&object,0x601),right_iface("Ldiamond/Right;",&object,0x601),leaf_iface("Ldiamond/Leaf;",&object,0x601);
 left_iface.interfaces={&root_iface};left_iface.native_interfaces={&root_iface};right_iface.interfaces={&root_iface};right_iface.native_interfaces={&root_iface};leaf_iface.interfaces={&left_iface,&right_iface};leaf_iface.native_interfaces={&root_iface,&right_iface,&left_iface};
 a.interfaces={&leaf_iface};a.native_interfaces={&root_iface,&right_iface,&leaf_iface,&left_iface};b.native_interfaces=a.native_interfaces;
 input.before.interfaces={leaf_iface.descriptor};input.after.interfaces=input.before.interfaces;
 loaded={&object,&a,&b,&parent,&i,&root_iface,&left_iface,&right_iface,&leaf_iface};
 assert(authority.Stage(context,&ti,{input},bindings,&error));authority.Clear(&env);assert(!globals&&locals.empty());
 deny_interface_read=&a;assert(!authority.Stage(context,&ti,{input},bindings,&error));assert(error.find("class=Lpkg/A;")!=std::string::npos&&error.find("nativeRead=0")!=std::string::npos&&error.find("readError=Independent native IfTable prefix refused")!=std::string::npos);deny_interface_read=nullptr;assert(!globals&&locals.empty());
 Fake interface_loader("Lloader/Interfaces;"),foreign_iface(root_iface.descriptor.c_str(),&object,0x601);foreign_iface.loader=&interface_loader;loaded.push_back(&foreign_iface);
 a.native_interfaces[0]=&foreign_iface;
 assert(!authority.Stage(context,&ti,{input},bindings,&error));assert(error.find("class=Lpkg/A;")!=std::string::npos&&error.find("nativeRead=1 expected=4 actual=4")!=std::string::npos&&error.find("missing=Ldiamond/Root;")!=std::string::npos&&error.find("unexpected=Ldiamond/Root;")!=std::string::npos);assert(!globals&&locals.empty());
 a.native_interfaces[0]=&root_iface;a.native_interfaces.push_back(&root_iface);
 assert(!authority.Stage(context,&ti,{input},bindings,&error));assert(error.find("duplicates=1")!=std::string::npos);assert(!globals&&locals.empty());
 a.native_interfaces.pop_back();b.native_interfaces.clear();
 assert(!authority.Stage(context,&ti,{input},bindings,&error));assert(error.find("class=Lpkg/B;")!=std::string::npos&&error.find("expected=4 actual=0")!=std::string::npos);assert(!globals&&locals.empty());
 a.interfaces.clear();a.native_interfaces.clear();input.before.interfaces.clear();input.after.interfaces={i.descriptor};
 Fake shared_loader("LSharedLoader;"),other_loader("LOtherLoader;");
 for(size_t class_count:{size_t(30000),size_t(60000)}){
  std::vector<std::unique_ptr<Fake>> many;many.reserve(class_count+2);
  loaded={&object,&a,&b,&parent,&i};
  for(size_t index=0;index<class_count;++index){auto descriptor="Lstress/C"+std::to_string(index)+";";auto value=std::make_unique<Fake>(descriptor.c_str(),&object,index%2?0x601u:1u);value->loader=&shared_loader;value->interfaces={&i};value->native_interfaces={&i};loaded.push_back(value.get());many.push_back(std::move(value));}
  for(auto loader_identity:{&shared_loader,&other_loader}){auto value=std::make_unique<Fake>("Lstress/Collision;",&object);value->loader=loader_identity;loaded.push_back(value.get());many.push_back(std::move(value));}
  auto stage_start=std::chrono::steady_clock::now();peak_globals=0;const auto before_queries=os_queries,before_snapshots=snapshots,before_pairs=pair_reads,before_borrows=root_borrows;assert(authority.Stage(context,&ti,{input},bindings,&error));assert(os_queries-before_queries==1&&snapshots-before_snapshots==1);
  std::cout<<"Full interface graph Stage nodes="<<class_count<<" seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-stage_start).count()<<"\n";const auto* graph=authority.Staged();assert(graph&&graph->nodes.size()==class_count+7&&pair_reads-before_pairs==3);
  // All Class roots were independently checked against their original JNI
  // identities. Native root decoding is per array cohort, not per Class.
  const size_t cohorts=(graph->nodes.size()+127)/128;
  assert(root_borrows-before_borrows<cohorts*10+graph->nodes.size()*3);
  const size_t chunks=(graph->nodes.size()+3+1023)/1024;
  assert(globals==chunks+1&&peak_globals<=chunks+2&&peak_globals<51200);
  auto old_witness=graph->nodes[5].class_root,loader_witness=graph->nodes[5].loader_root;
  assert(old_witness.Valid()&&loader_witness.Valid());
  for(size_t index=5;index<class_count+5;++index){const auto&node=graph->nodes[index];assert(node.RootedClass()&&!node.strong_class&&!node.strong_loader&&node.loader_root==loader_witness&&node.klass==loaded[index]);}
  assert(graph->nodes[class_count+5].descriptor==graph->nodes[class_count+6].descriptor&&graph->nodes[class_count+5].klass!=graph->nodes[class_count+6].klass&&graph->nodes[class_count+5].loader_root!=graph->nodes[class_count+6].loader_root);
  assert(graph->nodes[graph->inputs.front()].strong_class==input.strong_original);
  authority.Clear(&env);assert(!globals&&locals.empty()&&array_globals.empty()&&!old_witness.Valid()&&!loader_witness.Valid());
  // A recycled owner address and slot cannot revive a released witness.
  loaded={&object,&a,&b,&parent,&i};assert(authority.Stage(context,&ti,{input},bindings,&error));assert(!old_witness.Valid());authority.Clear(&env);assert(!globals&&locals.empty());
 }
}
static Node NodeOf(Fake& f){Node n;n.klass=&f;n.descriptor=f.descriptor;n.flags=f.bytes.access_flags;n.loader=f.loader;n.initialized=f.initialized;return n;}
static Graph Basic(Fake& object,Fake& a,Fake& b,Fake& i){Graph g;g.nodes={NodeOf(object),NodeOf(a),NodeOf(b),NodeOf(i)};g.nodes[1].parent=g.nodes[1].proposed_parent=0;g.nodes[2].parent=g.nodes[2].proposed_parent=1;g.inputs={1};return g;}
static void InterfaceParentIdentity(){
 int native_object=1,native_interface=2;Graph graph;Node object,interface;
 object.klass=&native_object;object.strong_class=reinterpret_cast<jclass>(&native_object);object.descriptor="Ljava/lang/Object;";object.flags=1;
 interface.klass=&native_interface;interface.flags=0x601;interface.descriptor="Lpkg/I;";
 graph.nodes={object,interface};void*value=nullptr;
 forbid_alloc=true;assert(NativeSuperclass(graph,graph.nodes[1],false,&value)&&value==&native_object);forbid_alloc=false;
 graph.nodes[0].strong_class=nullptr;value=&native_interface;assert(!NativeSuperclass(graph,graph.nodes[1],false,&value)&&value==&native_interface);graph.nodes[0]=object;
 graph.nodes[0].flags=0x601;assert(!NativeSuperclass(graph,graph.nodes[1],false,&value));graph.nodes[0]=object;
 graph.nodes[0].loader=&native_interface;assert(!NativeSuperclass(graph,graph.nodes[1],false,&value));graph.nodes[0]=object;
 graph.nodes.push_back(object);assert(!NativeSuperclass(graph,graph.nodes[1],false,&value));graph.nodes.pop_back();
 // Validated immutable node indices survive GC pointer refresh and keep all
 // rooted/role/namespace checks. Duplicate namespace or changed shape refuses.
 graph.inputs={1};assert(ValidateGraph(&graph,nullptr)&&graph.BootstrapObjectIndex()==0);
 int moved_native_object=3;graph.nodes[0].klass=&moved_native_object;forbid_alloc=true;assert(NativeSuperclass(graph,graph.nodes[1],false,&value)&&value==&moved_native_object);forbid_alloc=false;
 graph.nodes[0].strong_class=nullptr;assert(!NativeSuperclass(graph,graph.nodes[1],false,&value));graph.nodes[0]=object;
 graph.nodes.push_back(object);assert(!NativeSuperclass(graph,graph.nodes[1],false,&value));assert(!ValidateGraph(&graph,nullptr)&&!graph.BootstrapObjectIndex());graph.nodes.pop_back();
 graph.nodes[1].parent=0;assert(!NativeSuperclass(graph,graph.nodes[1],false,&value));
}
static void NestedModifiers(){
 modern_field_metadata::FieldMetadata metadata;assert(metadata.Calibrate({},nullptr,nullptr,nullptr,0,nullptr));
 Fake object("Ljava/lang/Object;"),parent("Lone/Outer$ProtectedNested;",&object),child("Ltwo/Child;",&parent),interface("Lone/Outer$PrivateInterface;",&object,0x601),new_parent("Lother/PackageParent;",&object,0);
 std::string error;Graph graph;graph.nodes={NodeOf(object),NodeOf(parent),NodeOf(child),NodeOf(interface),NodeOf(new_parent)};
 graph.nodes[1].flags=4; // JVMTI InnerClass protected annotation; real ART Class is public.
 graph.nodes[3].flags=0x602; // JVMTI private interface annotation; native role flags remain 0x601.
 graph.nodes[1].parent=graph.nodes[1].proposed_parent=0;graph.nodes[2].parent=graph.nodes[2].proposed_parent=1;graph.nodes[4].parent=graph.nodes[4].proposed_parent=0;
 graph.nodes[2].interfaces=graph.nodes[2].proposed_interfaces={3};graph.inputs={2};
 assert(CanonicalizeFlags(metadata,&graph,&error));assert(graph.nodes[1].public_flags==4&&graph.nodes[1].flags==1);assert(graph.nodes[3].public_flags==0x602&&graph.nodes[3].flags==0x601);assert(ValidateGraph(&graph,&error));
 // A newly requested genuinely nonpublic runtime Class still needs exact
 // loader+package access. Public InnerClass modifiers never confer permission.
 graph.nodes[2].proposed_parent=4;assert(!ValidateGraph(&graph,&error));assert(error.find(child.descriptor)!=std::string::npos&&error.find(new_parent.descriptor)!=std::string::npos&&error.find("retained=false")!=std::string::npos);
 graph.nodes[2].proposed_parent=1;graph.nodes[1].proposed_flags=graph.nodes[1].flags|0x10;assert(!ValidateGraph(&graph,&error)); // Retained edge cannot bypass newly final parent.
 graph.nodes[1].proposed_flags.reset();parent.bytes.access_flags|=0x400;assert(!CanonicalizeFlags(metadata,&graph,&error)); // Defining ClassDef/native disagreement remains authoritative.
 parent.bytes.access_flags&=~0x400u;assert(CanonicalizeFlags(metadata,&graph,&error));assert(ValidateGraph(&graph,&error));
 // An existing resolved edge carries its original access permission. A new
 // identity with identical descriptor text cannot borrow that permission.
 // Anonymous/static annotation can omit a native final flag. Immutable DEX
 // evidence preserves it, and using that Class as a new parent still fails.
 new_parent.bytes.access_flags=0x10;new_parent.defining_flags=0x10;graph.nodes[4].flags=8;graph.nodes[4].public_flags=8;
 assert(CanonicalizeFlags(metadata,&graph,&error));assert(graph.nodes[4].flags==0x10);graph.nodes[2].proposed_parent=4;assert(!ValidateGraph(&graph,&error));graph.nodes[2].proposed_parent=1;
 graph.nodes[1].flags&=~1u;graph.nodes[1].public_flags.reset();assert(ValidateGraph(&graph,&error));graph.nodes[2].proposed_parent=4;assert(!ValidateGraph(&graph,&error));
}
int main(){
 NestedModifiers();
 InterfaceParentIdentity();
 Lifecycle();
 Fake object("Ljava/lang/Object;"),a("Lpkg/A;",&object),b("Lpkg/B;",&a),i("Lpkg/I;",&object,0x601);std::string error;
 auto graph=Basic(object,a,b,i);assert(ValidateGraph(&graph,&error));assert(!graph.nodes[0].affected&&graph.nodes[1].affected&&graph.nodes[2].affected&&!graph.nodes[3].affected);
 graph=Basic(object,a,b,i);graph.nodes[1].proposed_parent=2;assert(!ValidateGraph(&graph,&error)); // A -> B -> A actual batch cycle.
 graph=Basic(object,a,b,i);graph.nodes[1].proposed_interfaces={3};graph.nodes[3].proposed_interfaces={3};assert(!ValidateGraph(&graph,&error));
 graph=Basic(object,a,b,i);graph.nodes[1].proposed_parent=0;graph.nodes[2].proposed_parent=0;graph.inputs={1,2};assert(ValidateGraph(&graph,&error)); // Detaching B is valid with an owner, regardless of input ordering.
 graph.inputs={2,1};assert(ValidateGraph(&graph,&error));
 graph=Basic(object,a,b,i);graph.nodes[1].proposed_parent=3;assert(!ValidateGraph(&graph,&error)); // Interface cannot become superclass.
 graph=Basic(object,a,b,i);graph.nodes[1].proposed_interfaces={0};assert(!ValidateGraph(&graph,&error));
 graph=Basic(object,a,b,i);graph.nodes[1].proposed_interfaces={graph.nodes.size()};assert(!ValidateGraph(&graph,&error));
 graph=Basic(object,a,b,i);graph.nodes[1].proposed_interfaces={SIZE_MAX};assert(!ValidateGraph(&graph,&error));
 graph=Basic(object,a,b,i);graph.nodes[1].proposed_interfaces={3,3};assert(!ValidateGraph(&graph,&error));
 graph=Basic(object,a,b,i);graph.nodes[2].proposed_parent=0;graph.nodes[0].initialized=false;assert(!ValidateGraph(&graph,&error));
 graph=Basic(object,a,b,i);graph.nodes[0].flags|=0x10;assert(!ValidateGraph(&graph,&error));
 graph=Basic(object,a,b,i);graph.nodes[1].proposed_parent=SIZE_MAX;assert(!ValidateGraph(&graph,&error));
 graph=Basic(object,a,b,i);graph.nodes[0].loader=&a;assert(!ValidateGraph(&graph,&error)); // Another loader's Object is not VM root.
 graph=Basic(object,a,b,i);graph.nodes[1].proposed_interfaces={3};graph.nodes[3].flags&=~1u;graph.nodes[3].descriptor="Lother/I;";assert(!ValidateGraph(&graph,&error));
 graph=Basic(object,a,b,i);graph.nodes[1].proposed_interfaces={3};graph.nodes[3].flags&=~1u;graph.nodes[3].loader=&a;assert(!ValidateGraph(&graph,&error)); // Same package alone never grants loader access.
 graph=Basic(object,a,b,i);graph.nodes[1].proposed_interfaces={3};graph.nodes[3].flags&=~1u;assert(ValidateGraph(&graph,&error));
 graph=Basic(object,a,b,i);graph.nodes[3].klass=graph.nodes[1].klass;assert(!ValidateGraph(&graph,&error));
 graph=Basic(object,a,b,i);graph.nodes[3].descriptor=graph.nodes[1].descriptor;assert(!ValidateGraph(&graph,&error));
 graph=Basic(object,a,b,i);graph.nodes[3].descriptor=graph.nodes[1].descriptor;graph.nodes[3].loader=&i;assert(ValidateGraph(&graph,&error)); // Identical text in another loader remains distinct.
 graph=Basic(object,a,b,i);graph.inputs={1,1};assert(!ValidateGraph(&graph,&error));
 graph=Basic(object,a,b,i);graph.nodes[2].parent=9999;assert(!ValidateGraph(&graph,&error));
 graph=Basic(object,a,b,i);graph.nodes[2].interfaces={9999};assert(!ValidateGraph(&graph,&error));
 graph=Basic(object,a,b,i);graph.inputs={3};graph.nodes[1].interfaces=graph.nodes[1].proposed_interfaces={3};assert(ValidateGraph(&graph,&error));assert(graph.nodes[1].affected&&graph.nodes[2].affected&&graph.nodes[3].affected);
 // Proposed edges use the replacement flags, including all batch targets.
 graph=Basic(object,a,b,i);graph.nodes[1].proposed_flags=0x11;assert(!ValidateGraph(&graph,&error)); // Existing proposed subclass prevents final.
 graph.nodes[2].proposed_parent=0;graph.inputs={1,2};assert(ValidateGraph(&graph,&error)); // Graph detached it; the independent loaded-subclass guard remains mandatory.
 graph=Basic(object,a,b,i);graph.nodes[1].flags=0x11;graph.nodes[1].proposed_flags=1;assert(ValidateGraph(&graph,&error)); // Final removal permits the proposed parent.
 graph=Basic(object,a,b,i);graph.nodes[1].proposed_flags=0x401;assert(ValidateGraph(&graph,&error));assert(graph.nodes[2].ProposedFlags()==1); // Implicit descendants retain flags.
 graph.nodes[1].proposed_flags=0x411;assert(!ValidateGraph(&graph,&error));
 graph.nodes[1].proposed_flags=0x601;assert(!ValidateGraph(&graph,&error));
 graph.nodes[1].proposed_flags=0;assert(!ValidateGraph(&graph,&error)); // Access change needs another real owner.
 // A long chain is iterative; no C++ recursion or fixed source-depth guess.
 Graph deep;deep.nodes.reserve(4096);for(size_t n=0;n<4096;++n){Node node;node.klass=reinterpret_cast<void*>(n+1);node.flags=1;node.initialized=true;node.descriptor=n?"Lpkg/N"+std::to_string(n)+";":"Ljava/lang/Object;";node.parent=node.proposed_parent=n?n-1:SIZE_MAX;deep.nodes.push_back(std::move(node));}deep.inputs={1};assert(ValidateGraph(&deep,&error));assert(deep.nodes.back().affected);deep.nodes[1].proposed_parent=4095;assert(!ValidateGraph(&deep,&error));
 Authority authority;runnable_jni::Context context{};Bindings bindings;std::vector<Input> inputs{{nullptr,{},{}}};assert(!authority.Stage(context,nullptr,inputs,bindings,&error));assert(!authority.CheckClass(context,nullptr,{}));Prepared prepared;assert(!authority.AuthorizeAllocation(context,prepared,&error));assert(!authority.VerifyPrepared(context,prepared,&error));source_commit::Plan plan;assert(!authority.Seal(plan,&error));
 forbid_alloc=true;assert(!authority.Verify(plan,0));assert(!authority.CanRefresh({}));forbid_alloc=false;
 std::puts("Native hierarchy graph: exact cycles, loader/access/role/init, descendants, 4096-depth and absent owner refusal PASS; public JNI fixture/complete authority lifecycle and full 30000/60000 Class bounded roots, single fresh ordinary mapping capture, denied view no fallback, shared/different loaders, collision identities, refunds/recycled witness PASS; live ART owner integration unproven");
}
