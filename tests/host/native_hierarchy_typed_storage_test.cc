#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "cleanup_fixture.inc"
#pragma clang diagnostic pop
#include "platform/common/native_hierarchy_typed_storage.h"
#include "platform/common/native_schema_binding.h"
#include "platform/common/native_typed_heap_calibration.h"
#include "platform/common/native_narrowing_authority.h"
namespace {
using StorageProof=native_hierarchy_typed_storage::Proof;
std::unique_ptr<receiver_guard::Guard> incomplete_guard;std::unique_ptr<receiver_guard::FrameReader> incomplete_frames;std::unique_ptr<native_narrowing::Authority> incomplete_authority;native_hierarchy_graph::Graph incomplete_graph;
void TestNarrowingOrdinary(StorageProof&proof){
 incomplete_guard=std::make_unique<receiver_guard::Guard>(receiver_guard::Access{});incomplete_frames=std::make_unique<receiver_guard::FrameReader>(receiver_guard::FrameAccess{});
 incomplete_authority=std::make_unique<native_narrowing::Authority>(*incomplete_guard,*incomplete_frames,proof);incomplete_graph.nodes.resize(1);incomplete_graph.inputs.push_back(0);std::string error;assert(!incomplete_authority->Prepare(incomplete_graph,&error)&&!error.empty());
}

void* typed_probe_class=nullptr,*typed_probe_instance=nullptr,*typed_ancestor=nullptr,*typed_int=nullptr;
void* typed_array=nullptr,*typed_array_class=nullptr,*typed_old_array_class=nullptr,*typed_ancestor_array=nullptr,*typed_old_array=nullptr;
void* typed_ancestor_2d=nullptr,*typed_old_2d=nullptr,*typed_old_2d_value=nullptr;
std::array<abi::arm64::Field*,4> typed_fields{};std::array<Mirror,4> typed_mirrors{};std::array<Mirror,4> new_mirrors{};
std::array<const char*,4> typed_names{{"referenceValue","staticReferenceValue","ancestor","staticAncestor"}};
std::array<const char*,4> typed_descriptors{{"Ljava/lang/Object;","Ljava/lang/Object;","LAncestor;","LAncestor;"}};
std::vector<void*> typed_classes,typed_public_arrays;ExplicitMappings typed_maps;source_commit::Plan typed_wrong_plan;
bool typed_wrong_public_name=false;size_t typed_failure_globals=0;bool typed_stage_watch=false;bool typed_mirrors_moved=false;std::array<void*,4> typed_moved_mirrors{};size_t typed_stage_native_fields=0;uint64_t typed_stage_native_queries=0;int typed_stage_native_jni=0;bool typed_stage_reentered_jni=false;
void* TypedStageFieldLeaf(void* mirror){
 if(typed_stage_watch){
  if(!typed_stage_native_fields){typed_stage_native_queries=method_identity::MappingOsQueries();typed_stage_native_jni=jni_calls;}
  else typed_stage_reentered_jni|=jni_calls!=typed_stage_native_jni;
  if(typed_mirrors_moved)for(auto& original:typed_mirrors)assert(mirror!=&original);
  ++typed_stage_native_fields;
 }
 return FieldLeaf(mirror);
}
int component_id,primitive_id,field_type_id,typed_loader_id,typed_facade;bool typed_foreign_boot=false;size_t typed_defining_loaders=0;bool typed_helper_calibration=false;bool typed_late_class=false;size_t typed_inventory_passes=0;
using HeapCtor=void(*)(void*,void*,void*,size_t*,bool);using HeapRef=bool(*)(const void*,void*,void*,uint32_t);
HeapCtor typed_ctor=nullptr;HeapRef typed_ref=nullptr;int typed_heap;bool typed_scan_arrays=false;uint32_t typed_array_offset=12;
size_t TypedFieldIndex(void* f){for(size_t i=0;i<typed_fields.size();++i)if(f==typed_fields[i])return i;return SIZE_MAX;}
const char* TypedFieldName(void*f){auto i=TypedFieldIndex(f);return i==SIZE_MAX?ResolutionName(f):typed_names[i];}
const char* TypedFieldType(void*f){auto i=TypedFieldIndex(f);return i==SIZE_MAX?ResolutionType(f):typed_descriptors[i];}
const char* TypedDescriptor(void*c,void*storage){if(c==typed_probe_class)return "LProbe;";if(c==typed_ancestor)return "LAncestor;";return ResolutionDescriptor(c,storage);}
bool typed_code_only=std::getenv("ARTDCEVM_TYPED_ASSIGN_CODE")!=nullptr,typed_code_bad=false;
std::array<void*,11> typed_assignment_roles{};
enum class MatrixFailure {None,Find,Component,Root,Relation};
MatrixFailure matrix_failure=MatrixFailure::None;bool matrix_pending=false;
int matrix_exception_identity;

bool TypedPrimitiveClass(void*p){return p==typed_int||p==typed_assignment_roles[10];}
bool TypedAssignable(void* target,void* value){
 if(target==value)return true;if(target==parent_class)return !TypedPrimitiveClass(value);
 const auto flags=Read32(target,offsetof(class_metadata::Class17,access_flags));
 if(flags&0x200u){auto*t=Root(static_cast<char*>(value)+offsetof(class_metadata::Class17,iftable));if(t)for(uint32_t i=0;i<Read32(t,8);i+=2)if(Root(static_cast<char*>(t)+12+4*i)==target)return true;return false;}
 auto*part=Root(static_cast<char*>(value)+offsetof(class_metadata::Class17,component_type));
 if(part){auto*target_part=Root(static_cast<char*>(target)+offsetof(class_metadata::Class17,component_type));return target_part?TypedAssignable(target_part,part):target==Root(static_cast<char*>(value)+offsetof(class_metadata::Class17,super_class));}
 if(Read32(value,offsetof(class_metadata::Class17,access_flags))&0x200u)return false;
 for(auto*c=value;c;c=Root(static_cast<char*>(c)+offsetof(class_metadata::Class17,super_class)))if(c==target)return true;return false;
}
// Independent public matrix for fixed boot roles, expressed as public Java
// assignability outcomes rather than invoking the original ART leaf model.
bool TypedPublicAssignment(size_t target,size_t actual){
 if(target==actual)return true;if(target==0)return actual<9;
 if(target==2||target==3)return (actual>=4&&actual<=8)||(target==3&&actual==1);
 if(target==4)return actual==5||actual==8;
 return false;
}
jclass TypedAssignmentFind(JNIEnv*,const char*n){
 const char*names[]{"java/lang/Object","java/lang/String","java/lang/Cloneable","java/io/Serializable","[Ljava/lang/Object;","[Ljava/lang/String;","[I","[J","[[I"};
 for(size_t i=0;i<9;++i)if(!std::strcmp(n,names[i])){Jni();if(matrix_failure==MatrixFailure::Find&&i==3){matrix_pending=true;return nullptr;}return reinterpret_cast<jclass>(typed_assignment_roles[i]);}
 return Find(&env,n);
}
size_t TypedAssignableCode(void* target,void* value){return typed_code_bad?256u:(TypedAssignable(target,value)?1u:0u);}
void TypedCtor(void*,void*self,void*h,size_t*failures,bool refs){assert(stopped&&self==current&&h==&typed_heap&&failures&&refs);}
bool TypedRef(const void*,void*,void*,uint32_t){assert(stopped);return true;}
size_t TypedHeap(void*h,bool refs){assert(stopped&&h==&typed_heap&&refs);size_t failures=0;uintptr_t visitor=0;typed_ctor(&visitor,current,h,&failures,true);
 auto field=[&](void*o,uint32_t offset){typed_ref(&visitor,o,Root(static_cast<char*>(o)+offset),offset);};
 field(typed_array,12);field(typed_array,16);field(typed_probe_instance,8);field(typed_probe_class,128);
 field(typed_probe_instance,12);field(typed_probe_class,132);field(old_instance,12);
 if(typed_scan_arrays)field(typed_array,typed_array_offset);
 for(auto p:{typed_array,typed_probe_instance,typed_probe_class,ref_a,ref_b})typed_ref(&visitor,nullptr,p,0);
 return failures;
}
class TypedHooks final:public OwnerHooks {public:
 void* FindSymbol(const char* lib,const char* n)override{
  if(std::strstr(n,"GetArtFieldEv"))return reinterpret_cast<void*>(TypedStageFieldLeaf);
  if(!std::strcmp(n,native_typed_heap_walk::kVerifyHeap))return reinterpret_cast<void*>(TypedHeap);
  if(!std::strcmp(n,native_typed_heap_walk::kVisitorCtor))return reinterpret_cast<void*>(TypedCtor);
  if(!std::strcmp(n,native_typed_heap_walk::kVerifyReference))return reinterpret_cast<void*>(TypedRef);
  if(!std::strcmp(n,"_ZN3art6mirror5Class16IsAssignableFromENS_6ObjPtrIS1_EE"))return typed_probe_class&&!typed_code_only?reinterpret_cast<void*>(TypedAssignable):nullptr;
  if(!std::strcmp(n,"artIsAssignableFromCode"))return reinterpret_cast<void*>(TypedAssignableCode);
  if(std::strstr(n,"GetTypeDescriptor"))return reinterpret_cast<void*>(TypedFieldType);
  if(std::strstr(n,"ArtField7GetName"))return reinterpret_cast<void*>(TypedFieldName);
  if(std::strstr(n,"GetDescriptor"))return reinterpret_cast<void*>(TypedDescriptor);
  return OwnerHooks::FindSymbol(lib,n);
 }
 void* Install(void*t,void*p,void**orig,std::string*e)override{
  if(t==reinterpret_cast<void*>(TypedCtor)){*orig=t;typed_ctor=reinterpret_cast<HeapCtor>(p);return t;}
  if(t==reinterpret_cast<void*>(TypedRef)){*orig=t;typed_ref=reinterpret_cast<HeapRef>(p);return t;}
  return OwnerHooks::Install(t,p,orig,e);
 }
};
jmethodID TypedMethod(JNIEnv*e,jclass c,const char*n,const char*s){if(!std::strcmp(n,"getClassLoader"))return reinterpret_cast<jmethodID>(&typed_loader_id);if(!std::strcmp(n,"getComponentType"))return reinterpret_cast<jmethodID>(&component_id);if(!std::strcmp(n,"isPrimitive"))return reinterpret_cast<jmethodID>(&primitive_id);if(!std::strcmp(n,"getType"))return reinterpret_cast<jmethodID>(&field_type_id);return InterfaceMethod(e,c,n,s);}
jobject TypedObject(JNIEnv*e,jobject o,jmethodID id,va_list args){Jni();
 if(id==reinterpret_cast<jmethodID>(&typed_loader_id))return reinterpret_cast<jobject>(&typed_facade);
 if(id==reinterpret_cast<jmethodID>(&component_id)&&matrix_failure==MatrixFailure::Component&&o==typed_assignment_roles[6]){matrix_pending=true;return nullptr;}
 if(id==reinterpret_cast<jmethodID>(&component_id))return reinterpret_cast<jobject>(Root(static_cast<char*>(static_cast<void*>(o))+offsetof(class_metadata::Class17,component_type)));
 if(id==reinterpret_cast<jmethodID>(&field_type_id)){auto*f=static_cast<Mirror*>(static_cast<void*>(o))->field;const auto i=TypedFieldIndex(f);if(i==2||i==3){typed_late_class=true;return reinterpret_cast<jobject>(typed_ancestor);}return reinterpret_cast<jobject>(parent_class);}
 return InterfaceObject(e,o,id,args);
}
jboolean TypedBoolean(JNIEnv*,jobject o,jmethodID id,va_list){Jni();assert(id==reinterpret_cast<jmethodID>(&primitive_id));return TypedPrimitiveClass(o);}
jobject TypedReflected(JNIEnv*,jclass c,jfieldID f,jboolean){Jni();if(c==typed_probe_class)return reinterpret_cast<jobject>(&typed_mirrors[TypedFieldIndex(f)]);auto& ms=c==reinterpret_cast<jclass>(old_class)?mirrors:new_mirrors;for(size_t i=0;i<4;++i)if(ms[i].field==f)return reinterpret_cast<jobject>(&ms[i]);std::abort();}
jobjectArray TypedNewArray(JNIEnv*,jsize n,jclass,jobject){Jni();void*a=heap.New(12+size_t(n)*4);Put32(a,0,uint32_t(uintptr_t(typed_array_class)));Put32(a,8,uint32_t(n));typed_public_arrays.push_back(a);if(typed_helper_calibration&&n==2)typed_array=a;return reinterpret_cast<jobjectArray>(a);}
jsize TypedLength(JNIEnv*,jarray a){Jni();return jsize(Read32(a,8));}
jobject TypedElement(JNIEnv*,jobjectArray a,jsize i){Jni();assert(i>=0&&i<jsize(Read32(a,8)));return reinterpret_cast<jobject>(uintptr_t(Read32(a,12+4*size_t(i))));}
void TypedSet(JNIEnv*,jobjectArray a,jsize i,jobject v){Jni();assert(i>=0&&i<jsize(Read32(a,8)));Put32(a,12+4*size_t(i),uint32_t(uintptr_t(v)));}
jvmtiError TypedLoaded(jvmtiEnv*,jint*n,jclass**out){assert(!stopped);++typed_inventory_passes;
 if(typed_stage_watch&&typed_inventory_passes>1&&!typed_mirrors_moved){
  // A collector boundary during later public enumeration moves previously
  // staged reflected mirrors. Update their strongly owned managed array slots;
  // the final native pass must decode those slots again, never use old locals.
  for(size_t i=0;i<typed_mirrors.size();++i){auto* moved=heap.New(sizeof(Mirror));std::memcpy(moved,&typed_mirrors[i],sizeof(Mirror));typed_moved_mirrors[i]=moved;
   for(auto* array:typed_public_arrays)for(uint32_t j=0;j<Read32(array,8);++j)
    if(Root(static_cast<char*>(array)+12+4*j)==&typed_mirrors[i])Put32(array,12+4*j,uint32_t(uintptr_t(moved)));
  }typed_mirrors_moved=true;
 }
*n=jint(typed_classes.size()+(typed_late_class?1:0));*out=static_cast<jclass*>(std::malloc(size_t(*n)*sizeof(jclass)));for(size_t i=0;i<typed_classes.size();++i)(*out)[i]=reinterpret_cast<jclass>(typed_classes[i]);if(typed_late_class)(*out)[typed_classes.size()]=reinterpret_cast<jclass>(typed_ancestor);return JVMTI_ERROR_NONE;}
jvmtiError TypedSignature(jvmtiEnv*,jclass c,char**s,char**){const char*names[]{"Ljava/lang/Object;","Ljava/lang/String;","Ljava/lang/Cloneable;","Ljava/io/Serializable;","[Ljava/lang/Object;","[Ljava/lang/String;","[I","[J","[[I","I","J"};
 for(size_t i=0;i<typed_assignment_roles.size();++i)if(c==typed_assignment_roles[i]){*s=strdup(names[i]);return JVMTI_ERROR_NONE;}
 const char*n=c==parent_class?"Ljava/lang/Object;":c==typed_probe_class?"LProbe;":c==typed_ancestor?"LAncestor;":c==typed_int?"I":c==typed_array_class?"[Ljava/lang/Object;":c==typed_ancestor_array?"[LAncestor;":c==typed_old_array_class?"[LFixture;":c==typed_ancestor_2d?"[[LAncestor;":c==typed_old_2d?"[[LFixture;":"LFixture;";*s=strdup(n);return JVMTI_ERROR_NONE;}
jvmtiError TypedDefiningLoader(jvmtiEnv*,jclass c,jobject*out){assert(!stopped);++typed_defining_loaders;*out=((c==parent_class&&!typed_foreign_boot)||(std::find(typed_assignment_roles.begin(),typed_assignment_roles.end(),c)!=typed_assignment_roles.end()&&c!=parent_class))?nullptr:reinterpret_cast<jobject>(&typed_facade);return JVMTI_ERROR_NONE;}
jvmtiError TypedFields(jvmtiEnv*,jclass c,jint*n,jfieldID**out){*n=c==typed_probe_class||c==reinterpret_cast<jclass>(old_class)||c==reinterpret_cast<jclass>(new_class)?4:0;*out=nullptr;if(*n){*out=static_cast<jfieldID*>(std::malloc(4*sizeof(jfieldID)));auto& fs=c==typed_probe_class?typed_fields:c==reinterpret_cast<jclass>(old_class)?old_fields:new_fields;for(size_t i=0;i<4;++i)(*out)[i]=reinterpret_cast<jfieldID>(fs[i]);}return JVMTI_ERROR_NONE;}
jvmtiError TypedFieldPublicName(jvmtiEnv*,jclass,jfieldID f,char**n,char**s,char**){*n=strdup(typed_wrong_public_name&&f==reinterpret_cast<jfieldID>(typed_fields[3])?"wrongPublicName":TypedFieldName(f));*s=strdup(TypedFieldType(f));return JVMTI_ERROR_NONE;}
jvmtiError TypedFieldFlags(jvmtiEnv*,jclass,jfieldID f,jint*out){*out=jint(reinterpret_cast<abi::arm64::Field*>(f)->access_flags);return JVMTI_ERROR_NONE;}
jvmtiError TypedDeallocate(jvmtiEnv*,unsigned char*p){std::free(p);return JVMTI_ERROR_NONE;}
std::unique_ptr<StorageProof> PrepareTyped(TypedHooks&hooks,modern_field_metadata::FieldMetadata& metadata,modern_field_storage::Service& fields,source_handle_roots::Service& source,native_typed_heap_walk::Service&walk,std::string&error){
 auto type=[](void*part=nullptr){auto*c=static_cast<class_metadata::Class17*>(heap.New(160));c->object.klass=uint32_t(uintptr_t(meta_class));c->super_class=uint32_t(uintptr_t(parent_class));c->component_type=uint32_t(uintptr_t(part));c->status=15u<<28;return static_cast<void*>(c);};
 typed_probe_class=type();typed_ancestor=type();typed_int=type();typed_array_class=type(parent_class);typed_ancestor_array=type(typed_ancestor);typed_old_array_class=type(old_class);typed_ancestor_2d=type(typed_ancestor_array);typed_old_2d=type(typed_old_array_class);
 old_class->super_class=uint32_t(uintptr_t(std::getenv("ARTDCEVM_TYPED_SCHEMA_COPY")?parent_class:typed_ancestor));
 auto*raw=static_cast<unsigned char*>(heap.New(4+4*16));Put32(raw,0,4);static_cast<class_metadata::Class17*>(typed_probe_class)->fields=uintptr_t(raw);
 for(size_t i=0;i<4;++i){typed_fields[i]=reinterpret_cast<abi::arm64::Field*>(raw+4+i*16);*typed_fields[i]={uint32_t(uintptr_t(typed_probe_class)),i==1||i==3?8u:1u,uint32_t(i),uint32_t(i==0?8:i==1?128:i==2?12:132)};typed_mirrors[i]={typed_fields[i]};new_mirrors[i]={new_fields[i]};}
 typed_probe_instance=heap.New(24);Put32(typed_probe_instance,0,uint32_t(uintptr_t(typed_probe_class)));Put32(typed_probe_instance,8,uint32_t(uintptr_t(ref_a)));Put32(typed_probe_class,128,uint32_t(uintptr_t(ref_b)));
 typed_array=heap.New(24);Put32(typed_array,0,uint32_t(uintptr_t(typed_array_class)));Put32(typed_array,8,3);Put32(typed_array,12,uint32_t(uintptr_t(ref_a)));Put32(typed_array,16,uint32_t(uintptr_t(ref_b)));
 typed_old_array=heap.New(12);Put32(typed_old_array,0,uint32_t(uintptr_t(typed_old_array_class)));typed_old_2d_value=heap.New(12);Put32(typed_old_2d_value,0,uint32_t(uintptr_t(typed_old_2d)));
 typed_assignment_roles={parent_class,type(),type(),type(),typed_array_class,type(nullptr),type(typed_int),type(),type(),typed_int,type()};
 static_cast<class_metadata::Class17*>(typed_assignment_roles[2])->access_flags=0x201;
 static_cast<class_metadata::Class17*>(typed_assignment_roles[3])->access_flags=0x201;
 Put32(typed_assignment_roles[5],offsetof(class_metadata::Class17,component_type),uint32_t(uintptr_t(typed_assignment_roles[1])));
 Put32(typed_assignment_roles[7],offsetof(class_metadata::Class17,component_type),uint32_t(uintptr_t(typed_assignment_roles[10])));
 Put32(typed_assignment_roles[8],offsetof(class_metadata::Class17,component_type),uint32_t(uintptr_t(typed_assignment_roles[6])));
 for(size_t i=4;i<=8;++i)Put32(typed_assignment_roles[i],offsetof(class_metadata::Class17,iftable),uint32_t(uintptr_t(IfTable({typed_assignment_roles[2],typed_assignment_roles[3]}))));
 Put32(typed_assignment_roles[1],offsetof(class_metadata::Class17,iftable),uint32_t(uintptr_t(IfTable({typed_assignment_roles[3]}))));
 // Primitive Class mirrors have no superclass in the real source declaration.
 Put32(typed_int,offsetof(class_metadata::Class17,super_class),0);Put32(typed_assignment_roles[10],offsetof(class_metadata::Class17,super_class),0);
 table.FindClass=TypedAssignmentFind;
 table.IsAssignableFrom=[](JNIEnv*,jclass actual,jclass target)->jboolean{Jni();
  auto t=std::find(typed_assignment_roles.begin(),typed_assignment_roles.end(),target),a=std::find(typed_assignment_roles.begin(),typed_assignment_roles.end(),actual);
  if(matrix_failure==MatrixFailure::Relation&&t==typed_assignment_roles.begin()+2&&a==typed_assignment_roles.begin()+4){matrix_pending=true;return false;}
  if(t!=typed_assignment_roles.end()&&a!=typed_assignment_roles.end())return TypedPublicAssignment(size_t(t-typed_assignment_roles.begin()),size_t(a-typed_assignment_roles.begin()));
  if(actual==target)return true;if(target==parent_class)return true;return false;};
 table.GetFieldID=[](JNIEnv*,jclass c,const char*n,const char*s){Jni();assert(c==typed_probe_class&&!std::strcmp(n,"referenceValue")&&!std::strcmp(s,"Ljava/lang/Object;"));return reinterpret_cast<jfieldID>(typed_fields[0]);};
 table.GetStaticFieldID=[](JNIEnv*,jclass c,const char*n,const char*s){Jni();assert(c==typed_probe_class&&!std::strcmp(n,"staticReferenceValue")&&!std::strcmp(s,"Ljava/lang/Object;"));return reinterpret_cast<jfieldID>(typed_fields[1]);};
 table.GetMethodID=TypedMethod;table.CallObjectMethodV=TypedObject;table.CallBooleanMethodV=TypedBoolean;table.ToReflectedField=TypedReflected;table.GetStaticObjectField=[](JNIEnv*e,jclass o,jfieldID f){return GetObject(e,o,f);};
 table.NewObjectArray=TypedNewArray;table.GetArrayLength=TypedLength;table.GetObjectArrayElement=TypedElement;table.SetObjectArrayElement=TypedSet;
 exclusive_owned_roots::Service exclusive;assert(exclusive_owned_roots::Service::Bind(hooks,&exclusive,&error));typed_helper_calibration=true;
 assert(native_typed_heap_calibration::Prepare(hooks,&env,reinterpret_cast<jobject>(typed_probe_instance),&typed_heap,Current,metadata,exclusive,&walk,&error));typed_helper_calibration=false;
 assert(walk.Calibrated());
 typed_array=heap.New(24);Put32(typed_array,0,uint32_t(uintptr_t(typed_array_class)));Put32(typed_array,8,3);Put32(typed_array,12,uint32_t(uintptr_t(ref_a)));Put32(typed_array,16,uint32_t(uintptr_t(ref_b)));

 void*self=current;JNIEnv*source_env=&env;std::memcpy(thread.data()+216,&self,8);std::memcpy(thread.data()+200,&source_env,8);typed_env_owner.self=self;typed_env_owner.vm=&runtime;table.GetJavaVM=PairVM;assert(source_handle_roots::Service::Bind(hooks,fields,runnable_jni::Context{self,&env,&runtime},&source,&error));
 alignas(8) static unsigned char source_scope[32]{};OriginalHeader head{nullptr,3};std::memcpy(source_scope,&head,sizeof(head));Put32(source_scope,12,uint32_t(uintptr_t(old_class)));public_slot=source_scope+12;Publish(source_scope);
 typed_classes={old_class,new_class,parent_class,meta_class,typed_probe_class,typed_int,typed_array_class,typed_ancestor_array,typed_old_array_class,typed_ancestor_2d,typed_old_2d};
 for(auto*role:typed_assignment_roles)if(std::find(typed_classes.begin(),typed_classes.end(),role)==typed_classes.end())typed_classes.push_back(role);
 static jvmtiInterface_1 functions{};functions.GetClassLoader=TypedDefiningLoader;functions.GetLoadedClasses=TypedLoaded;functions.GetClassSignature=TypedSignature;functions.GetClassFields=TypedFields;functions.GetFieldName=TypedFieldPublicName;functions.GetFieldModifiers=TypedFieldFlags;functions.Deallocate=TypedDeallocate;static jvmtiEnv ti{&functions};
 auto result=std::make_unique<StorageProof>(walk,source,metadata);
 struct Request{source_handle_roots::Service*source;StorageProof*proof;std::string*error;} request{&source,result.get(),&error};
 // Hook owner is a real dependency, never inferred from a staged bool.
 struct StageRequest{Request*request;TypedHooks*hooks;native_typed_heap_walk::Service*walk;modern_field_metadata::FieldMetadata*metadata;} sr{&request,&hooks,&walk,&metadata};
 auto actual_stage=[](const runnable_jni::Context&c,void*p){auto&r=*static_cast<StageRequest*>(p);if(!r.request->source->ObserveAllocatorHandle(c,reinterpret_cast<jobject>(old_class),public_slot,r.request->error))return false;auto facade=c.env->CallObjectMethod(reinterpret_cast<jobject>(parent_class),reinterpret_cast<jmethodID>(&typed_loader_id));assert(facade==reinterpret_cast<jobject>(&typed_facade));c.env->DeleteLocalRef(facade);
 const auto stage_queries=method_identity::MappingOsQueries();typed_stage_watch=true;typed_stage_native_fields=0;typed_stage_reentered_jni=false;
 const bool staged=r.request->proof->Stage(*r.hooks,c,&ti,r.request->error);typed_stage_watch=false;
 std::cout<<"Actual typed Stage mapping queries="<<method_identity::MappingOsQueries()-stage_queries<<" reference-fields="<<typed_stage_native_fields<<" post-first-native-field queries="<<method_identity::MappingOsQueries()-typed_stage_native_queries<<" JNI-reentered="<<typed_stage_reentered_jni<<"\n";
 if(!staged)return false;
 assert(typed_mirrors_moved&&typed_stage_native_fields==8&&!typed_stage_reentered_jni);
 assert(method_identity::MappingOsQueries()==typed_stage_native_queries);
 // Same real public inventory and rooted identities, but a broken C ABI
 // encoding must not become truthy assignment or a staged proof.
 const auto new_global=table.NewGlobalRef;const auto delete_global=table.DeleteGlobalRef;
 table.NewGlobalRef=[](JNIEnv*e,jobject value){++typed_failure_globals;return Global(e,value);};
 table.DeleteGlobalRef=[](JNIEnv*e,jobject value){assert(typed_failure_globals);--typed_failure_globals;Delete(e,value);};
 const bool prior_code=typed_code_only;typed_code_only=typed_code_bad=true;
 StorageProof bad_encoding(*r.walk,*r.request->source,*r.metadata);
 assert(!bad_encoding.Stage(*r.hooks,c,&ti,r.request->error)&&!bad_encoding.Staged()&&*r.request->error=="Named Class assignment leaf differs from independent JNI relation or result encoding");
 bad_encoding.Clear(c.env);assert(!typed_failure_globals);typed_code_bad=false;
 // Actual public JNI leaf failures must identify their role/operation, retain
 // the original pending Throwable and release only this failed Proof's roots.
 // Context identity is independent of executor TLS. Foreign self/VM must
 // refuse exact source-root pair publication, even with valid public objects.
 for(const bool foreign_vm:{false,true}){
  auto wrong=c;if(foreign_vm)wrong.vm=&typed_facade;else wrong.self=foreign_thread.data();
  StorageProof foreign_context(*r.walk,*r.request->source,*r.metadata);r.request->error->clear();
  assert(!foreign_context.Stage(*r.hooks,wrong,&ti,r.request->error)&&!foreign_context.Staged());
  assert(r.request->error->find("root Stage target=java/lang/Object")!=std::string::npos);
  foreign_context.Clear(c.env);assert(!typed_failure_globals);
 }
 const auto check_exception=table.ExceptionCheck;const auto occurred=table.ExceptionOccurred;
 table.ExceptionCheck=[](JNIEnv*)->jboolean{Jni();return matrix_pending;};
 table.ExceptionOccurred=[](JNIEnv*)->jthrowable{Jni();return matrix_pending?reinterpret_cast<jthrowable>(&matrix_exception_identity):nullptr;};
 const auto matrix_global=table.NewGlobalRef;
 table.NewGlobalRef=[](JNIEnv*e,jobject value)->jobject{if(matrix_failure==MatrixFailure::Root&&value==typed_assignment_roles[2]){matrix_pending=true;return nullptr;}++typed_failure_globals;return Global(e,value);};
 for(const auto which:{MatrixFailure::Find,MatrixFailure::Component,MatrixFailure::Root,MatrixFailure::Relation}){
  matrix_failure=which;StorageProof rejected(*r.walk,*r.request->source,*r.metadata);
  assert(!rejected.Stage(*r.hooks,c,&ti,r.request->error)&&!rejected.Staged());
  const char* phase=which==MatrixFailure::Find?"FindClass target=java/io/Serializable":which==MatrixFailure::Component?"getComponentType target=int.class":which==MatrixFailure::Root?"root Stage target=java/lang/Cloneable":"IsAssignableFrom target=java/lang/Cloneable source=[Ljava/lang/Object;";
  assert(r.request->error->find(phase)!=std::string::npos&&r.request->error->find("pending JNI exception preserved")!=std::string::npos);
  assert(c.env->ExceptionOccurred()==reinterpret_cast<jthrowable>(&matrix_exception_identity));
  rejected.Clear(c.env);assert(!typed_failure_globals&&matrix_pending);
  matrix_pending=false;matrix_failure=MatrixFailure::None;
 }
 table.NewGlobalRef=matrix_global;table.ExceptionCheck=check_exception;table.ExceptionOccurred=occurred;
 typed_code_only=prior_code;
 typed_foreign_boot=true;StorageProof foreign(*r.walk,*r.request->source,*r.metadata);assert(!foreign.Stage(*r.hooks,c,&ti,r.request->error)&&!foreign.Staged());foreign.Clear(c.env);assert(!typed_failure_globals);typed_foreign_boot=false;
 // A mismatching independent public declaration refuses in the late native
 // pass after preceding identities were read. Failed staging owns no published
 // proof, and Clear refunds every global produced by that failed attempt.
 typed_wrong_public_name=true;StorageProof mismatch(*r.walk,*r.request->source,*r.metadata);
 assert(!mismatch.Stage(*r.hooks,c,&ti,r.request->error)&&!mismatch.Staged()&&*r.request->error=="Native typed field differs from independent public declaration");
 mismatch.Clear(c.env);assert(!typed_failure_globals);typed_wrong_public_name=false;
 table.NewGlobalRef=new_global;table.DeleteGlobalRef=delete_global;return true;};
 typed_maps.Cover(heap.base);
 // The original ART source caller establishes Runnable ownership itself. It
 // never enters With and must not borrow the executor's TLS decode permission.
 assert(!runnable_executor::host_executor_active);Put32(thread.data(),0,0);
 const runnable_jni::Context original_context{current,&env,&runtime};void* executor_borrow=nullptr;
 assert(!runnable_executor::DecodeStrongGlobal(original_context,reinterpret_cast<jobject>(old_class),&executor_borrow)&&!executor_borrow);
 const bool original_staged=actual_stage(original_context,&sr);Put32(thread.data(),0,92u<<24);
 if(!original_staged){std::cerr<<"typed Stage: "<<error<<"\n";std::abort();}assert(typed_inventory_passes>=3&&typed_late_class&&typed_defining_loaders>=typed_classes.size());return result;
}
void MoveTypedRootsAfterStage(){
 // A real collector boundary double moves the Class and updates compressed
 // managed array children/native Class edges. The Proof's borrowed Stage
 // pointers are deliberately left stale; Capture must reload its owned slots.
 const auto previous=typed_ancestor;auto*moved=heap.New(160);std::memcpy(moved,previous,160);typed_ancestor=moved;
 if(!std::getenv("ARTDCEVM_TYPED_SCHEMA_COPY"))old_class->super_class=uint32_t(uintptr_t(moved));Put32(typed_ancestor_array,offsetof(class_metadata::Class17,component_type),uint32_t(uintptr_t(moved)));
 for(auto*array:typed_public_arrays)for(uint32_t i=0;i<Read32(array,8);++i)if(Root(static_cast<char*>(array)+12+4*i)==previous)Put32(array,12+4*i,uint32_t(uintptr_t(moved)));
}
void TestTypedCells(bool prepared,StorageProof& proof,const source_commit::Plan& plan,const native_required_cleanup::Service&cleanup,void*driver,const transaction_root::Holder*holder,const native_source_commit::Services&copy){
 auto&mappings=typed_maps;modern_field_metadata::ReadContext context{&mappings,ExplicitMappings::Extent};const auto os=method_identity::MappingOsQueries();
 const auto check=[&](const source_commit::Plan&p,const modern_field_metadata::ReadContext&ctx,void*d,const native_source_commit::Services&delegate)noexcept{return prepared?proof.ValidatePrepared(p,ctx,cleanup,d,holder,delegate):proof.Validate(p,ctx,cleanup,d,holder,delegate);};
 assert(check(plan,context,driver,copy));
 assert(prepared?!proof.Validate(plan,context,cleanup,driver,holder,copy):!proof.ValidatePrepared(plan,context,cleanup,driver,holder,copy));
 if(!prepared)assert(incomplete_authority&&!incomplete_authority->BindRun(cleanup,driver,holder,copy)&&!incomplete_authority->Validate(incomplete_graph,plan,context));
 // A real complete retained-cell proof and actual final cleanup cannot supply
 // any missing independently witnessed source/listener/receiver call path.

 Put32(old_instance,12,uint32_t(uintptr_t(old_instance)));assert(check(plan,context,driver,copy));Put32(old_instance,12,uint32_t(uintptr_t(ref_a)));
 if(std::getenv("ARTDCEVM_TYPED_SCHEMA_COPY")){bool retained=false;assert(copy.RetainsSourceField(plan,false,old_class,old_fields[2],&retained)&&retained);const auto original_parent=old_class->super_class;old_class->super_class=uint32_t(uintptr_t(typed_ancestor));assert(!check(plan,context,driver,copy));old_class->super_class=original_parent;assert(check(plan,context,driver,copy));}
 const auto&wrong=typed_wrong_plan;assert(!check(wrong,context,driver,copy));assert(!check(plan,context,&typed_heap,copy));
 Put32(typed_probe_instance,12,uint32_t(uintptr_t(old_instance)));assert(!check(plan,context,driver,copy)&&proof.LastRefusal()==StorageProof::Refusal::IncompatibleField);Put32(typed_probe_instance,12,0);
 Put32(typed_probe_class,132,uint32_t(uintptr_t(old_instance)));assert(!check(plan,context,driver,copy)&&proof.LastRefusal()==StorageProof::Refusal::IncompatibleField);Put32(typed_probe_class,132,0);
 // Source ordinary fields/arrays with Object declaration retain legality.
 Put32(typed_array,20,uint32_t(uintptr_t(old_instance)));typed_scan_arrays=true;typed_array_offset=20;assert(check(plan,context,driver,copy));
 Put32(typed_array,0,uint32_t(uintptr_t(typed_ancestor_array)));assert(!check(plan,context,driver,copy));
 Put32(typed_array,0,uint32_t(uintptr_t(typed_array_class)));Put32(typed_array,20,uint32_t(uintptr_t(typed_old_array)));assert(check(plan,context,driver,copy));
 Put32(typed_array,0,uint32_t(uintptr_t(typed_ancestor_2d)));assert(!check(plan,context,driver,copy));
 Put32(typed_array,0,uint32_t(uintptr_t(typed_array_class)));Put32(typed_array,20,0);assert(check(plan,context,driver,copy));
 modern_field_metadata::ReadContext foreign_context{&typed_heap,ExplicitMappings::Extent};assert(!check(plan,foreign_context,driver,copy));
 mappings.reject_all=true;assert(!check(plan,context,driver,copy));mappings.reject_all=false;
 assert(method_identity::MappingOsQueries()==os);std::cout<<"Complete typed storage: public resolving fixed point/collision identities, native roots, exact Plan/Holder/final stop, retained Object field, illegal instance/static Ancestor cells, unchanged array/multidimensional component relations, null and mapping refusal; collected/final no JNI/new/OS PASS\n";
}
}
int main(int argc,char**argv){
#include "cleanup_main.inc"
