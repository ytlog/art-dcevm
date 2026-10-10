#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "provider_fixture.inc"
#include "roots_fixture.inc"
#pragma clang diagnostic pop
#include "platform/common/source_cache_public_roles.h"
namespace {
namespace roles=source_cache_public_roles;
struct PublicMember {void* native;void* declaring;std::string name,descriptor;uint32_t flags;Mirror mirror;};
std::vector<PublicMember> public_fields,public_methods;
std::map<std::string,void*> public_types;
std::map<jobject,std::string> strings;
size_t globals_created=0,globals_deleted=0,ti_allocations=0,ti_deletions=0;bool public_pending=false,bad_native=false,bad_owner=false,wrong_primitive=false,wrong_for_name=false;
int for_name,primitive_type;void* loader_object=nullptr;void* primitive_i=nullptr;void* primitive_v=nullptr;
jobject PGlobal(JNIEnv*,jobject p){Jni();if(p)++globals_created;return p;}
void PDeleteGlobal(JNIEnv*,jobject p){Jni();if(p)++globals_deleted;}
jboolean PException(JNIEnv*){Jni();return public_pending;}
jclass PFind(JNIEnv* e,const char*name){if(!std::strcmp(name,"java/lang/Integer"))return reinterpret_cast<jclass>(primitive_i);if(!std::strcmp(name,"java/lang/Void"))return reinterpret_cast<jclass>(primitive_v);return Find(e,name);}
jstring PString(JNIEnv*,const char*s){Jni();auto*token=new int(0);auto object=reinterpret_cast<jobject>(token);strings.emplace(object,s);return reinterpret_cast<jstring>(object);}
void PDeleteLocal(JNIEnv*,jobject p){Jni();auto i=strings.find(p);if(i!=strings.end()){delete reinterpret_cast<int*>(p);strings.erase(i);}}
jmethodID PStaticMethod(JNIEnv* e,jclass k,const char*n,const char*d){Jni();assert(std::strcmp(n,"<clinit>")!=0);if(!std::strcmp(n,"forName"))return reinterpret_cast<jmethodID>(&for_name);for(auto&m:public_methods)if(m.name==n&&m.descriptor==d&&(k==m.declaring||k==public_types["LChild;"])&&(m.flags&8))return reinterpret_cast<jmethodID>(m.native);return StaticMethodId(e,k,n,d);}
jmethodID PMethod(JNIEnv* e,jclass k,const char*n,const char*d){Jni();for(auto&m:public_methods)if(m.name==n&&m.descriptor==d&&(k==m.declaring||k==public_types["LChild;"])&&!(m.flags&8))return reinterpret_cast<jmethodID>(m.native);return MethodId(e,k,n,d);}
jobject PStaticCall(JNIEnv*,jclass,jmethodID id,va_list args){Jni();assert(id==reinterpret_cast<jmethodID>(&for_name));auto text=va_arg(args,jstring);auto initialize=va_arg(args,int);auto loader=va_arg(args,jobject);assert(!initialize&&loader==loader_object);auto name=strings.at(text);std::replace(name.begin(),name.end(),'.','/');if(name.front()!='[')name="L"+name+";";auto i=public_types.find(name);return i==public_types.end()?nullptr:reinterpret_cast<jobject>(wrong_for_name?parent_class:i->second);}
jfieldID PField(JNIEnv*,jclass k,const char*n,const char*d){Jni();for(auto&f:public_fields)if(f.name==n&&f.descriptor==d&&(k==f.declaring||k==public_types["LChild;"]))return reinterpret_cast<jfieldID>(f.native);return nullptr;}
jfieldID PStaticField(JNIEnv* e,jclass k,const char*n,const char*d){if(!std::strcmp(n,"TYPE")){Jni();return reinterpret_cast<jfieldID>(&primitive_type);}return PField(e,k,n,d);}
jobject PStaticObject(JNIEnv*,jclass k,jfieldID id){Jni();assert(id==reinterpret_cast<jfieldID>(&primitive_type));return wrong_primitive?reinterpret_cast<jobject>(parent_class):k;}
jclass PSuper(JNIEnv*,jclass k){Jni();if(k==public_types["LChild;"])return reinterpret_cast<jclass>(old_class);return nullptr;}
jobject PReflectField(JNIEnv*,jclass k,jfieldID id,jboolean is_static){Jni();for(auto&f:public_fields)if(f.native==id){assert((bad_owner||k==f.declaring)&&bool(is_static)==bool(f.flags&8));return reinterpret_cast<jobject>(&f.mirror);}return nullptr;}
jobject PReflectMethod(JNIEnv*,jclass k,jmethodID id,jboolean is_static){Jni();for(auto&m:public_methods)if(m.native==id){assert((bad_owner||k==m.declaring)&&bool(is_static)==bool(m.flags&8));assert(m.name!="<clinit>");return reinterpret_cast<jobject>(&m.mirror);}return nullptr;}
void* PReflectedMethod(const abi::common::RunnableJniScope&,jobject object){assert(!stopped&&Read32(current)==0);return reinterpret_cast<Mirror*>(object)->field;}
void* PCodecMethod(void*,jmethodID id){return bad_native?next_method:reinterpret_cast<void*>(id);}
void* PCodecField(void*,jfieldID id){return bad_native?new_fields[0]:reinterpret_cast<void*>(id);}
char* TiString(const std::string&s){++ti_allocations;auto*p=static_cast<char*>(std::malloc(s.size()+1));std::memcpy(p,s.c_str(),s.size()+1);return p;}
template<class T>T* TiArray(size_t n){if(!n)return nullptr;++ti_allocations;return static_cast<T*>(std::malloc(n*sizeof(T)));}
jvmtiError TiDelete(jvmtiEnv*,unsigned char*p){if(p){++ti_deletions;std::free(p);}return JVMTI_ERROR_NONE;}
jvmtiError TiLoader(jvmtiEnv*,jclass,jobject*out){Jni();*out=reinterpret_cast<jobject>(loader_object);return JVMTI_ERROR_NONE;}
jvmtiError TiInterfaces(jvmtiEnv*,jclass,jint*n,jclass**out){Jni();*n=0;*out=nullptr;return JVMTI_ERROR_NONE;}
jvmtiError TiMethods(jvmtiEnv*,jclass k,jint*n,jmethodID**out){Jni();*n=k==reinterpret_cast<jclass>(old_class)?jint(public_methods.size()):0;*out=TiArray<jmethodID>(*n);for(int i=0;i<*n;++i)(*out)[i]=reinterpret_cast<jmethodID>(public_methods[i].native);return JVMTI_ERROR_NONE;}
jvmtiError TiFields(jvmtiEnv*,jclass k,jint*n,jfieldID**out){Jni();*n=k==reinterpret_cast<jclass>(old_class)?jint(public_fields.size()):0;*out=TiArray<jfieldID>(*n);for(int i=0;i<*n;++i)(*out)[i]=reinterpret_cast<jfieldID>(public_fields[i].native);return JVMTI_ERROR_NONE;}
jvmtiError TiMethodName(jvmtiEnv*,jmethodID id,char**n,char**d,char**generic){Jni();if(generic)*generic=nullptr;for(auto&m:public_methods)if(m.native==id){*n=TiString(m.name);*d=TiString(m.descriptor);return JVMTI_ERROR_NONE;}return JVMTI_ERROR_INVALID_METHODID;}
jvmtiError TiFieldName(jvmtiEnv*,jclass,jfieldID id,char**n,char**d,char**generic){Jni();if(generic)*generic=nullptr;for(auto&f:public_fields)if(f.native==id){*n=TiString(f.name);*d=TiString(f.descriptor);return JVMTI_ERROR_NONE;}return JVMTI_ERROR_INVALID_FIELDID;}
jvmtiError TiMethodFlags(jvmtiEnv*,jmethodID id,jint*out){Jni();for(auto&m:public_methods)if(m.native==id){*out=jint(m.flags);return JVMTI_ERROR_NONE;}return JVMTI_ERROR_INVALID_METHODID;}
jvmtiError TiFieldFlags(jvmtiEnv*,jclass,jfieldID id,jint*out){Jni();for(auto&f:public_fields)if(f.native==id){*out=jint(f.flags);return JVMTI_ERROR_NONE;}return JVMTI_ERROR_INVALID_FIELDID;}
jvmtiError TiMethodOwner(jvmtiEnv*,jmethodID id,jclass*out){Jni();for(auto&m:public_methods)if(m.native==id){*out=reinterpret_cast<jclass>(bad_owner?parent_class:m.declaring);return JVMTI_ERROR_NONE;}return JVMTI_ERROR_INVALID_METHODID;}
jvmtiError TiFieldOwner(jvmtiEnv*,jclass,jfieldID id,jclass*out){Jni();for(auto&f:public_fields)if(f.native==id){*out=reinterpret_cast<jclass>(bad_owner?parent_class:f.declaring);return JVMTI_ERROR_NONE;}return JVMTI_ERROR_INVALID_FIELDID;}
jvmtiError TiSignature(jvmtiEnv*,jclass k,char**out,char**generic){Jni();if(generic)*generic=nullptr;for(const auto&entry:public_types)if(entry.second==k){*out=TiString(entry.first);return JVMTI_ERROR_NONE;}return JVMTI_ERROR_INVALID_CLASS;}
const char* PublicDescriptor(void*k,void*storage){for(const auto&entry:public_types)if(entry.second==k)return entry.first.c_str();return ResolutionDescriptor(k,storage);}
class PublicHooks final:public Hooks {public:void* FindSymbol(const char*lib,const char*n)override {if(std::strstr(n,"Class13GetDescriptor"))return reinterpret_cast<void*>(PublicDescriptor);if(std::strstr(n,"GetMirrorClass"))return reinterpret_cast<void*>(AllocationMirror);if(std::strstr(n,"HandleScopeVisitRoots"))return reinterpret_cast<void*>(OriginalRoots);if(std::strstr(n,"AllocateNewClassObject"))return reinterpret_cast<void*>(OriginalRoots);return Hooks::FindSymbol(lib,n);}};
void InstallPublicJni(jvmtiInterface_1&ti){table.NewGlobalRef=PGlobal;table.DeleteGlobalRef=PDeleteGlobal;table.DeleteLocalRef=PDeleteLocal;table.ExceptionCheck=PException;table.FindClass=PFind;table.NewStringUTF=PString;table.GetStaticMethodID=PStaticMethod;table.GetMethodID=PMethod;table.CallStaticObjectMethodV=PStaticCall;table.GetFieldID=PField;table.GetStaticFieldID=PStaticField;table.GetStaticObjectField=PStaticObject;table.GetSuperclass=PSuper;table.ToReflectedField=PReflectField;table.ToReflectedMethod=PReflectMethod;
 ti.GetClassSignature=TiSignature;ti.Deallocate=TiDelete;ti.GetClassLoader=TiLoader;ti.GetImplementedInterfaces=TiInterfaces;ti.GetClassMethods=TiMethods;ti.GetClassFields=TiFields;ti.GetMethodName=TiMethodName;ti.GetFieldName=TiFieldName;ti.GetMethodModifiers=TiMethodFlags;ti.GetFieldModifiers=TiFieldFlags;ti.GetMethodDeclaringClass=TiMethodOwner;ti.GetFieldDeclaringClass=TiFieldOwner;}
struct FinalMapper: native_hierarchy_cache::SourceMapper,source_handle_roots::StoppedReader {
 source_handle_roots::Pool&pool;modern_field_metadata::ReadContext context;bool active=true,deny=false;uintptr_t heap_begin=uintptr_t(heap.base),heap_end=uintptr_t(heap.base)+heap.used,dex_begin=0,dex_end=0;
 explicit FinalMapper(source_handle_roots::Pool&p):pool(p),context{this,[](void*raw,const void*address,size_t*out){auto&s=*static_cast<FinalMapper*>(raw);if(!s.active||s.deny||!stopped)return false;auto p=uintptr_t(address);if(p>=s.heap_begin&&p<s.heap_end){*out=s.heap_end-p;return true;}if(p>=s.dex_begin&&p<s.dex_end){*out=s.dex_end-p;return true;}return false;}}{}
 bool FinalRootWindow()const noexcept override{return active&&stopped&&current==thread.data();}
 bool ReadStoppedRoot(const void*slot,void**out)const noexcept override{return pool.ReadStopped(*this,slot,out);}
 void* Get(void*root)const noexcept override{void*out=nullptr;return pool.ReadStopped(*this,root,&out)?out:nullptr;}
 bool Readable(const void*p,size_t n)const noexcept override{size_t extent=0;return modern_field_metadata::ReadMappingExtent(&context,p,&extent)&&extent>=n;}
 const modern_field_metadata::ReadContext*CacheReadContext()const noexcept override{return active?&context:nullptr;}
 bool VerifyStaged(const native_hierarchy_graph::Graph&,const std::vector<native_hierarchy_graph::Input>&,std::string*)const override{return false;}
 bool Describe(const native_hierarchy_graph::Prepared&,native_hierarchy_cache::Source*,std::string*)const override{return false;}
 bool FindSource(const native_hierarchy_graph::Prepared&,uint32_t,void**,std::string*)const override{return false;}
 bool OwnsUnpublished(const native_hierarchy_graph::Prepared&,const native_hierarchy_cache::Source&)const noexcept override{return false;}
 bool Barrier(void*)const noexcept override{return false;}
 bool RefreshPrepared(native_hierarchy_graph::Prepared*)const noexcept override{return false;}
 bool VerifyFrozen(const native_hierarchy_graph::Graph&,const native_hierarchy_graph::Prepared&,const native_hierarchy_cache::Source&)const noexcept override{return false;}
 bool VerifyCommit(const native_hierarchy_graph::Graph&,const source_commit::Plan&)const noexcept override{return false;}
 void BeginCommit(const native_hierarchy_graph::Graph&,const source_commit::Plan&)const noexcept override{}
};
}
int main(int argc,char**argv){
 assert(argc==2);InitJni();InitializeHeap();PublicHooks hooks;std::string error;
 modern_field_metadata::FieldMetadata metadata;assert(metadata.Bind(hooks,&error));std::array<modern_field_metadata::FieldOracle,4> oracle{};for(size_t i=0;i<4;++i)oracle[i]={reinterpret_cast<jobject>(&mirrors[i]),names[i],types[i],i==0?8u:1u};
 struct Cal{modern_field_metadata::FieldMetadata*metadata;decltype(oracle)*oracles;std::string*error;}cal{&metadata,&oracle,&error};auto calibrate=[](const runnable_jni::Context&c,void*raw){auto&r=*static_cast<Cal*>(raw);return r.metadata->Calibrate(c,reinterpret_cast<jobject>(old_class),"LFixture;",r.oracles->data(),4,r.error);};if(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),calibrate,&cal)!=runnable_jni::Result::kSuccess){std::cerr<<"calibration: "<<error<<"\n";return 1;}
 modern_field_storage::Service field_storage;assert(field_storage.Calibrate(hooks,&env,reinterpret_cast<jobject>(old_class),reinterpret_cast<jobject>(old_instance),metadata,oracle.data(),4,&error));
 void*self=thread.data();JNIEnv*source_env=&env;std::memcpy(thread.data()+216,&self,8);std::memcpy(thread.data()+200,&source_env,8);
 EnvironmentOracle independent_env{{&table},self,&runtime};table.GetJavaVM=PairVM;
 source_handle_roots::Service roots;assert(source_handle_roots::Service::Bind(hooks,field_storage,runnable_jni::Context{self,&independent_env.env,&runtime},&roots,&error));
 alignas(8) unsigned char original_scope[32]{};OriginalHeader header{nullptr,3};std::memcpy(original_scope,&header,sizeof(header));if(sized_scope)Put32(original_scope,12,2);Put32(original_scope,OriginalReferences(),uint32_t(uintptr_t(old_class)));public_slot=original_scope+OriginalReferences();Publish(original_scope);
 struct RootCal{source_handle_roots::Service*roots;std::string*error;}root_cal{&roots,&error};auto root_observe=[](const runnable_jni::Context&c,void*raw){auto&r=*static_cast<RootCal*>(raw);return r.roots->ObserveAllocatorHandle(c,reinterpret_cast<jobject>(old_class),public_slot,r.error);};assert(runnable_executor::With(&independent_env.env,reinterpret_cast<jobject>(old_class),root_observe,&root_cal)==runnable_jni::Result::kSuccess);
 auto dex=File(argv[1]);LinkDex(old_class,dex);old_class->dex_class_def_index=0;old_class->dex_type_index=2;loader_object=heap.New(16);old_class->class_loader=uint32_t(uintptr_t(loader_object));
 source_cache_role_catalog::Catalog catalog;assert(source_cache_role_catalog::Read(dex.data(),dex.size(),&catalog,&error));
 primitive_i=heap.New(160);primitive_v=heap.New(160);public_types={{"I",primitive_i},{"V",primitive_v},{"LFixture;",old_class},{"LChild;",new_class},{"Ljava/lang/Object;",parent_class},{"[I",heap.New(160)}};
 // Public declarations are independent of the cache; external inherited IDs
 // resolve onto the same actual declaring-class members and native indexes.
 for(size_t i=0;i<catalog.fields.size();++i){const auto&r=catalog.fields[i];if(!r.declared)continue;size_t physical=0;while(names[physical]!=r.name)++physical;auto*n=old_fields[physical];n->dex_field_index=uint32_t(i);public_fields.push_back({n,old_class,r.name,r.descriptor,r.access,{n}});}
 for(size_t i=0;i<catalog.methods.size();++i){const auto&r=catalog.methods[i];if(!r.declared)continue;auto*n=new(heap.New(sizeof(abi::arm64::android12::Method12)))abi::arm64::android12::Method12{};n->declaring_class=uint32_t(uintptr_t(old_class));n->dex_method_index=uint32_t(i);n->access_flags=r.access;public_methods.push_back({n,old_class,r.name,r.descriptor,r.access,{n}});}
 Put32(public_types["[I"],offsetof(class_metadata::Class17,component_type),uint32_t(uintptr_t(primitive_i)));
 jvmtiInterface_1 ti_table{};jvmtiEnv ti{&ti_table};InstallPublicJni(ti_table);
 roles::Bindings binding{&metadata,PCodecMethod,PCodecField,PReflectedMethod,FieldLeaf,&metadata,method_metadata::kAndroid12Onward};auto cache=reinterpret_cast<jobject>(Root(&old_class->dex_cache));roles::Public prepared;
 auto prepare=[&](){return prepared.Prepare(&env,&ti,catalog,dex,reinterpret_cast<jclass>(old_class),cache,binding,&error);};
 if(!prepare()){std::cerr<<"prepare: "<<error<<"\n";return 1;}
 assert(prepared.Ready()&&prepared.Types().size()==6&&prepared.Fields().size()==5&&prepared.Methods().size()==4);assert(prepared.Fields()[0].declaring==reinterpret_cast<jclass>(old_class)&&prepared.Methods()[0].declaring==reinterpret_cast<jclass>(old_class));assert(prepared.Methods()[1].name=="<clinit>"&&!prepared.Methods()[1].reflected);prepared.Clear(&env);assert(globals_created==globals_deleted&&ti_allocations==ti_deletions&&strings.empty());
 wrong_primitive=true;assert(!prepare()&&!prepared.Ready());wrong_primitive=false;wrong_for_name=true;assert(!prepare()&&!prepared.Ready());wrong_for_name=false;bad_native=true;assert(!prepare()&&!prepared.Ready());bad_native=false;bad_owner=true;assert(!prepare()&&!prepared.Ready());bad_owner=false;public_pending=true;assert(!prepare());public_pending=false;auto saved=dex[100];dex[100]^=1;assert(!prepare());dex[100]=saved;
 auto wrong_original=dex;wrong_original[200]^=1;assert(!prepared.Prepare(&env,&ti,catalog,wrong_original,reinterpret_cast<jclass>(old_class),cache,binding,&error));
 auto original_component=Read32(public_types["[I"],offsetof(class_metadata::Class17,component_type));Put32(public_types["[I"],offsetof(class_metadata::Class17,component_type),uint32_t(uintptr_t(primitive_v)));assert(!prepare());Put32(public_types["[I"],offsetof(class_metadata::Class17,component_type),original_component);
 auto malformed=catalog;malformed.types[0]="";assert(!prepared.Prepare(&env,&ti,malformed,dex,reinterpret_cast<jclass>(old_class),cache,binding,&error));assert(globals_created==globals_deleted&&ti_allocations==ti_deletions&&strings.empty());assert(prepare());
 Put32(thread.data(),0,0);const runnable_jni::Context context{self,&independent_env.env,&runtime};{
 source_handle_roots::Pool rejected_pool(roots);roles::Rooted rejected;auto wrong_context=context;wrong_context.vm=&linker;assert(!rejected.Import(prepared,wrong_context,roots,rejected_pool,&error));assert(!rejected.Import(prepared,context,roots,rejected_pool,&error));
 source_handle_roots::Pool pool(roots);roles::Rooted rooted;assert(rooted.Import(prepared,context,roots,pool,&error));assert(!rooted.Import(prepared,context,roots,pool,&error));void*cache_root=pool.Add(cache,&error);assert(cache_root);FinalMapper mapper(pool);mapper.dex_begin=uintptr_t(dex.data());mapper.dex_end=mapper.dex_begin+dex.size();native_hierarchy_cache::Counts counts{6,5,4};
 stopped=1;const int before_jni=jni_calls;const auto before_os=method_identity::MappingOsQueries();forbid_provider_allocation=true;
 assert(rooted.Cache(mapper,cache_root,counts));assert(!rejected.Cache(mapper,cache_root,counts)&&!rejected.Type(mapper,0,primitive_i)&&!rejected.Field(mapper,0,prepared.Fields()[0].native)&&!rejected.Method(mapper,0,prepared.Methods()[0].native));assert(!rooted.Cache(mapper,cache_root,{7,5,4}));for(uint32_t i=0;i<6;++i){assert(rooted.Type(mapper,i,prepared.Types()[i].klass));assert(!rooted.Type(mapper,i,loader_object));}assert(!rooted.Type(mapper,6,old_class));
 for(uint32_t i=0;i<5;++i){assert(rooted.Field(mapper,i,prepared.Fields()[i].native));assert(!rooted.Field(mapper,i,next_method));}for(uint32_t i=0;i<4;++i){assert(rooted.Method(mapper,i,prepared.Methods()[i].native));assert(!rooted.Method(mapper,i,old_fields[0]));}mapper.deny=true;assert(!rooted.Cache(mapper,cache_root,counts)&&!rooted.Field(mapper,0,prepared.Fields()[0].native));mapper.deny=false;mapper.active=false;assert(!rooted.Type(mapper,0,prepared.Types()[0].klass));mapper.active=true;
 forbid_provider_allocation=false;stopped=0;assert(jni_calls==before_jni&&method_identity::MappingOsQueries()==before_os);
 // Emulate actual moving collector by modifying every source-owned slot and
 // every native declaring-class reference, rather than stale public handles.
 auto* moved=static_cast<class_metadata::Class17*>(heap.New(160));std::memcpy(moved,old_class,160);mapper.heap_end=uintptr_t(heap.base)+heap.used;MoveRoots move;move.old_value=uint32_t(uintptr_t(old_class));move.next_value=uint32_t(uintptr_t(moved));OriginalRoots(self,&move,0);void*live_cache=heap.New(sizeof(abi::arm64::android11::LegacyCache));std::memcpy(live_cache,cache,sizeof(abi::arm64::android11::LegacyCache));move.old_value=uint32_t(uintptr_t(cache));move.next_value=uint32_t(uintptr_t(live_cache));OriginalRoots(self,&move,0);moved->dex_cache=move.next_value;void*live_loader=heap.New(16);std::memcpy(live_loader,loader_object,16);move.old_value=uint32_t(uintptr_t(loader_object));move.next_value=uint32_t(uintptr_t(live_loader));OriginalRoots(self,&move,0);moved->class_loader=move.next_value;move.next_value=uint32_t(uintptr_t(moved));mapper.heap_end=uintptr_t(heap.base)+heap.used;for(auto&m:public_methods)Put32(m.native,0,move.next_value);for(auto&f:public_fields)Put32(f.native,0,move.next_value);
 stopped=1;forbid_provider_allocation=true;assert(rooted.Cache(mapper,cache_root,counts));assert(rooted.Type(mapper,2,moved)&&!rooted.Type(mapper,2,old_class));for(uint32_t i=0;i<5;++i)assert(rooted.Field(mapper,i,prepared.Fields()[i].native));for(uint32_t i=0;i<4;++i)assert(rooted.Method(mapper,i,prepared.Methods()[i].native));
 auto*n=static_cast<abi::arm64::android12::Method12*>(prepared.Methods()[0].native);++n->dex_method_index;assert(!rooted.Method(mapper,0,n));--n->dex_method_index;n->access_flags^=8;assert(!rooted.Method(mapper,0,n));n->access_flags^=8;auto*f=static_cast<abi::arm64::Field*>(prepared.Fields()[0].native);++f->dex_field_index;assert(!rooted.Field(mapper,0,f));--f->dex_field_index;f->declaring_class=uint32_t(uintptr_t(parent_class));assert(!rooted.Field(mapper,0,f));f->declaring_class=move.next_value;
 auto*cached=static_cast<unsigned char*>(live_cache);uint64_t original_file=0;std::memcpy(&original_file,cached+offsetof(abi::arm64::android11::LegacyCache,dex_file),8);uint64_t wrong_file=0;std::memcpy(cached+offsetof(abi::arm64::android11::LegacyCache,dex_file),&wrong_file,8);assert(!rooted.Cache(mapper,cache_root,counts)&&!rooted.Field(mapper,0,f)&&!rooted.Method(mapper,0,n));std::memcpy(cached+offsetof(abi::arm64::android11::LegacyCache,dex_file),&original_file,8);
 const auto loader=moved->class_loader;moved->class_loader=0;assert(!rooted.Cache(mapper,cache_root,counts));moved->class_loader=loader;
 forbid_provider_allocation=false;stopped=0;assert(jni_calls==before_jni&&method_identity::MappingOsQueries()==before_os);assert(pool.DropManagedReferences());stopped=1;assert(!rooted.Type(mapper,2,moved));stopped=0;
 }
 assert(Top()==original_scope);Publish(nullptr);Put32(thread.data(),0,92u<<24);prepared.Clear(&env);assert(globals_created==globals_deleted&&ti_allocations==ti_deletions&&strings.empty());
 std::cout<<"Complete public cache roles: real metadata, inherited/static/clinit/primitive/array IDs; actual source Pool import, GC-moved Class/cache/loader current roles; malformed identity, ownership, flags/index/count, mapping denial and cleanup refusal; stopped callbacks no JNI, OS mapping queries or C++ allocation\n";
}
