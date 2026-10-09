#include "platform/common/native_hierarchy_graph.h"
#include "platform/common/native_hierarchy_parent.h"
#include "platform/common/modern_field_metadata.h"
#include "platform/common/native_interface_graph.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/common/readable_mapping_scope.h"
#include <cassert>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <cstdarg>
#include <unordered_map>
using namespace artdcevm;
using namespace native_hierarchy_graph;
// These leaf stubs deliberately do not prove ART metadata. This fixture tests
// complete public JNI staging and exact graph authority lifecycle; device/source
// owner proof remains a separate integration obligation.
struct Fake {
 class_metadata::Class11 bytes{};
 std::string descriptor;Fake* parent=nullptr;void* loader=nullptr;
 std::vector<Fake*> interfaces;std::vector<void*> native_interfaces;
 bool initialized=true;uint32_t defining_flags=1;
 Fake(const char* d,Fake* p=nullptr,uint32_t flags=1):descriptor(d),parent(p){bytes.access_flags=flags;defining_flags=flags;}
};
static std::vector<Fake*> loaded;
static bool forbid_alloc=false,forbid_os=false;static size_t context_reads=0,os_queries=0,snapshots=0;static bool deny_snapshot=false,deny_view=false;
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
}
namespace artdcevm::native_interface_graph {
bool Read(const modern_field_metadata::FieldMetadata&,void* k,std::vector<void*>* out,std::string*){*out=static_cast<Fake*>(k)->native_interfaces;return true;}
bool Matches(const modern_field_metadata::FieldMetadata&,void* k,const std::vector<void*>& expected,std::string*){return static_cast<Fake*>(k)->native_interfaces==expected;}
bool ReadInContext(const modern_field_metadata::FieldMetadata&m,void*k,std::vector<void*>*out,std::string*e,const modern_field_metadata::ReadContext*c){size_t extent=0;for(int i=0;i<7;++i)if(!modern_field_metadata::ReadMappingExtent(c,k,&extent))return false;return Read(m,k,out,e);}
bool MatchesInContext(const modern_field_metadata::FieldMetadata&m,void*k,const std::vector<void*>&e,std::string*error,const modern_field_metadata::ReadContext*c){size_t extent=0;return (!c||(c->extent&&c->extent(c->owner,k,&extent)&&extent>=sizeof(class_metadata::Class11)))&&Matches(m,k,e,error);}
}
// Fake JNI implements the public graph APIs, not private ART allocations.
static size_t globals=0,peak_globals=0;
static std::unordered_map<jobject,int> locals;
struct Array {std::vector<Fake*> values;};
static std::unordered_map<Array*,size_t> array_globals;
static std::unordered_map<jobject,void*> moved_globals;
namespace artdcevm::modern_field_metadata {void* FieldMetadata::Reference(void* array,size_t offset,bool)const{auto*a=static_cast<Array*>(array);assert(array_globals.count(a)&&offset>=12&&(offset-12)%4==0);auto value=reinterpret_cast<jobject>(a->values.at((offset-12)/4));auto moved=moved_globals.find(value);return moved==moved_globals.end()?static_cast<void*>(value):moved->second;}}
static jclass JNICALL Find(JNIEnv*,const char*){return reinterpret_cast<jclass>(loaded.front());}
static jmethodID JNICALL Method(JNIEnv*,jclass,const char* name,const char*){return reinterpret_cast<jmethodID>(!std::strcmp(name,"getSuperclass")?1:!std::strcmp(name,"getInterfaces")?2:3);}
static jmethodID JNICALL StaticMethod(JNIEnv*,jclass,const char*,const char*){return reinterpret_cast<jmethodID>(4);}
static jobject JNICALL Global(JNIEnv*,jobject v){if(v){++globals;peak_globals=std::max(peak_globals,globals);assert(globals<51200);auto it=locals.find(v);if(it!=locals.end()&&it->second==1)++array_globals[reinterpret_cast<Array*>(v)];}return v;}
static void JNICALL DeleteGlobal(JNIEnv*,jobject v){if(v){assert(globals);--globals;auto it=array_globals.find(reinterpret_cast<Array*>(v));if(it!=array_globals.end()&&!--it->second){auto*a=it->first;array_globals.erase(it);if(!locals.count(v))delete a;}}}
static void JNICALL DeleteLocal(JNIEnv*,jobject v){auto it=locals.find(v);if(it==locals.end())return;if(it->second==1){if(!array_globals.count(reinterpret_cast<Array*>(v)))delete reinterpret_cast<Array*>(v);}else delete reinterpret_cast<std::string*>(v);locals.erase(it);}
static jboolean JNICALL Exception(JNIEnv*){return JNI_FALSE;}
static jobject JNICALL Call(JNIEnv*,jobject object,jmethodID method,va_list){auto* f=reinterpret_cast<Fake*>(object);if(reinterpret_cast<uintptr_t>(method)==1)return f->bytes.access_flags&0x200u?nullptr:reinterpret_cast<jobject>(f->parent);if(reinterpret_cast<uintptr_t>(method)==2){auto* a=new Array{f->interfaces};auto value=reinterpret_cast<jobject>(a);locals.emplace(value,1);return value;}return reinterpret_cast<jobject>(f->loader);}
static jobject JNICALL StaticCall(JNIEnv*,jclass,jmethodID,va_list args){auto text=va_arg(args,jstring);(void)va_arg(args,int);auto loader=va_arg(args,jobject);auto name=*reinterpret_cast<std::string*>(text);std::replace(name.begin(),name.end(),'.','/');name="L"+name+";";for(auto* f:loaded)if(f->descriptor==name&&f->loader==loader)return reinterpret_cast<jobject>(f);return nullptr;}
static jsize JNICALL Length(JNIEnv*,jarray v){return jsize(reinterpret_cast<Array*>(v)->values.size());}
static jobject JNICALL Element(JNIEnv*,jobjectArray v,jsize n){return reinterpret_cast<jobject>(reinterpret_cast<Array*>(v)->values[size_t(n)]);}
static jobjectArray JNICALL NewArray(JNIEnv*,jsize n,jclass,jobject initial){auto*a=new Array{std::vector<Fake*>(size_t(n),reinterpret_cast<Fake*>(initial))};locals.emplace(reinterpret_cast<jobject>(a),1);return reinterpret_cast<jobjectArray>(a);}
static void JNICALL SetElement(JNIEnv*,jobjectArray array,jsize index,jobject value){reinterpret_cast<Array*>(array)->values.at(size_t(index))=reinterpret_cast<Fake*>(value);}
static jboolean JNICALL SameObject(JNIEnv*,jobject a,jobject b){return a==b;}
static jstring JNICALL String(JNIEnv*,const char* text){auto* s=new std::string(text);auto value=reinterpret_cast<jobject>(s);locals.emplace(value,2);return reinterpret_cast<jstring>(s);}
static jobjectRefType JNICALL RefType(JNIEnv*,jobject v){return v?JNIGlobalRefType:JNIInvalidRefType;}
static jvmtiError JNICALL Loaded(jvmtiEnv*,jint* count,jclass** result){*count=jint(loaded.size());*result=static_cast<jclass*>(std::malloc(loaded.size()*sizeof(jclass)));for(size_t i=0;i<loaded.size();++i)(*result)[i]=reinterpret_cast<jclass>(loaded[i]);return JVMTI_ERROR_NONE;}
static jvmtiError JNICALL Signature(jvmtiEnv*,jclass c,char** out,char**){*out=strdup(reinterpret_cast<Fake*>(c)->descriptor.c_str());return JVMTI_ERROR_NONE;}
static jvmtiError JNICALL Status(jvmtiEnv*,jclass c,jint* out){*out=JVMTI_CLASS_STATUS_PREPARED|(reinterpret_cast<Fake*>(c)->initialized?JVMTI_CLASS_STATUS_INITIALIZED:0);return JVMTI_ERROR_NONE;}
static jvmtiError JNICALL Modifiers(jvmtiEnv*,jclass c,jint* out){*out=jint(reinterpret_cast<Fake*>(c)->bytes.access_flags);return JVMTI_ERROR_NONE;}
static jvmtiError JNICALL Deallocate(jvmtiEnv*,unsigned char* p){std::free(p);return JVMTI_ERROR_NONE;}
static bool Borrow(void*,const runnable_jni::Context&,jobject value,void** out){auto it=moved_globals.find(value);*out=it==moved_globals.end()?static_cast<void*>(value):it->second;return value!=nullptr;}
static size_t pair_reads=0;
static bool PairBorrow(void*owner,const runnable_jni::Context&c,jobject a,jobject b,void**left,void**right){++pair_reads;return Borrow(owner,c,a,left)&&Borrow(owner,c,b,right);}
struct FixtureOwner final:AllocationOwner,ReceiverOwner,CacheOwner {
 bool granted=true;size_t commits=0;void* cache=nullptr;
 bool RefreshAllocations(Graph*,std::vector<Prepared>*)const noexcept override{return granted;}
 bool VerifyStaged(const Graph&,const std::vector<Input>&,std::string*)const override{return granted;}
 bool VerifyPrepared(const Graph&,const Prepared& p,std::string*)const override{return granted&&p.cache==cache&&p.class_index==2;}
 bool VerifyAllocation(const Graph&g,const Prepared&p,std::string*e)const override{return !p.new_class&&VerifyPrepared(g,p,e);}
 bool Verify(const Graph&,const source_commit::Plan&)const noexcept override{return granted;}
 void BeginCommit(const Graph&,const source_commit::Plan&)const noexcept override{++const_cast<FixtureOwner*>(this)->commits;}
};
static void Lifecycle(){
 Fake object("Ljava/lang/Object;"),a("Lpkg/A;",&object),b("Lpkg/B;",&a),parent("Lpkg/P;",&object),i("Lpkg/I;",&object,0x601);
 loaded={&object,&a,&b,&parent,&i};JNINativeInterface_ jni{};jni.FindClass=Find;jni.GetMethodID=Method;jni.GetStaticMethodID=StaticMethod;jni.NewGlobalRef=Global;jni.DeleteGlobalRef=DeleteGlobal;jni.DeleteLocalRef=DeleteLocal;jni.ExceptionCheck=Exception;jni.CallObjectMethodV=Call;jni.CallStaticObjectMethodV=StaticCall;jni.GetArrayLength=Length;jni.GetObjectArrayElement=Element;jni.NewObjectArray=NewArray;jni.SetObjectArrayElement=SetElement;jni.IsSameObject=SameObject;jni.NewStringUTF=String;jni.GetObjectRefType=RefType;JNIEnv env{&jni};
 jvmtiInterface_1 funcs{};funcs.GetLoadedClasses=Loaded;funcs.GetClassSignature=Signature;funcs.GetClassStatus=Status;funcs.GetClassModifiers=Modifiers;funcs.Deallocate=Deallocate;jvmtiEnv ti{&funcs};runnable_jni::Context context{&a,&env,&ti};
 modern_field_metadata::FieldMetadata metadata;assert(metadata.Calibrate(context,nullptr,nullptr,nullptr,0,nullptr));FixtureOwner owner;owner.cache=&metadata;
 Bindings bindings{&metadata,nullptr,Borrow,&owner,&owner,&owner,PairBorrow};Input input;input.strong_original=reinterpret_cast<jclass>(&a);input.before={a.descriptor,object.descriptor,{},1,{},{}};input.after=input.before;input.after.superclass=parent.descriptor;input.after.interfaces={i.descriptor};input.after.access|=0x400u;
 Authority authority;std::string error;i.parent=nullptr;assert(!authority.Stage(context,&ti,{input},bindings,&error));assert(!globals&&locals.empty());i.parent=&object;deny_snapshot=true;assert(!authority.Stage(context,&ti,{input},bindings,&error));deny_snapshot=false;assert(!globals&&locals.empty());deny_view=true;auto denied_queries=os_queries;assert(!authority.Stage(context,&ti,{input},bindings,&error));assert(os_queries==denied_queries+1);deny_view=false;assert(!globals&&locals.empty());assert(authority.Stage(context,&ti,{input},bindings,&error));assert(globals==2&&locals.empty());jvmtiError expected=JVMTI_ERROR_NONE;assert(authority.CheckClass(context,input.strong_original,input.after,&expected));assert(expected==JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED);a.bytes.access_flags|=0x10u;assert(!authority.CheckClass(context,input.strong_original,input.after));a.bytes.access_flags&=~0x10u;auto wrong=input.after;wrong.methods.push_back({"x","()V",1,0});assert(!authority.CheckClass(context,input.strong_original,wrong));
 Fake moved_object=object,moved_a=a,moved_b=b,moved_parent=parent,moved_i=i;moved_a.parent=&moved_object;moved_b.parent=&moved_a;moved_parent.parent=&moved_object;moved_i.parent=&moved_object;
 moved_globals={{reinterpret_cast<jobject>(&object),&moved_object},{reinterpret_cast<jobject>(&a),&moved_a},{reinterpret_cast<jobject>(&b),&moved_b},{reinterpret_cast<jobject>(&parent),&moved_parent},{reinterpret_cast<jobject>(&i),&moved_i}};
 assert(authority.CheckClass(context,input.strong_original,input.after)); // Exact public globals survive GC; old raw pointers cannot authorize.
 Prepared p{&owner,&a,nullptr,&metadata,&parent,2,&input.after};assert(!authority.AuthorizeAllocation(context,p,&error));p.old_class=&moved_a;p.parent=&moved_parent;assert(authority.AuthorizeAllocation(context,p,&error));p.class_index=1;assert(!authority.AuthorizeAllocation(context,p,&error));p.class_index=2;
 Fake new_a(a.descriptor.c_str(),&moved_parent),new_b(b.descriptor.c_str(),&new_a);new_a.bytes.access_flags=input.after.access;new_a.native_interfaces={&moved_i};new_b.native_interfaces={&moved_i};p.new_class=&new_a;new_a.bytes.access_flags=1;assert(!authority.VerifyPrepared(context,p,&error));new_a.bytes.access_flags=input.after.access;assert(authority.VerifyPrepared(context,p,&error));
 auto implicit=input.before;implicit.descriptor=b.descriptor;implicit.superclass=a.descriptor;implicit.interfaces={};Prepared q{&owner,&moved_b,&new_b,&metadata,&new_a,2,&implicit};assert(authority.VerifyPrepared(context,q,&error));
 source_commit::Plan plan;plan.old_classes={&moved_a,&moved_b};plan.new_classes={&new_a,&new_b};owner.granted=false;assert(authority.PrepareRoots(plan,&error));assert(!authority.Verify(plan,0));assert(!authority.Seal(plan,&error));std::vector<void*> root_values;prepared_roots::Registrar registrar{&root_values,[](void* owner,void* value,size_t* slot){auto* roots=static_cast<std::vector<void*>*>(owner);*slot=roots->size();roots->push_back(value);return true;}};assert(authority.CollectManagedRoots(registrar));prepared_roots::Resolver resolver{&root_values,[](void* owner,size_t index)noexcept{return (*static_cast<std::vector<void*>*>(owner))[index];}};
 Fake final_object=moved_object,final_a=moved_a,final_b=moved_b,final_parent=moved_parent,final_i=moved_i,final_new_a=new_a,final_new_b=new_b;
 final_a.parent=&final_object;final_b.parent=&final_a;final_parent.parent=&final_object;final_i.parent=&final_object;final_new_a.parent=&final_parent;final_new_b.parent=&final_new_a;final_new_a.native_interfaces={&final_i};final_new_b.native_interfaces={&final_i};
 std::unordered_map<void*,void*> relocation{{&moved_object,&final_object},{&moved_a,&final_a},{&moved_b,&final_b},{&moved_parent,&final_parent},{&moved_i,&final_i},{&new_a,&final_new_a},{&new_b,&final_new_b}};
 for(auto& value:root_values){auto it=relocation.find(value);if(it!=relocation.end())value=it->second;}plan.old_classes={&final_a,&final_b};plan.new_classes={&final_new_a,&final_new_b};
 bool mapping_allowed=true;modern_field_metadata::ReadContext read_context{&mapping_allowed,[](void*owner,const void*,size_t*out){++context_reads;if(!*static_cast<bool*>(owner))return false;*out=4096;return true;}};authority.UseReadContext(read_context);forbid_os=true;
 forbid_alloc=true;assert(authority.CanRefresh(resolver));authority.Refresh(resolver);assert(!authority.Verify(plan,0));assert(!authority.Seal(plan,nullptr));owner.granted=true;mapping_allowed=false;assert(!authority.Seal(plan,nullptr));mapping_allowed=true;assert(authority.Seal(plan,nullptr));assert(!authority.Seal(plan,nullptr));assert(authority.Verify(plan,0));final_new_a.bytes.access_flags=1;assert(!authority.Verify(plan,0));final_new_a.bytes.access_flags=input.after.access;final_new_a.parent=&final_object;assert(!authority.Verify(plan,0));final_new_a.parent=&final_parent;owner.granted=false;assert(!authority.Verify(plan,0));owner.granted=true;assert(authority.Verify(plan,1));authority.BeginCommit(plan);forbid_alloc=false;forbid_os=false;assert(context_reads>0&&owner.commits==3);
 authority.Clear(&env);moved_globals.clear();assert(!globals&&locals.empty());
 // Public Stage cyclic proposal rejects and releases every independently owned
 // global even though all individual lookups resolve valid Classes.
 input.after.superclass=b.descriptor;assert(!authority.Stage(context,&ti,{input},bindings,&error));assert(!globals&&locals.empty());
 // Exercise complete Authority Stage, not just its storage helper: every Class
 // remains strongly rooted while a single real loader is shared by 30k/60k
 // nodes. Same descriptors in different loaders retain separate identities.
 input.after.superclass=parent.descriptor;
 Fake shared_loader("LSharedLoader;"),other_loader("LOtherLoader;");
 for(size_t class_count:{size_t(30000),size_t(60000)}){
  std::vector<std::unique_ptr<Fake>> many;many.reserve(class_count+2);
  loaded={&object,&a,&b,&parent,&i};
  for(size_t index=0;index<class_count;++index){auto descriptor="Lstress/C"+std::to_string(index)+";";auto value=std::make_unique<Fake>(descriptor.c_str(),&object);value->loader=&shared_loader;loaded.push_back(value.get());many.push_back(std::move(value));}
  for(auto loader_identity:{&shared_loader,&other_loader}){auto value=std::make_unique<Fake>("Lstress/Collision;",&object);value->loader=loader_identity;loaded.push_back(value.get());many.push_back(std::move(value));}
  peak_globals=0;const auto before_queries=os_queries,before_snapshots=snapshots,before_pairs=pair_reads;assert(authority.Stage(context,&ti,{input},bindings,&error));assert(os_queries-before_queries==1&&snapshots-before_snapshots==1);
  const auto* graph=authority.Staged();assert(graph&&graph->nodes.size()==class_count+7&&pair_reads-before_pairs==graph->nodes.size()+2);
  const size_t chunks=(graph->nodes.size()+2+1023)/1024;
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
