#include "platform/common/native_source_dex_registry.h"
#include "platform/common/native_hierarchy_private_ledger.h"
#include "platform/common/native_source_class_loading.h"
#include "platform/common/native_hierarchy_session.h"
#include "source_cache_walker_fixture.inc"
#include "platform/common/native_hierarchy_graph.h"
#include "platform/common/native_hierarchy_allocator.h"
#include "platform/common/native_hierarchy_receiver.h"
#include "platform/common/native_hierarchy_source_checks.h"
#include "platform/common/native_class_lookup.h"
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
bool registry_wrong=false,registry_published=false;
bool RegistryRegistered(void*l,void*self,const void*f){assert(l==&linker&&self==thread.data());return registry_wrong?false:(registry_published||IsRegistered(f));}
abi::arm64::android11::ReleaseObjectPointer RegistryFind(void*l,void*self,const void*f){assert(l==&linker&&self==thread.data());return {uintptr_t(FindDexCache(f))};}
abi::arm64::android11::ReleaseObjectPointer Enter(void*self,abi::arm64::android11::ReleaseObjectPointer p,bool attempt){assert(self==thread.data()&&!held&&!attempt);held=true;held_object=reinterpret_cast<void*>(p.reference);return p;}
bool Exit(void*self,abi::arm64::android11::ReleaseObjectPointer p){assert(self==thread.data()&&held&&held_object==reinterpret_cast<void*>(p.reference));held=false;held_object=nullptr;return true;}
jmethodID StaticMethod(JNIEnv*,jclass,const char*n,const char*){return reinterpret_cast<jmethodID>(!std::strcmp(n,"holdsLock")?uintptr_t(&monitor_id):4);}
jboolean Holds(JNIEnv*,jclass,jmethodID id,va_list args){Jni();assert(id==reinterpret_cast<jmethodID>(&monitor_id));return held&&held_object==va_arg(args,jobject);}
class SourceHooks:public RootHooks {public:void*FindSymbol(const char*lib,const char*n)override{if(std::strstr(n,"Class13GetDescriptor"))return reinterpret_cast<void*>(SourceDescriptor);if(std::strstr(n,"MonitorEnter"))return reinterpret_cast<void*>(Enter);if(std::strstr(n,"MonitorExit"))return reinterpret_cast<void*>(Exit);return RootHooks::FindSymbol(lib,n);}};
abi::arm64::android11::ReleaseObjectPointer OriginalFind(void*linker_arg,void*self,const char*d,void*loader){assert(linker_arg==&linker&&self==thread.data()&&!Read32(loader));if(allocated==2&&!relocated_new&&subject_descriptor==d){relocated_new=heap.New(160);std::memcpy(relocated_new,new_class,160);MoveRoots mover;mover.old_value=uint32_t(uintptr_t(new_class));mover.next_value=uint32_t(uintptr_t(relocated_new));OriginalRoots(self,&mover,0);}
 for(const auto&c:public_classes)if(c.descriptor==d)return {uintptr_t(c.native)};return {0};}

const runnable_jni::Context*lookup_context=nullptr;const native_class_lookup::Service*lookup_service=nullptr;
abi::arm64::android11::ReleaseObjectPointer LookupBridge(void*l,void*self,const char*d,void*loader){assert(l==&linker&&lookup_context&&self==lookup_context->self&&lookup_service);return lookup_service->Find(*lookup_context,d,loader);}
abi::arm64::android11::ReleaseObjectPointer PlainFind(void*l,void*self,const char*d,const void*loader){return OriginalFind(l,self,d,const_cast<void*>(loader));}
bool lookup_wrong=false;uint32_t LookupHash(const char*d){assert(d&&*d);return 0xabcdefu;}
abi::arm64::android11::ReleaseObjectPointer HashedFind(void*l,void*self,const char*d,size_t hash,const void*loader){assert(hash==LookupHash(d));return lookup_wrong?abi::arm64::android11::ReleaseObjectPointer{uintptr_t(parent_class)}:OriginalFind(l,self,d,const_cast<void*>(loader));}
class LookupHooks final:public SourceHooks {public:bool hashed=false,missing_hash=false;
 void*FindSymbol(const char*lib,const char*n)override{
  if(std::strstr(n,"ClassLinker9FindClassEPNS_6ThreadEPKcNS_6Handle"))return hashed?nullptr:reinterpret_cast<void*>(PlainFind);
  if(std::strstr(n,"ClassLinker9FindClassEPNS_6ThreadEPKcmNS_6Handle"))return hashed?reinterpret_cast<void*>(HashedFind):nullptr;
  if(std::strstr(n,"ComputeModifiedUtf8HashEPKc"))return missing_hash?nullptr:reinterpret_cast<void*>(LookupHash);
  return SourceHooks::FindSymbol(lib,n);
 }
};
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
bool(*session_finish_proxy)(void*,transaction_root::Holder*,void*)=nullptr;
abi::arm64::android_common::HierarchyAllocator session_allocate_proxy=nullptr;
std::array<void*,4> pauser_vtable{};unsigned add_calls=0,remove_calls=0;
void(*class_add_proxy)(void*,void*)=nullptr;void(*class_remove_proxy)(void*,void*)=nullptr;
using Publication=void(*)(void*,abi::arm64::android11::ReleaseObjectPointer,abi::arm64::android11::ReleaseObjectPointer);
Publication publication_proxy=nullptr;unsigned publication_calls=0;
void OriginalPublication(void*,abi::arm64::android11::ReleaseObjectPointer,abi::arm64::android11::ReleaseObjectPointer){++publication_calls;}
bool inspect_remove=false,throw_remove=false,reenter_remove=false;
void OriginalClassAdd(void*,void*){++add_calls;}
void OriginalClassRemove(void*callbacks,void*pauser){
 ++remove_calls;
 if(inspect_remove){auto*loading=native_source_class_loading::Run::Current();assert(loading&&!loading->Paused());assert(!loading->Complete(true));}
 if(reenter_remove){reenter_remove=false;class_remove_proxy(callbacks,pauser);}
 if(throw_remove)throw 7;
}
struct PauserFixture{const void*table;void*self;uint8_t running;};
void*session_parent_handle=nullptr;
void* session_old_handle=nullptr;void*session_cache_handle=nullptr;void*session_allocated=nullptr;
bool SessionFinish(void*owner,transaction_root::Holder*,void*){session_allocated=reinterpret_cast<void*>(session_allocate_proxy(owner,session_old_handle,session_parent_handle,session_cache_handle,0).reference);return session_allocated!=nullptr;}
unsigned source_failure_notifications=0;
void NotifyFailure(void*driver,jvmtiError)noexcept{++source_failure_notifications;auto*s=native_hierarchy_session::Session::Current();if(s&&s->OwnsDriver(driver))s->Refund();}
unsigned untouched_finish_calls=0;
bool OriginalSessionFinish(void*owner,transaction_root::Holder*h,void*i){auto*s=native_hierarchy_session::Session::Current();if(!s||!s->OwnsClassOwner(owner)){if(native_hierarchy_session::CalibrationScope::Current())return SessionFinish(owner,h,i);++untouched_finish_calls;return true;}return SessionFinish(owner,h,i);}
class SessionHooks final:public SourceHooks {
 public:int fail=-1;size_t installs=0,removes=0;bool missing=false;
 void*FindSymbol(const char*lib,const char*n)override{
  if(!std::strcmp(n,"_ZN3art11ClassLinker19IsDexFileRegisteredEPNS_6ThreadERKNS_7DexFileE"))return reinterpret_cast<void*>(RegistryRegistered);
  if(!std::strcmp(n,"_ZN3art11ClassLinker12FindDexCacheEPNS_6ThreadERKNS_7DexFileE"))return reinterpret_cast<void*>(RegistryFind);
  if(!std::strcmp(n,"_ZN3art11ClassLinker24RegisterExistingDexCacheENS_6ObjPtrINS_6mirror8DexCacheEEENS1_INS2_11ClassLoaderEEE"))return reinterpret_cast<void*>(OriginalPublication);
  if(!std::strcmp(n,"_ZTVN12openjdkjvmti21ClassDefinitionPauserE"))return pauser_vtable.data();
  if(!std::strcmp(n,"_ZN3art16RuntimeCallbacks20AddClassLoadCallbackEPNS_17ClassLoadCallbackE"))return reinterpret_cast<void*>(OriginalClassAdd);
  if(!std::strcmp(n,"_ZN3art16RuntimeCallbacks23RemoveClassLoadCallbackEPNS_17ClassLoadCallbackE"))return reinterpret_cast<void*>(OriginalClassRemove);
  if(!std::strcmp(n,abi::arm64::android_common::kFinishSubtreeAllocations))return reinterpret_cast<void*>(OriginalSessionFinish);
  if(!std::strcmp(n,abi::arm64::android_common::kHierarchyAllocator))return reinterpret_cast<void*>(OriginalAllocate);
  if(!std::strcmp(n,abi::arm64::android_common::kHierarchyLoadClass))return reinterpret_cast<void*>(OriginalLoad);
  if(!std::strcmp(n,abi::arm64::android_common::kCaptureLinkedClass))return missing?nullptr:reinterpret_cast<void*>(OriginalIds);
  return SourceHooks::FindSymbol(lib,n);
 }
 void*Install(void*target,void*proxy,void**original,std::string*)override{
  assert(stopped==1);const size_t at=installs++;if(int(at)==fail)return nullptr;*original=target;
  if(target==reinterpret_cast<void*>(OriginalSessionFinish))session_finish_proxy=reinterpret_cast<decltype(session_finish_proxy)>(proxy);
  if(target==reinterpret_cast<void*>(OriginalPublication))publication_proxy=reinterpret_cast<Publication>(proxy);
  if(target==reinterpret_cast<void*>(OriginalClassAdd))class_add_proxy=reinterpret_cast<decltype(class_add_proxy)>(proxy);
  if(target==reinterpret_cast<void*>(OriginalClassRemove))class_remove_proxy=reinterpret_cast<decltype(class_remove_proxy)>(proxy);
  if(target==reinterpret_cast<void*>(OriginalAllocate))session_allocate_proxy=reinterpret_cast<decltype(session_allocate_proxy)>(proxy);
  return reinterpret_cast<void*>(at+1);
 }
 bool Remove(void*,std::string*)override{++removes;return true;}
};
class SourceOwners final:public native_hierarchy_source_cache::RequiredServices {
 const runnable_jni::Context&context_;source_handle_roots::Pool&pool_;
 public:SourceOwners(const runnable_jni::Context&c,source_handle_roots::Pool&p):context_(c),pool_(p){}
 bool VerifyStaged(const Graph&g,const std::vector<Input>&inputs)const noexcept override{return Current()==context_.self&&pool_.ReadyForSourceAccess()&&g.nodes.size()==public_classes.size()&&inputs.size()==1&&!stopped;}
 bool PreparePrivate(const native_hierarchy_source_cache::RecordView&r,const native_hierarchy_cache::Source&s,std::string*)const override{return VerifyPrivate(r,s,context_,pool_);}
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
  SourceHooks hooks;native_hierarchy_allocator::Service allocation;runtime_services::Snapshot runtime;runtime.receivers.class_linker=&linker;runtime.runtime=&linker;actual_driver.runtime=runtime.runtime;
  assert(native_hierarchy_allocator::Service::Bind(hooks,*d.metadata,*d.roots,runtime,&allocation,&error));assert(allocation.SetOriginals({OriginalAllocate,OriginalFind,OriginalIds,Failure,Retire,OriginalLoad},&error));allocator_service=&allocation;root_service=d.roots;
  table.GetStaticMethodID=StaticMethod;table.CallStaticBooleanMethodV=Holds;assert(allocation.CalibrateRetirement(context,reinterpret_cast<jobject>(old_class),&error));
  LookupHooks lookup_hooks;native_class_lookup::Service plain_lookup,hashed_lookup,unbound;assert(native_class_lookup::Service::Bind(lookup_hooks,runtime,&plain_lookup,&error));assert(!plain_lookup.Find(context,subject_descriptor.c_str(),array_root).reference);assert(plain_lookup.Calibrate(context,reinterpret_cast<jclass>(old_class),*d.metadata,*d.roots,&error));
  auto*loader=pool.Add(nullptr,&error);assert(plain_lookup.Find(context,subject_descriptor.c_str(),loader).reference==uintptr_t(old_class));uint32_t foreign_loader=0;assert(!plain_lookup.Find(context,subject_descriptor.c_str(),&foreign_loader).reference);
  lookup_hooks.hashed=true;lookup_hooks.missing_hash=true;assert(!native_class_lookup::Service::Bind(lookup_hooks,runtime,&unbound,&error));lookup_hooks.missing_hash=false;assert(native_class_lookup::Service::Bind(lookup_hooks,runtime,&hashed_lookup,&error));lookup_wrong=true;assert(!hashed_lookup.Calibrate(context,reinterpret_cast<jclass>(old_class),*d.metadata,*d.roots,&error));lookup_wrong=false;assert(hashed_lookup.Calibrate(context,reinterpret_cast<jclass>(old_class),*d.metadata,*d.roots,&error));assert(hashed_lookup.Find(context,subject_descriptor.c_str(),loader).reference==uintptr_t(old_class));lookup_context=&context;lookup_service=&hashed_lookup;assert(allocation.SetOriginals({OriginalAllocate,LookupBridge,OriginalIds,Failure,Retire,OriginalLoad},&error));SessionHooks session_hooks;auto original_functions=native_hierarchy_allocator::Functions{OriginalAllocate,LookupBridge,OriginalIds,Failure,Retire,OriginalLoad};session_hooks.missing=true;assert(!native_hierarchy_session::Install(session_hooks,allocation,original_functions,&error)&&!session_hooks.installs);session_hooks.missing=false;session_hooks.fail=1;Stop(nullptr,"test actual installation owner",false);assert(!native_hierarchy_session::Install(session_hooks,allocation,original_functions,&error)&&session_hooks.removes==1);session_hooks.fail=-1;assert(native_hierarchy_session::Install(session_hooks,allocation,original_functions,&error));native_source_class_loading::Service class_loading;assert(native_source_class_loading::Service::Bind(session_hooks,runtime,&class_loading,&error));assert(native_source_class_loading::Install(session_hooks,class_loading,&error));assert(native_hierarchy_private_ledger::Install(session_hooks,&linker,&error));Resume(nullptr);assert(session_finish_proxy(&actual_owner,&holder,nullptr)&&untouched_finish_calls==1);

  auto*old_root=pool.Add(old_class,&error),*parent_root=pool.Add(base,&error),*cache_root=pool.Add(cache,&error);selected_parent=base;
  {native_hierarchy_session::CalibrationScope observation(&actual_driver,context,reinterpret_cast<jobject>(old_class),*d.roots,allocation);native_source_class_loading::Run loading(class_loading,&actual_driver,context);assert(native_hierarchy_session::CalibrationScope::Current()==&observation);PauserFixture pauser{pauser_vtable.data()+2,context.self,1};class_add_proxy(&linker,&pauser);assert(!loading.Paused()&&!class_loading.ObservedOriginal());session_old_handle=old_root;session_parent_handle=parent_root;session_cache_handle=cache_root;transaction_root::Iterator iter{0,&holder};assert(session_finish_proxy(&actual_owner,&holder,&iter)&&loading.Paused());class_remove_proxy(&linker,&pauser);assert(!loading.Paused());std::memset(&pauser,0,sizeof(pauser));assert(loading.Complete(true)&&class_loading.ObservedOriginal());}assert(!native_hierarchy_session::CalibrationScope::Current()&&allocated==1);
  native_source_dex_registry::Service registry;assert(native_source_dex_registry::Service::Bind(session_hooks,runtime,&registry,&error));registry_wrong=true;assert(!registry.Calibrate(context,reinterpret_cast<jobject>(old_class),*d.metadata,*d.roots,&error));registry_wrong=false;assert(registry.Calibrate(context,reinterpret_cast<jobject>(old_class),*d.metadata,*d.roots,&error));assert(registry.Unregistered(context,pool,cache_root,incoming_file,&error));registry_published=true;assert(!registry.Unregistered(context,pool,cache_root,incoming_file,&error));registry_published=false;assert(!registry.Unregistered(context,pool,cache_root,original_file,&error));
  native_hierarchy_source_cache::AllocatorBatchAccess access(pool);SourceOwners source_owners(context,pool);native_hierarchy_source_cache::Mapper mapper(context,pool,*d.metadata,*d.storage,access,&source_owners);
  native_hierarchy_cache::Counts counts;assert(native_hierarchy_source_cache::Inspect(d.mapping,*d.schema,&counts,&error));uint32_t subject_index=UINT32_MAX;for(uint32_t i=0;i<counts.types;++i){const char*name=nullptr;assert(evolution::ReadMappedDexTypeDescriptor(d.mapping,i,&name,&error));if(subject_descriptor==name)subject_index=i;}assert(subject_index!=UINT32_MAX&&counts.fields&&counts.methods);
  auto populate=[&](void*raw_cache){auto*c=static_cast<type_cache::Dual*>(raw_cache);auto*t=static_cast<type_cache::Pair*>(heap.New(8192));t[subject_index%1024]={uint32_t(uintptr_t(old_class)),subject_index};c->types=uintptr_t(t);auto*full=static_cast<uint32_t*>(heap.New(counts.types*4));full[subject_index]=uint32_t(uintptr_t(old_class));c->types_array=uintptr_t(full);auto*f=static_cast<method_cache::Pair*>(heap.New(16384));f[0]={uintptr_t(old_fields[0]),0};c->fields=uintptr_t(f);auto*m=static_cast<method_cache::Pair*>(heap.New(16384));m[0]={uintptr_t(old_method),0};c->methods=uintptr_t(m);auto*fa=static_cast<std::atomic<uintptr_t>*>(heap.New(counts.fields*8));fa[0]=uintptr_t(old_fields[0]);c->fields_array=uintptr_t(fa);auto*ma=static_cast<std::atomic<uintptr_t>*>(heap.New(counts.methods*8));ma[0]=uintptr_t(old_method);c->methods_array=uintptr_t(ma);};populate(published_cache);populate(cache);
  native_hierarchy_cache::Owner cache_owner;auto*calibration=pool.Add(published_cache,&error);assert(cache_owner.Calibrate(mapper,calibration,112,counts,{subject_index,0,0,old_root,old_fields[0],old_method},&error));native_hierarchy_source_cache::Composite caches(mapper,cache_owner);native_hierarchy_receiver::Preservation receivers(*d.metadata);
  table.DeleteLocalRef=LocalDelete;table.GetMethodID=Method;table.CallObjectMethodV=Call;table.GetArrayLength=Length;table.GetObjectArrayElement=Element;table.NewStringUTF=String;table.CallStaticObjectMethodV=Lookup;
  jvmtiInterface_1 funcs{};funcs.GetLoadedClasses=Loaded;funcs.GetClassSignature=Signature;funcs.GetClassModifiers=Modifiers;funcs.GetClassStatus=Status;funcs.Deallocate=Deallocate;jvmtiEnv ti{&funcs};
  Input proposal;proposal.strong_original=reinterpret_cast<jclass>(old_class);proposal.after=*d.schema;proposal.before=proposal.after;proposal.before.superclass=pkg+"HierarchyBase;";
  native_hierarchy_source_cache::Mapper absent_mapper(context,pool,*d.metadata,*d.storage,access,nullptr);native_hierarchy_cache::Owner absent_cache;assert(absent_cache.Calibrate(absent_mapper,calibration,112,counts,{subject_index,0,0,old_root,old_fields[0],old_method},&error));native_hierarchy_source_cache::Composite absent_caches(absent_mapper,absent_cache);native_hierarchy_receiver::Preservation missing_receivers(*d.metadata);Authority rejected;Bindings missing{d.metadata,d.roots,Borrow,&allocation,&missing_receivers,&absent_caches};assert(!rejected.Stage(context,&ti,{proposal},missing,&error));
  class FinalFixture final:public native_hierarchy_private_ledger::CleanupOwner {public:bool final=false;mutable unsigned commits=0;bool Verify(void*d,const transaction_root::Holder*h,const source_commit::Plan&p)const noexcept override{return d==&actual_driver&&h==actual_holder&&stopped==1&&!p.old_classes.empty();}bool IsStopped(void*d,const transaction_root::Holder*h)const noexcept override{return final&&d==&actual_driver&&h==actual_holder&&stopped==1;}bool IsFinalWindow(void*d,const transaction_root::Holder*h)const noexcept override{return final&&d==&actual_driver&&h==actual_holder&&stopped==1;}void BeginCommit(const source_commit::Plan&)const noexcept override{++commits;}} final_fixture;
  Authority authority;Bindings bindings{d.metadata,d.roots,Borrow,&allocation,&receivers,&caches};assert(!access.Count());if(!authority.Stage(context,&ti,{proposal},bindings,&error)){std::cerr<<error<<"\n";std::abort();}assert(authority.CheckClass(context,proposal.strong_original,proposal.after));
  CheckHooks checks;assert(native_hierarchy_source_checks::Install(checks,&error));assert(!native_hierarchy_source_checks::Ready(NotifyFailure));native_hierarchy_source_checks::SetFailureObserver(NotifyFailure);assert(native_hierarchy_source_checks::Ready(NotifyFailure));{native_hierarchy_source_checks::Scope scope(&actual_driver,context,*d.roots,authority,SourceRefuse);assert(source_check_proxy(&actual_owner));assert(actual_driver.result==113&&actual_owner.added_methods&&!source_failure_notifications);auto*source_file=actual_owner.dex_file;actual_owner.dex_file=nullptr;assert(!source_check_proxy(&actual_owner)&&actual_driver.result==66);actual_owner.dex_file=source_file;actual_driver.result=113;}
  native_hierarchy_source_checks::SetFailureObserver(nullptr);{native_hierarchy_session::Session refused({&actual_driver,context,&hooks,d.metadata,d.roots,&pool,&authority,&allocation,&access,SourceRefuse,NotifyFailure});assert(!refused.Ready()&&!native_hierarchy_session::Session::Current());}native_hierarchy_source_checks::SetFailureObserver(NotifyFailure);
  {native_source_class_loading::Run loading(class_loading,&actual_driver,context);PauserFixture pauser{pauser_vtable.data()+2,context.self,1};class_add_proxy(&linker,&pauser);assert(!loading.Paused());native_hierarchy_session::Session session({&actual_driver,context,&hooks,d.metadata,d.roots,&pool,&authority,&allocation,&access,SourceRefuse,NotifyFailure});assert(session.Ready()&&native_hierarchy_session::Session::Current()==&session);transaction_root::Iterator iter{0,&holder};assert(!session.BeforeVerification(&actual_owner,&iter,&error));selected_parent=inserted;session_old_handle=old_root;session_cache_handle=cache_root;
   auto foreign_owner=actual_owner;Driver foreign_driver=actual_driver;foreign_owner.driver=&foreign_driver;assert(session_finish_proxy(&foreign_owner,&holder,&iter)&&untouched_finish_calls==2);assert(session_finish_proxy(&actual_owner,&holder,&iter));auto*result=session_allocated;assert(result==relocated_new&&relocated_new&&allocated==2);native_hierarchy_source_cache::RecordView record;assert(access.At(0,&record));assert(session.BeforeVerification(&actual_owner,&iter,&error));session.AfterVerification(&actual_owner,&iter,true);assert(!mapper.VerifyStaged(*authority.Staged(),{proposal},&error));assert(pool.Read(record.new_root,&result)&&result==relocated_new);
   auto*types=static_cast<type_cache::Dual*>(cache);assert(reinterpret_cast<type_cache::Pair*>(types->types)[subject_index%1024].object==uint32_t(uintptr_t(result)));assert(reinterpret_cast<uint32_t*>(types->types_array)[subject_index]==uint32_t(uintptr_t(result)));assert(!reinterpret_cast<method_cache::Pair*>(types->fields)[0].method&&!reinterpret_cast<method_cache::Pair*>(types->methods)[0].method);
   Prepared forbidden{record.source_owner,old_class,nullptr,cache,inserted,0,record.declarations,record.old_root};assert(!authority.AuthorizeAllocation(context,forbidden,&error));
   source_commit::Plan plan;plan.old_classes={old_class};plan.new_classes={result};
   auto*collected=heap.New(16);Put32(collected,0,uint32_t(uintptr_t(array_type)));Put32(collected,8,1);Put32(collected,12,uint32_t(uintptr_t(result)));plan.mirror=old_class;plan.new_classes_root=collected;plan.class_root_origins={{collected,0}};
   Put32(array,12+11*4,uint32_t(uintptr_t(collected)));
   Stop(nullptr,"actual source final witness",false);plan.class_root_origins[0].index=1;assert(!session.Collected(plan,&error));plan.class_root_origins[0].index=0;assert(session.Collected(plan,&error));forbid_allocation=true;assert(authority.Verify(plan,0));session.BeginCommit(plan);forbid_allocation=false;Resume(nullptr);class_remove_proxy(&linker,&pauser);assert(loading.Complete(true));forbid_allocation=true;assert(session.ReleaseAfterRun(true));assert(!native_hierarchy_session::Session::Current()&&!access.Count());forbid_allocation=false;
  }
  authority.Clear(context.env);
  for(int failure_case=0;failure_case<5;++failure_case){
   native_hierarchy_source_cache::AllocatorBatchAccess failure_access(pool);native_hierarchy_source_cache::Mapper failure_mapper(context,pool,*d.metadata,*d.storage,failure_access,&source_owners);
   native_hierarchy_cache::Owner failure_cache;assert(failure_cache.Calibrate(failure_mapper,calibration,112,counts,{subject_index,0,0,old_root,old_fields[0],old_method},&error));native_hierarchy_source_cache::Composite failure_caches(failure_mapper,failure_cache);native_hierarchy_receiver::Preservation failure_receivers(*d.metadata);Authority failure_authority;Bindings failure_bindings{d.metadata,d.roots,Borrow,&allocation,&failure_receivers,&failure_caches};
   // Restore the source's preallocation root partition; each run has a fresh Holder.
   Put32(array,12+11*4,0);Put32(array,12+12*4,0);assert(failure_authority.Stage(context,&ti,{proposal},failure_bindings,&error));
   native_hierarchy_session::Session failure({&actual_driver,context,&hooks,d.metadata,d.roots,&pool,&failure_authority,&allocation,&failure_access,SourceRefuse,NotifyFailure});assert(failure.Ready());transaction_root::Iterator it{0,&holder};
   if(failure_case==0){assert(!failure.Finish(&actual_owner,&holder,&it,+[](void*,transaction_root::Holder*,void*){return false;},&error));}
   else if(failure_case==1){assert(!failure.Finish(&actual_owner,&holder,&it,+[](void*,transaction_root::Holder*,void*)->bool{throw std::bad_alloc();},&error));assert(actual_driver.result==JVMTI_ERROR_OUT_OF_MEMORY);actual_driver.result=113;}
   else if(failure_case==2){transaction_root::Holder foreign=holder;transaction_root::Iterator wrong{0,&foreign};assert(!failure.Finish(&actual_owner,&holder,&wrong,SessionFinish,&error));assert(actual_driver.result==JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED);actual_driver.result=113;}
   else if(failure_case==3){assert(!failure.Finish(&actual_owner,&holder,&it,+[](void*owner,transaction_root::Holder*h,void*i){assert(SessionFinish(owner,h,i));source_failure_proxy(&actual_driver,JVMTI_ERROR_FAILS_VERIFICATION,nullptr,nullptr);assert(Read32(new_class,offsetof(class_metadata::Class17,status))>>28==15);return false;},&error));assert(actual_driver.result==JVMTI_ERROR_FAILS_VERIFICATION);assert(Read32(new_class,offsetof(class_metadata::Class17,status))>>28==1);}
   else {assert(failure.Finish(&actual_owner,&holder,&it,SessionFinish,&error));assert(failure.BeforeVerification(&actual_owner,&it,&error));source_failure_proxy(&actual_driver,JVMTI_ERROR_FAILS_VERIFICATION,nullptr,nullptr);assert(Read32(new_class,offsetof(class_metadata::Class17,status))>>28==15);failure.AfterVerification(&actual_owner,&it,false);assert(actual_driver.result==JVMTI_ERROR_FAILS_VERIFICATION);assert(Read32(new_class,offsetof(class_metadata::Class17,status))>>28==1);}
   failure.Refund();assert(!failure.Ready());assert(failure.ReleaseAfterRun(false));assert(!failure_access.Count()&&!native_hierarchy_session::Session::Current());failure_authority.Clear(context.env);actual_driver.result=113;
  }
  for(int paused_case=0;paused_case<4;++paused_case){
   native_source_class_loading::Run loading(class_loading,&actual_driver,context);PauserFixture p{pauser_vtable.data()+2,context.self,1};
   if(paused_case==0)p.table=pauser_vtable.data()+1;
   if(paused_case==1)p.self=thread.data()+1;
   class_add_proxy(&linker,&p);assert(!loading.Paused());
   transaction_root::Iterator it{0,&holder};
   if(paused_case<2){assert(!loading.BeforeFinish(&actual_owner,&holder,&it));}
   else if(paused_case==2){it.index=1;assert(!loading.BeforeFinish(&actual_owner,&holder,&it));}
   // AddCallback without the first real Finish never proves the barrier wait.
   class_remove_proxy(&linker,&p);assert(!loading.Paused());assert(!loading.Complete(true));
  }
  // The original callback must return before a pause lifecycle can complete.
  for(int mode=0;mode<3;++mode){
   const bool unwind=mode==1;
   native_source_class_loading::Run loading(class_loading,&actual_driver,context);
   PauserFixture p{pauser_vtable.data()+2,context.self,1};class_add_proxy(&linker,&p);
   transaction_root::Iterator it{0,&holder};assert(session_finish_proxy(&actual_owner,&holder,&it)&&loading.Paused());
   inspect_remove=true;throw_remove=unwind;reenter_remove=mode==2;unsigned calls=remove_calls;
   try{class_remove_proxy(&linker,&p);assert(!unwind);}catch(int value){assert(unwind&&value==7);}
   inspect_remove=false;throw_remove=false;assert(remove_calls==calls+(mode==2?2:1)&&!loading.Paused());
   std::memset(&p,0,sizeof(p));assert(loading.Complete(true)==(mode==0));
  }
  // Exercise the real named publication proxy and frozen ledger independently
  // from the boot-class fixture above. Native source/API leaves are doubles;
  // token/Holder/root/phase policies below are the production Ledger TU.
  {
   class Records final:public native_hierarchy_source_cache::BatchAccess{public:bool enabled=false;native_hierarchy_source_cache::RecordView record{};size_t Count()const noexcept override{return enabled?1:0;}bool At(size_t i,native_hierarchy_source_cache::RecordView*out)const noexcept override{if(!enabled||i||!out)return false;*out=record;return true;}void*Find(const char*,void*)const noexcept override{return nullptr;}void*OriginalFind(const char*,void*)const override{return nullptr;}} records;
   native_hierarchy_private_ledger::Ledger published_ledger(&actual_driver,context,pool,records,*d.metadata,registry,&final_fixture);
   Graph expected;expected.inputs={0};expected.nodes.resize(1);assert(published_ledger.VerifyStaged(expected,{proposal}));
   native_source_class_loading::Run loading(class_loading,&actual_driver,context);PauserFixture p{pauser_vtable.data()+2,context.self,1};class_add_proxy(&linker,&p);transaction_root::Iterator it{0,&holder};assert(session_finish_proxy(&actual_owner,&holder,&it));source_allocation_transaction::Snapshot snapshot;assert(source_allocation_transaction::Read(hooks,*d.metadata,&holder,pool,&snapshot,&error));assert(published_ledger.AttachHolder(&holder,snapshot.metadata));
   void*actual_loader=heap.New(32);const uint32_t saved_loader=Read32(reinterpret_cast<unsigned char*>(old_class)+offsetof(class_metadata::Class11,class_loader));
   auto*empty=pool.Add(nullptr,&error);records.record={&actual_owner,0,old_root,empty,cache_root,parent_root,d.schema,d.mapping};records.enabled=true;
   native_hierarchy_cache::Source source{cache_root,incoming_file,&records,112,counts};assert(!published_ledger.PreparePrivate(records.record,source,&error));Put32(old_class,offsetof(class_metadata::Class11,class_loader),uint32_t(uintptr_t(actual_loader)));registry_published=true;assert(!published_ledger.PreparePrivate(records.record,source,&error));registry_published=false;assert(published_ledger.PreparePrivate(records.record,source,&error));assert(!published_ledger.PreparePrivate(records.record,source,&error));assert(published_ledger.VerifyPrivate(records.record,source,context,pool));assert(pool.Write(empty,relocated_new));source_commit::Plan exact;exact.old_classes={old_class};exact.new_classes={relocated_new};assert(published_ledger.Collected(exact));
   publication_proxy(&linker,{uintptr_t(published_cache)},{uintptr_t(actual_loader)}); // unrelated registration stays original.
   publication_proxy(&linker,{uintptr_t(cache)},{uintptr_t(actual_loader)});assert(publication_calls==2);assert(!published_ledger.VerifyPrivate(records.record,source,context,pool));
   std::vector<void*>roots;prepared_roots::Registrar registrar{&roots,[](void*o,void*v,size_t*n){auto&r=*static_cast<std::vector<void*>*>(o);*n=r.size();r.push_back(v);return true;}};assert(published_ledger.CollectManagedRoots(registrar));prepared_roots::Resolver resolver{&roots,[](void*o,size_t n)noexcept{const auto&r=*static_cast<std::vector<void*>*>(o);return n<r.size()?r[n]:nullptr;}};
   auto*moved=heap.New(160);std::memcpy(moved,relocated_new,160);for(auto&v:roots)if(v==relocated_new)v=moved;
   Stop(nullptr,"actual ledger final witness",false);final_fixture.final=true;Put32(context.self,0,92u<<24);assert(!pool.ReadyForSourceAccess());forbid_allocation=true;published_ledger.Refresh(resolver);void*read=nullptr;assert(published_ledger.ReadStoppedRoot(empty,&read)&&read==moved);assert(!published_ledger.VerifyCommit(expected,exact));exact.new_classes[0]=moved;assert(published_ledger.VerifyCommit(expected,exact));published_ledger.BeginCommit(expected,exact);assert(final_fixture.commits==1);forbid_allocation=false;Put32(context.self,0,0);final_fixture.final=false;Resume(nullptr);
   Put32(old_class,offsetof(class_metadata::Class11,class_loader),saved_loader);class_remove_proxy(&linker,&p);assert(loading.Complete(true));std::memset(&holder,0,sizeof(holder));assert(published_ledger.ReleaseAfterRun(true)); // no dead Holder reads.
   records.enabled=false;native_hierarchy_private_ledger::Ledger missing_cleanup(&actual_driver,context,pool,records,*d.metadata,registry,nullptr);assert(!missing_cleanup.VerifyStaged(expected,{proposal}));assert(!missing_cleanup.FinalRootWindow());assert(missing_cleanup.ReleaseAfterRun(false));
  }
  assert(local_objects.empty());return true;
 };
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),run,&use)==runnable_jni::Result::kSuccess);Publish(nullptr);std::puts("actual modules expected graph -> first allocator -> private cache -> exact Collected graph PASS");
}
