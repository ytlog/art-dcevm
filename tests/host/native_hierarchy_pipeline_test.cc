#include "source_cache_walker_fixture.inc"
#include "platform/common/native_hierarchy_graph.h"
#include "platform/common/native_hierarchy_allocator.h"
#include "platform/common/native_hierarchy_receiver.h"
#include "platform/common/native_hierarchy_source_checks.h"
#include "platform/common/native_hierarchy_source_cache.h"
#include "platform/common/dex_mapping_abi.h"
#include "platform/common/method_cache_lookup.h"
#include "platform/abi/arm64/android-common/class_redefinition_prefix.h"
#include <fstream>
#include <iterator>
#include <new>
static bool forbid_allocation=false;
void*operator new(size_t n){if(forbid_allocation)std::abort();if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete(void*p,size_t)noexcept{std::free(p);}
using namespace artdcevm::native_hierarchy_graph;
namespace {
struct PublicClass {void* native;std::string descriptor;void* parent;std::vector<void*>interfaces;};
std::vector<PublicClass>public_classes;std::string subject_descriptor;
void*relocated_new=nullptr;void*original_file=nullptr;void*incoming_file=nullptr;void*published_cache=nullptr;
const char*SourceDescriptor(void*p,void*){if(p==new_class||p==relocated_new)return subject_descriptor.c_str();for(const auto&c:public_classes)if(c.native==p)return c.descriptor.c_str();return p==parent_class?"Ljava/lang/Object;":"LFixture;";}
struct Driver {void*env;int32_t result=113;uint32_t pad=0;void*runtime;void*self;int32_t type=0;};
Driver actual_driver{};abi::arm64::android_common::ClassRedefinitionPrefix actual_owner{};
transaction_root::Holder*actual_holder=nullptr;source_handle_roots::Service*root_service=nullptr;
native_hierarchy_allocator::Service*allocator_service=nullptr;void*selected_parent=nullptr;bool held=false;void*held_object=nullptr;unsigned allocated=0;int monitor_id;
bool IsRegistered(const void*file){return file==original_file;}
void*FindDexCache(const void*file){return IsRegistered(file)?published_cache:nullptr;}
abi::arm64::android11::ReleaseObjectPointer Enter(void*self,abi::arm64::android11::ReleaseObjectPointer p,bool attempt){assert(self==thread.data()&&!held&&!attempt);held=true;held_object=reinterpret_cast<void*>(p.reference);return p;}
bool Exit(void*self,abi::arm64::android11::ReleaseObjectPointer p){assert(self==thread.data()&&held&&held_object==reinterpret_cast<void*>(p.reference));held=false;held_object=nullptr;return true;}
jmethodID StaticMethod(JNIEnv*,jclass,const char*n,const char*){return reinterpret_cast<jmethodID>(!std::strcmp(n,"holdsLock")?uintptr_t(&monitor_id):4);}
jboolean Holds(JNIEnv*,jclass,jmethodID id,va_list args){Jni();assert(id==reinterpret_cast<jmethodID>(&monitor_id));return held&&held_object==va_arg(args,jobject);}
class SourceHooks:public RootHooks {public:void*FindSymbol(const char*lib,const char*n)override{if(std::strstr(n,"Class13GetDescriptor"))return reinterpret_cast<void*>(SourceDescriptor);if(std::strstr(n,"MonitorEnter"))return reinterpret_cast<void*>(Enter);if(std::strstr(n,"MonitorExit"))return reinterpret_cast<void*>(Exit);return RootHooks::FindSymbol(lib,n);}};
abi::arm64::android11::ReleaseObjectPointer OriginalFind(void*linker_arg,void*self,const char*d,void*loader){assert(linker_arg==&linker&&self==thread.data()&&!Read32(loader));if(allocated==2&&!relocated_new&&subject_descriptor==d){relocated_new=heap.New(160);std::memcpy(relocated_new,new_class,160);MoveRoots mover;mover.old_value=uint32_t(uintptr_t(new_class));mover.next_value=uint32_t(uintptr_t(relocated_new));OriginalRoots(self,&mover,0);}
 for(const auto&c:public_classes)if(c.descriptor==d)return {uintptr_t(c.native)};return {0};}
void OriginalLoad(void*l,void*self,const void*,const void*,const void*){assert(l==&linker&&self==thread.data());}
bool OriginalIds(const void*){return true;}
void Failure(void*owner,int code,const char*message)noexcept{assert(owner==&actual_owner);std::cerr<<"source failure "<<code<<": "<<message<<"\n";}
void Retire(void*object,uint8_t status){assert(held&&held_object==object&&status==1);Put32(object,offsetof(class_metadata::Class17,status),uint32_t(status)<<28);}
abi::arm64::android11::ReleaseObjectPointer OriginalAllocate(void*owner,void*old,void*parent,void*cache,uint16_t index){
 assert(owner==&actual_owner&&Read32(old)==uintptr_t(old_class)&&Read32(parent)==uintptr_t(selected_parent)&&index==0);++allocated;
 new_class->super_class=Read32(parent);new_class->dex_cache=Read32(cache);new_class->dex_class_def_index=0;new_class->status=15u<<28;new_class->access_flags=old_class->access_flags;
 auto scope=root_service->CreateScope(1,nullptr);assert(scope);scope->Set(0,new_class);
 allocator_service->LoadClass(&linker,thread.data(),incoming_file,nullptr,scope->Slot(0));assert(allocator_service->EnsureInstanceIds(scope->Slot(0)));return {uintptr_t(scope->Get(0))};
}

using SourceFailure=void(*)(void*,jvmtiError,const void*,const void*);
SourceFailure source_failure_proxy=nullptr;bool(*source_check_proxy)(void*)=nullptr;
void RecordSourceFailure(void*driver,jvmtiError error,const void*,const void*){assert(driver==&actual_driver);actual_driver.result=error;}
bool OriginalCheck(void*owner){source_failure_proxy(&actual_driver,JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,static_cast<char*>(owner)+offsetof(abi::arm64::android_common::ClassRedefinitionPrefix,class_signature),nullptr);return false;}
void StructuralMarker(void*owner){static_cast<abi::arm64::android_common::ClassRedefinitionPrefix*>(owner)->added_methods=true;}
void SourceRefuse(void*owner,int error,const char*)noexcept{assert(owner==&actual_owner);actual_driver.result=error;}
class CheckHooks final:public SourceHooks {
 public:void*FindSymbol(const char*library,const char*name)override{
  if(std::strstr(name,"ClassRedefinition10CheckClassEv"))return reinterpret_cast<void*>(OriginalCheck);
  if(std::strstr(name,"Redefiner13RecordFailure"))return reinterpret_cast<void*>(RecordSourceFailure);
  if(std::strstr(name,"RecordNewMethodAdded"))return reinterpret_cast<void*>(StructuralMarker);
  return SourceHooks::FindSymbol(library,name);
 }
 void*Install(void*target,void*proxy,void**original,std::string*error)override{
  if(target==reinterpret_cast<void*>(OriginalCheck)||target==reinterpret_cast<void*>(RecordSourceFailure)){assert(stopped==1);*original=target;if(target==reinterpret_cast<void*>(OriginalCheck))source_check_proxy=reinterpret_cast<decltype(source_check_proxy)>(proxy);else source_failure_proxy=reinterpret_cast<SourceFailure>(proxy);return this;}
  return SourceHooks::Install(target,proxy,original,error);
 }
};
PublicClass&Public(void*p){for(auto&c:public_classes)if(c.native==p)return c;std::abort();}
jmethodID Method(JNIEnv*,jclass,const char*n,const char*){return reinterpret_cast<jmethodID>(!std::strcmp(n,"getSuperclass")?1:!std::strcmp(n,"getInterfaces")?2:3);}
struct Array{std::vector<void*>values;};
std::vector<std::pair<jobject,bool>> local_objects;
void LocalDelete(JNIEnv*,jobject p){for(auto i=local_objects.begin();i!=local_objects.end();++i)if(i->first==p){if(i->second)delete reinterpret_cast<Array*>(p);else delete reinterpret_cast<std::string*>(p);local_objects.erase(i);return;}}
jobject Call(JNIEnv*,jobject p,jmethodID id,va_list){Jni();auto&c=Public(p);switch(uintptr_t(id)){case 1:return reinterpret_cast<class_metadata::Class17*>(p)->access_flags&0x200u?nullptr:reinterpret_cast<jobject>(c.parent);case 2:{auto value=reinterpret_cast<jobject>(new Array{c.interfaces});local_objects.emplace_back(value,true);return value;}default:return nullptr;}}
jsize Length(JNIEnv*,jarray a){Jni();return jsize(reinterpret_cast<Array*>(a)->values.size());}
jobject Element(JNIEnv*,jobjectArray a,jsize i){Jni();return reinterpret_cast<jobject>(reinterpret_cast<Array*>(a)->values[size_t(i)]);}
jstring String(JNIEnv*,const char*s){Jni();auto value=reinterpret_cast<jstring>(new std::string(s));local_objects.emplace_back(value,false);return value;}
jobject Lookup(JNIEnv*,jclass,jmethodID,va_list args){Jni();auto*s=reinterpret_cast<std::string*>(va_arg(args,jstring));(void)va_arg(args,int);assert(!va_arg(args,jobject));std::string descriptor="L"+*s+";";std::replace(descriptor.begin(),descriptor.end(),'.','/');for(auto&c:public_classes)if(c.descriptor==descriptor)return reinterpret_cast<jobject>(c.native);return nullptr;}
jvmtiError Loaded(jvmtiEnv*,jint*n,jclass**out){*n=jint(public_classes.size());*out=static_cast<jclass*>(std::malloc(public_classes.size()*sizeof(jclass)));for(size_t i=0;i<public_classes.size();++i)(*out)[i]=reinterpret_cast<jclass>(public_classes[i].native);return JVMTI_ERROR_NONE;}
jvmtiError Signature(jvmtiEnv*,jclass p,char**out,char**){*out=strdup(Public(p).descriptor.c_str());return JVMTI_ERROR_NONE;}
jvmtiError Modifiers(jvmtiEnv*,jclass p,jint*out){*out=jint(static_cast<class_metadata::Class17*>(reinterpret_cast<void*>(p))->access_flags);return JVMTI_ERROR_NONE;}
jvmtiError Status(jvmtiEnv*,jclass,jint*out){*out=JVMTI_CLASS_STATUS_PREPARED|JVMTI_CLASS_STATUS_INITIALIZED;return JVMTI_ERROR_NONE;}
jvmtiError Deallocate(jvmtiEnv*,unsigned char*p){std::free(p);return JVMTI_ERROR_NONE;}
bool Borrow(void*raw,const runnable_jni::Context&c,jobject p,void**out){return static_cast<source_handle_roots::Service*>(raw)->BorrowOwnedStrongGlobal(c,p,out);}
// These are source runtime leaf doubles: actual DEX registration/ClassTable and
// stop witnesses. All graph/allocator/receiver/cache/roots policies below are
// real production implementations, never bool CacheOwner substitutes.
class SourceOwners final:public native_hierarchy_source_cache::RequiredServices {
 const runnable_jni::Context&context_;source_handle_roots::Pool&pool_;
 public:SourceOwners(const runnable_jni::Context&c,source_handle_roots::Pool&p):context_(c),pool_(p){}
 bool VerifyStaged(const Graph&g,const std::vector<Input>&inputs)const noexcept override{return Current()==context_.self&&pool_.ReadyForSourceAccess()&&g.nodes.size()==public_classes.size()&&inputs.size()==1&&!stopped;}
 bool VerifyPrivate(const native_hierarchy_source_cache::RecordView&r,const native_hierarchy_cache::Source&s,const runnable_jni::Context&c,const source_handle_roots::Pool&p)const noexcept override{
  void*old=nullptr,*cache=nullptr;void*holder_array=Root(const_cast<uint32_t*>(actual_holder->array_handle));
  return &p==&pool_&&c.self==context_.self&&Current()==c.self&&r.source_owner==&actual_owner&&actual_owner.driver==&actual_driver&&actual_driver.self==c.self&&actual_owner.dex_file==s.dex_identity&&s.dex_identity==incoming_file&&!IsRegistered(s.dex_identity)&&!FindDexCache(s.dex_identity)&&p.Read(r.old_root,&old)&&old==old_class&&p.Read(r.cache_root,&cache)&&cache==Root(static_cast<unsigned char*>(holder_array)+12+12)&&r.cache_root==s.cache_root;
 }
 bool VerifyReadonlyStaged(const Graph&,const native_hierarchy_source_cache::RecordView&)const noexcept override{return false;}
 bool VerifyReadonlyPrepared(const Graph&,const native_hierarchy_source_cache::RecordView&,const Prepared&)const noexcept override{return false;}
 bool VerifyReadonly(const Graph&,const native_hierarchy_source_cache::RecordView&,const source_commit::Plan&,size_t)const noexcept override{return false;}
 bool VerifyFrozen(const Graph&,const Prepared&p,const native_hierarchy_cache::Source&s)const noexcept override{native_hierarchy_source_cache::RecordView r{p.source_owner,p.class_index,const_cast<void*>(p.source_old_root),nullptr,s.cache_root,nullptr,p.declarations,{}};return VerifyPrivate(r,s,context_,pool_)&&p.new_class&&p.new_class!=p.old_class;}
 bool VerifyCommit(const Graph&g,const source_commit::Plan&p)const noexcept override{return stopped==1&&Current()==context_.self&&pool_.ReadyForSourceAccess()&&g.inputs.size()==1&&p.old_classes.size()==1&&p.new_classes.size()==1&&p.old_classes[0]==old_class&&p.new_classes[0]!=old_class&&p.mirror==old_class&&p.class_root_origins.size()==1&&p.class_root_origins[0].array==p.new_classes_root&&p.class_root_origins[0].index==0&&Read32(static_cast<unsigned char*>(p.new_classes_root)+12)==uintptr_t(p.new_classes[0])&&Read32(static_cast<unsigned char*>(Root(const_cast<uint32_t*>(actual_holder->array_handle)))+12+11*4)==uintptr_t(p.new_classes_root);}
 void BeginCommit(const Graph&g,const source_commit::Plan&p)const noexcept override{assert(VerifyCommit(g,p));}
};
}
#include "source_cache_calibration_main.inc"
 std::ifstream input(PIPELINE_DEX_FIXTURE,std::ios::binary);assert(input.good());std::vector<unsigned char>dex{std::istreambuf_iterator<char>(input),{}};
 evolution::MappedDexMetadata mapping{dex.data(),dex.size(),dex.data(),dex.size(),0};evolution::ClassSchema schema;assert(evolution::ReadMappedDexDeclarations(mapping,&schema,&error));
 struct Use{source_handle_roots::Service*roots;modern_field_metadata::FieldMetadata*metadata;modern_field_storage::Service*storage;evolution::ClassSchema*schema;evolution::MappedDexMetadata mapping;} use{&service,&metadata,&fields,&schema,mapping};
 auto run=[](const runnable_jni::Context&context,void*raw){auto&d=*static_cast<Use*>(raw);std::string error;subject_descriptor=d.schema->descriptor;const auto pkg=subject_descriptor.substr(0,subject_descriptor.find_last_of('/')+1);
  assert(d.schema->superclass==pkg+"HierarchyInserted;"&&d.schema->interfaces.size()==1);public_classes.clear();
  auto make=[](uint32_t flags){auto*p=static_cast<class_metadata::Class17*>(heap.New(160));InitializeClass(p);p->access_flags=flags;return p;};
  auto*root=make(1);auto*base=make(1);auto*inserted=make(1);auto*tag=make(0x601);auto*more=make(0x601);
  auto*object=static_cast<class_metadata::Class17*>(parent_class);object->access_flags=1;root->super_class=uint32_t(uintptr_t(object));base->super_class=uint32_t(uintptr_t(root));inserted->super_class=uint32_t(uintptr_t(base));tag->super_class=more->super_class=uint32_t(uintptr_t(object));old_class->super_class=uint32_t(uintptr_t(base));old_class->access_flags=1;
  public_classes={{object,"Ljava/lang/Object;",nullptr,{}},{root,pkg+"HierarchyRoot;",object,{}},{base,pkg+"HierarchyBase;",root,{}},{inserted,pkg+"HierarchyInserted;",base,{}},{tag,pkg+"HierarchyTag;",nullptr,{}},{more,pkg+"HierarchyMore;",nullptr,{tag}},{old_class,subject_descriptor,base,{more}}};
  auto*array_type=make(0x11);array_type->component_type=uint32_t(uintptr_t(object));
  auto iftable=[&](std::initializer_list<void*>interfaces){auto*p=heap.New(12+interfaces.size()*8);Put32(p,0,uint32_t(uintptr_t(array_type)));Put32(p,8,uint32_t(interfaces.size()*2));size_t i=0;for(auto*v:interfaces){Put32(p,12+i*8,uint32_t(uintptr_t(v)));++i;}return p;};
  auto*empty=iftable({});for(auto&c:public_classes)Put32(c.native,offsetof(class_metadata::Class17,iftable),uint32_t(uintptr_t(empty)));more->iftable=uint32_t(uintptr_t(iftable({tag})));old_class->iftable=uint32_t(uintptr_t(iftable({more,tag})));new_class->iftable=old_class->iftable;
  source_handle_roots::Pool pool(*d.roots);auto*cache=heap.New(112);published_cache=heap.New(112);old_class->dex_cache=uint32_t(uintptr_t(published_cache));new_class->dex_cache=uint32_t(uintptr_t(cache));
  auto*file=static_cast<dex_mapping_abi::Prefix*>(heap.New(48));*file={nullptr,d.mapping.begin,0xdeadbeef,d.mapping.data_begin,d.mapping.data_size};incoming_file=file;auto*original=static_cast<dex_mapping_abi::Prefix*>(heap.New(48));*original=*file;original_file=original;
  uint64_t incoming_pointer=uintptr_t(file),original_pointer=uintptr_t(original);std::memcpy(static_cast<char*>(cache)+16,&incoming_pointer,8);std::memcpy(static_cast<char*>(published_cache)+16,&original_pointer,8);
  actual_driver={};actual_driver.env=context.env;actual_driver.self=context.self;actual_owner={};actual_owner.driver=&actual_driver;actual_owner.klass=reinterpret_cast<jclass>(old_class);actual_owner.dex_file=file;
  auto*array=heap.New(12+13*4);Put32(array,0,uint32_t(uintptr_t(array_type)));Put32(array,8,13);Put32(array,12+3*4,uint32_t(uintptr_t(cache)));Put32(array,12+4*4,uint32_t(uintptr_t(old_class)));
  auto*array_root=pool.Add(array,&error);assert(array_root);transaction_root::Owners owners{reinterpret_cast<const unsigned char*>(&actual_owner),reinterpret_cast<const unsigned char*>(&actual_owner+1),reinterpret_cast<const unsigned char*>(&actual_owner+1)};uint64_t initialized=0,structural=1,initial=1;
  transaction_root::Holder holder{static_cast<const uint32_t*>(array_root),&owners,{&initialized,1,1},{&structural,1,1},{&initial,1,1}};actual_holder=&holder;
  SourceHooks hooks;native_hierarchy_allocator::Service allocation;runtime_services::Snapshot runtime;runtime.receivers.class_linker=&linker;
  assert(native_hierarchy_allocator::Service::Bind(hooks,*d.metadata,*d.roots,runtime,&allocation,&error));assert(allocation.SetOriginals({OriginalAllocate,OriginalFind,OriginalIds,Failure,Retire,OriginalLoad},&error));allocator_service=&allocation;root_service=d.roots;
  table.GetStaticMethodID=StaticMethod;table.CallStaticBooleanMethodV=Holds;assert(allocation.CalibrateRetirement(context,reinterpret_cast<jobject>(old_class),&error));
  auto*old_root=pool.Add(old_class,&error),*parent_root=pool.Add(base,&error),*cache_root=pool.Add(cache,&error);selected_parent=base;
  assert(allocation.ObserveOriginal(context,reinterpret_cast<jobject>(old_class),&actual_owner,old_root,parent_root,cache_root,0,&error)==new_class);assert(allocated==1);
  native_hierarchy_source_cache::AllocatorBatchAccess access(pool);SourceOwners source_owners(context,pool);native_hierarchy_source_cache::Mapper mapper(context,pool,*d.metadata,*d.storage,access,&source_owners);
  native_hierarchy_cache::Counts counts;assert(native_hierarchy_source_cache::Inspect(d.mapping,*d.schema,&counts,&error));uint32_t subject_index=UINT32_MAX;for(uint32_t i=0;i<counts.types;++i){const char*name=nullptr;assert(evolution::ReadMappedDexTypeDescriptor(d.mapping,i,&name,&error));if(subject_descriptor==name)subject_index=i;}assert(subject_index!=UINT32_MAX&&counts.fields&&counts.methods);
  auto populate=[&](void*raw_cache){auto*c=static_cast<type_cache::Dual*>(raw_cache);auto*t=static_cast<type_cache::Pair*>(heap.New(8192));t[subject_index%1024]={uint32_t(uintptr_t(old_class)),subject_index};c->types=uintptr_t(t);auto*full=static_cast<uint32_t*>(heap.New(counts.types*4));full[subject_index]=uint32_t(uintptr_t(old_class));c->types_array=uintptr_t(full);auto*f=static_cast<method_cache::Pair*>(heap.New(16384));f[0]={uintptr_t(old_fields[0]),0};c->fields=uintptr_t(f);auto*m=static_cast<method_cache::Pair*>(heap.New(16384));m[0]={uintptr_t(old_method),0};c->methods=uintptr_t(m);auto*fa=static_cast<std::atomic<uintptr_t>*>(heap.New(counts.fields*8));fa[0]=uintptr_t(old_fields[0]);c->fields_array=uintptr_t(fa);auto*ma=static_cast<std::atomic<uintptr_t>*>(heap.New(counts.methods*8));ma[0]=uintptr_t(old_method);c->methods_array=uintptr_t(ma);};populate(published_cache);populate(cache);
  native_hierarchy_cache::Owner cache_owner;auto*calibration=pool.Add(published_cache,&error);assert(cache_owner.Calibrate(mapper,calibration,112,counts,{subject_index,0,0,old_root,old_fields[0],old_method},&error));native_hierarchy_source_cache::Composite caches(mapper,cache_owner);native_hierarchy_receiver::Preservation receivers(*d.metadata);
  table.DeleteLocalRef=LocalDelete;table.GetMethodID=Method;table.CallObjectMethodV=Call;table.GetArrayLength=Length;table.GetObjectArrayElement=Element;table.NewStringUTF=String;table.CallStaticObjectMethodV=Lookup;
  jvmtiInterface_1 funcs{};funcs.GetLoadedClasses=Loaded;funcs.GetClassSignature=Signature;funcs.GetClassModifiers=Modifiers;funcs.GetClassStatus=Status;funcs.Deallocate=Deallocate;jvmtiEnv ti{&funcs};
  Input proposal;proposal.strong_original=reinterpret_cast<jclass>(old_class);proposal.after=*d.schema;proposal.before=proposal.after;proposal.before.superclass=pkg+"HierarchyBase;";
  native_hierarchy_source_cache::Mapper absent_mapper(context,pool,*d.metadata,*d.storage,access,nullptr);native_hierarchy_cache::Owner absent_cache;assert(absent_cache.Calibrate(absent_mapper,calibration,112,counts,{subject_index,0,0,old_root,old_fields[0],old_method},&error));native_hierarchy_source_cache::Composite absent_caches(absent_mapper,absent_cache);native_hierarchy_receiver::Preservation missing_receivers(*d.metadata);Authority rejected;Bindings missing{d.metadata,d.roots,Borrow,&allocation,&missing_receivers,&absent_caches};assert(!rejected.Stage(context,&ti,{proposal},missing,&error));
  Authority authority;Bindings bindings{d.metadata,d.roots,Borrow,&allocation,&receivers,&caches};assert(!access.Count());if(!authority.Stage(context,&ti,{proposal},bindings,&error)){std::cerr<<error<<"\n";std::abort();}assert(authority.CheckClass(context,proposal.strong_original,proposal.after));
  CheckHooks checks;assert(native_hierarchy_source_checks::Install(checks,&error));{native_hierarchy_source_checks::Scope scope(&actual_driver,context,*d.roots,authority,SourceRefuse);assert(source_check_proxy(&actual_owner));assert(actual_driver.result==113&&actual_owner.added_methods);auto*source_file=actual_owner.dex_file;actual_owner.dex_file=nullptr;assert(!source_check_proxy(&actual_owner)&&actual_driver.result==66);actual_owner.dex_file=source_file;actual_driver.result=113;}
  {native_hierarchy_allocator::Batch batch(allocation,context,authority,pool);assert(access.Attach(batch)&&!access.Attach(batch));assert(batch.Prepare(&holder,&error));selected_parent=inserted;
   auto*result=batch.Allocate(&actual_owner,old_root,cache_root,0,&error);if(!result)std::cerr<<error<<'\n';assert(result==relocated_new&&relocated_new&&allocated==2);native_hierarchy_allocator::Batch::RecordView record;assert(batch.At(0,&record));assert(!mapper.VerifyStaged(*authority.Staged(),{proposal},&error));assert(pool.Read(record.new_root,&result)&&result==relocated_new);
   auto*types=static_cast<type_cache::Dual*>(cache);assert(reinterpret_cast<type_cache::Pair*>(types->types)[subject_index%1024].object==uint32_t(uintptr_t(result)));assert(reinterpret_cast<uint32_t*>(types->types_array)[subject_index]==uint32_t(uintptr_t(result)));assert(!reinterpret_cast<method_cache::Pair*>(types->fields)[0].method&&!reinterpret_cast<method_cache::Pair*>(types->methods)[0].method);
   Prepared forbidden{record.source_owner,old_class,nullptr,cache,inserted,0,record.declarations,record.old_root};assert(!authority.AuthorizeAllocation(context,forbidden,&error));
   source_commit::Plan plan;plan.old_classes={old_class};plan.new_classes={result};
   auto*collected=heap.New(16);Put32(collected,0,uint32_t(uintptr_t(array_type)));Put32(collected,8,1);Put32(collected,12,uint32_t(uintptr_t(result)));plan.mirror=old_class;plan.new_classes_root=collected;plan.class_root_origins={{collected,0}};
   Put32(array,12+11*4,uint32_t(uintptr_t(collected)));
   Stop(nullptr,"actual source final witness",false);plan.class_root_origins[0].index=1;assert(!authority.Seal(plan,&error));plan.class_root_origins[0].index=0;assert(authority.Seal(plan,&error));forbid_allocation=true;assert(authority.Verify(plan,0));authority.BeginCommit(plan);assert(batch.Complete(true,&error));forbid_allocation=false;Resume(nullptr);
  }
  authority.Clear(context.env);assert(local_objects.empty());return true;
 };
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),run,&use)==runnable_jni::Result::kSuccess);Publish(nullptr);std::puts("actual modules expected graph -> first allocator -> private cache -> exact Collected graph PASS");
}
