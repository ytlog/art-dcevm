#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "native_commit_fixture.inc"
#pragma clang diagnostic pop
#include "platform/common/native_member_provider.h"
#include "platform/common/native_field_factory_observer.cc"
#include "platform/common/dex_mapping_abi.h"
#include "platform/abi/arm64/android11/dex_cache.h"
#include "platform/common/native_reflective_value_abi.h"
#include <fstream>
#include <unistd.h>
#include <iterator>
#include "platform/common/native_interface_graph.h"
// Finalization must still run when every native C++ allocation fails.
#include <new>
bool forbid_provider_allocation=false;
void* operator new(size_t n){if(forbid_provider_allocation)std::abort();if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
namespace {
namespace factory_alias=native_field_factory_observer;
factory_alias::Return OrdinaryFactory(void*,void*field,bool){return {uintptr_t(field)};}
struct OrdinaryAliasHooks:HookEngine {
 bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char*,const char*name)override{return !std::strcmp(name,"_ZN3art6mirror5Field18CreateFromArtFieldEPNS_6ThreadEPNS_8ArtFieldEb")?reinterpret_cast<void*>(OrdinaryFactory):nullptr;}
 void* Install(void*,void*,void**original,std::string*)override{*original=reinterpret_cast<void*>(OrdinaryFactory);return this;}
 bool Remove(void*token,std::string*)override{return token==this;}
};
struct OrdinaryAliasOwner {
 factory_alias::Service observer;const modern_field_metadata::FieldOracle* oracles=nullptr;std::array<void*,4> native{};
 static bool Root(void*,void*,const void*,void**){return false;}
 static bool Normalize(void*,void*,factory_alias::HandleCarrier,void*input,bool,void**out)noexcept{*out=input;return true;}
 static bool Ready(void*raw,void*field,void*current_field,void*target){auto&o=*static_cast<OrdinaryAliasOwner*>(raw);
  if(!o.observer.NormalizerInstalled()||!target||field!=current_field)return false;
  // Independent real public Field calibration tuple, not a static_ready flag.
  for(size_t i=0;i<4;++i)if((o.oracles[i].modifiers&8u)&&o.native[i]==current_field)return true;
  return false;
 }
};
struct ExplicitMappings {
 struct Range {uintptr_t begin,end;};std::vector<Range> ranges;
 const void* denied=nullptr;bool reject_all=false;size_t calls=0;
 void Cover(const void* address){size_t extent=0;assert(method_identity::ReadableMappingExtent(address,&extent));
  const auto p=uintptr_t(address);const auto page=uintptr_t(sysconf(_SC_PAGESIZE));
  assert(page&&extent<=UINTPTR_MAX-p);ranges.push_back({p-(p%page),p+extent});
 }
 static bool Extent(void* state,const void* address,size_t* out){auto& s=*static_cast<ExplicitMappings*>(state);++s.calls;
  if(!out||!stopped||current!=thread.data()||s.reject_all||address==s.denied)return false;
  const auto p=uintptr_t(address);for(const auto& r:s.ranges)if(p>=r.begin&&p<r.end){*out=r.end-p;return true;}
  return false;
 }
};
int retired_methods=0,retired_fields=0;bool mh_guard=false,vh_guard=false,visit_mh=false,visit_vh=false,field_mh_guard=false,visit_field_mh=false;
void RetireMethod(void*,void*){++retired_methods;}void RetireField(void*,void*){++retired_fields;}
bool NotRetired(void*,void*){return false;}bool Mh(void*){return mh_guard;}bool Vh(void*){return vh_guard;}bool FieldMh(void*){return field_mh_guard;}
bool SameIndex(void* a,void* b){return static_cast<abi::arm64::android12::Method12*>(a)->dex_method_index==static_cast<abi::arm64::android12::Method12*>(b)->dex_method_index;}
abi::arm64::Field* ancestor_cached_field=nullptr;art::ArtField* resolved_field_slot=nullptr;art::ArtMethod* resolved_method_slot=nullptr;bool visit_resolution=false;
const char* ResolutionName(void* field){return field==ancestor_cached_field?names[0]:FieldName(field);}
bool evolved_fields=false;
const char* ResolutionType(void* field){if(evolved_fields&&field==new_fields[0])return "J";return field==ancestor_cached_field?types[0]:FieldType(field);}
// Independent public transitive closure: ArrayList -> DirectA -> SuperB,
// plus inherited Parent -> InheritedC. Native IfTable pairs are separate arrays.
void* public_array_class=nullptr;void* public_list=nullptr;void* public_parent=nullptr;
void* interface_a=nullptr;void* interface_b=nullptr;void* interface_c=nullptr;
int interfaces_id;
std::unordered_map<void*,void*> direct_arrays,public_parents;
void* Array(const std::vector<void*>& values){void* array=heap.New(12+values.size()*4);Put32(array,0,uint32_t(uintptr_t(public_array_class)));Put32(array,8,uint32_t(values.size()));for(size_t i=0;i<values.size();++i)Put32(array,12+4*i,uint32_t(uintptr_t(values[i])));return array;}
void* IfTable(const std::vector<void*>& values){std::vector<void*> pairs;for(auto v:values){pairs.push_back(v);pairs.push_back(nullptr);}return Array(pairs);}
jclass InterfaceFind(JNIEnv* e,const char* name){if(!std::strcmp(name,"java/util/ArrayList")){Jni();return reinterpret_cast<jclass>(public_list);}return Find(e,name);}
jmethodID InterfaceMethod(JNIEnv* e,jclass owner,const char* name,const char* signature){if(!std::strcmp(name,"getInterfaces")){Jni();return reinterpret_cast<jmethodID>(&interfaces_id);}return MethodId(e,owner,name,signature);}
jobject InterfaceObject(JNIEnv* e,jobject o,jmethodID id,va_list args){
 if(id==reinterpret_cast<jmethodID>(&interfaces_id)){Jni();return reinterpret_cast<jobject>(direct_arrays.at(o));}
 if(id==reinterpret_cast<jmethodID>(&superclass_id)){Jni();auto i=public_parents.find(o);return i==public_parents.end()?nullptr:reinterpret_cast<jobject>(i->second);}
 return ObjectMethod(e,o,id,args);
}
jsize InterfaceLength(JNIEnv*,jarray array){Jni();return jsize(Read32(array,8));}
jobject InterfaceElement(JNIEnv*,jobjectArray array,jsize index){Jni();assert(index>=0&&index<jsize(Read32(array,8)));return reinterpret_cast<jobject>(uintptr_t(Read32(array,12+size_t(index)*4)));}
void InterfaceFixtures(){
 public_array_class=heap.New(sizeof(class_metadata::Class17));auto* array_class=static_cast<class_metadata::Class17*>(public_array_class);array_class->component_type=uint32_t(uintptr_t(parent_class));
 public_list=heap.New(sizeof(class_metadata::Class17));public_parent=heap.New(sizeof(class_metadata::Class17));
 interface_a=heap.New(sizeof(class_metadata::Class17));interface_b=heap.New(sizeof(class_metadata::Class17));interface_c=heap.New(sizeof(class_metadata::Class17));
 for(auto v:{public_array_class,public_list,public_parent,interface_a,interface_b,interface_c})static_cast<class_metadata::Class17*>(v)->object.klass=uint32_t(uintptr_t(meta_class));
 for(auto v:{interface_a,interface_b,interface_c})static_cast<class_metadata::Class17*>(v)->access_flags=0x200;
 void* empty=Array({});
 direct_arrays[public_list]=Array({interface_a});direct_arrays[public_parent]=Array({interface_c});direct_arrays[interface_a]=Array({interface_b});
 for(auto v:{interface_b,interface_c,parent_class,static_cast<void*>(old_class),static_cast<void*>(new_class)})direct_arrays[v]=empty;
 public_parents[public_list]=public_parent;public_parents[public_parent]=parent_class;public_parents[old_class]=parent_class;public_parents[new_class]=parent_class;
 for(auto v:{public_list,public_parent,interface_a,interface_b,interface_c,parent_class,static_cast<void*>(old_class),static_cast<void*>(new_class)})static_cast<class_metadata::Class17*>(v)->iftable=uint32_t(uintptr_t(IfTable({})));
 static_cast<class_metadata::Class17*>(public_list)->iftable=uint32_t(uintptr_t(IfTable({interface_a,interface_b,interface_c})));
 static_cast<class_metadata::Class17*>(public_parent)->iftable=uint32_t(uintptr_t(IfTable({interface_c})));
 table.FindClass=InterfaceFind;table.GetMethodID=InterfaceMethod;table.CallObjectMethodV=InterfaceObject;table.GetArrayLength=InterfaceLength;table.GetObjectArrayElement=InterfaceElement;
}
// ExceptionCheck and reference deletion remain legal with a pending exception.
// Every other JNI boundary asserts that calibration stopped at the first throw.
bool graph_pending=false;const char* graph_throw=nullptr;unsigned graph_injections=0;
void GraphJni(){assert(!graph_pending&&"JNI continued after graph calibration exception");}
void GraphThrow(const char* operation){if(graph_throw&&!std::strcmp(graph_throw,operation)){graph_pending=true;++graph_injections;}}
jboolean GraphException(JNIEnv*){return graph_pending;}
jclass GraphFind(JNIEnv* e,const char* name){GraphJni();auto result=InterfaceFind(e,name);GraphThrow(name);return result;}
jmethodID GraphMethod(JNIEnv* e,jclass owner,const char* name,const char* signature){GraphJni();auto result=InterfaceMethod(e,owner,name,signature);GraphThrow(name);return result;}
jobject GraphObject(JNIEnv* e,jobject object,jmethodID id,va_list args){GraphJni();auto result=InterfaceObject(e,object,id,args);GraphThrow(id==reinterpret_cast<jmethodID>(&interfaces_id)?"interfaces":"superclass");return result;}
jsize GraphLength(JNIEnv* e,jarray array){GraphJni();auto result=InterfaceLength(e,array);GraphThrow("length");return result;}
jobject GraphElement(JNIEnv* e,jobjectArray array,jsize index){GraphJni();auto result=InterfaceElement(e,array,index);GraphThrow("element");return result;}
jobject GraphGlobal(JNIEnv* e,jobject object){GraphJni();auto result=Global(e,object);GraphThrow(object==public_list?"positive-global":object==public_parent?"parent-global":"interface-global");return result;}
jboolean GraphSame(JNIEnv* e,jobject a,jobject b){GraphJni();return SameObject(e,a,b);}
void CheckGraphExceptionStops(const modern_field_metadata::FieldMetadata& metadata){
 const auto saved=table;
 table.ExceptionCheck=GraphException;table.FindClass=GraphFind;table.GetMethodID=GraphMethod;table.CallObjectMethodV=GraphObject;
 table.GetArrayLength=GraphLength;table.GetObjectArrayElement=GraphElement;table.NewGlobalRef=GraphGlobal;table.IsSameObject=GraphSame;
 runnable_jni::Context context{current,&env,nullptr};std::string error;
 for(auto operation:{"java/lang/Class","getInterfaces","getSuperclass","java/util/ArrayList","positive-global","interfaces","length","element","interface-global","superclass","parent-global"}){
  graph_throw=operation;graph_injections=0;graph_pending=false;
  assert(!native_interface_graph::Calibrate(context,reinterpret_cast<jobject>(old_class),metadata,&error));
  assert(graph_pending&&graph_injections==1&&!error.empty());
 }
 graph_pending=false;graph_throw=nullptr;table=saved;
}
void* forbidden_descriptor_class=nullptr;int forbidden_descriptor_calls=0;
const char* ResolutionDescriptor(void* klass,void* storage){if(klass==forbidden_descriptor_class){++forbidden_descriptor_calls;assert(false&&"Unrelated cache Class descriptor is not an eligibility prerequisite");}return Descriptor(klass,storage);}
std::array<executable_mirror::Declaration*,3> executable{};
void* Leaf(const abi::common::RunnableJniScope&,jobject object){return reinterpret_cast<void*>(uintptr_t(reinterpret_cast<executable_mirror::Declaration*>(object)->art_method));}
bool DecodeMirror(const runnable_jni::Context&,jobject object,void** out){*out=object;return true;}
void Visit(void* receiver,art::ReflectiveValueVisitor* visitor){
 assert(receiver==&runtime&&stopped);
 alignas(8) unsigned char info[24]{};
 if(visit_resolution){Put32(info,8,8);resolved_method_slot=visitor->VisitMethod(resolved_method_slot,*reinterpret_cast<const art::ReflectionSourceInfo*>(info));Put32(info,8,9);resolved_field_slot=visitor->VisitField(resolved_field_slot,*reinterpret_cast<const art::ReflectionSourceInfo*>(info));}
 if(visit_mh){Put32(info,8,3);assert(visitor->VisitMethod(reinterpret_cast<art::ArtMethod*>(old_method),*reinterpret_cast<const art::ReflectionSourceInfo*>(info))==reinterpret_cast<art::ArtMethod*>(old_method));}
 if(visit_field_mh){Put32(info,8,3);assert(visitor->VisitField(reinterpret_cast<art::ArtField*>(old_fields[3]),*reinterpret_cast<const art::ReflectionSourceInfo*>(info))==reinterpret_cast<art::ArtField*>(old_fields[3]));}
 if(visit_vh){Put32(info,8,4);assert(visitor->VisitField(reinterpret_cast<art::ArtField*>(old_fields[3]),*reinterpret_cast<const art::ReflectionSourceInfo*>(info))==reinterpret_cast<art::ArtField*>(old_fields[3]));}
}
struct RegisteredRoots{
 std::vector<void*> values;
 static bool Add(void* raw,void* value,size_t* slot){auto& self=*static_cast<RegisteredRoots*>(raw);auto found=std::find(self.values.begin(),self.values.end(),value);if(found==self.values.end()){*slot=self.values.size();self.values.push_back(value);}else *slot=size_t(found-self.values.begin());return true;}
 static void* Get(void* raw,size_t slot)noexcept{return static_cast<RegisteredRoots*>(raw)->values[slot];}
 void Move(void* before,void* after){for(auto& value:values)if(value==before)value=after;}
};
void SchemaObsolete(void* receiver,void* method){
 assert(stopped&&receiver==&linker);bool actual=false;
 auto* methods=reinterpret_cast<unsigned char*>(uintptr_t(old_class->methods));
 for(uint32_t n=0;n<Read32(methods);++n)actual|=method==methods+8+size_t(n)*32;
 assert(actual);events.push_back("obsolete");
}
abi::arm64::android11::ReleaseObjectPointer NoObsoleteHistory(void*){return {0};}
class ProviderHooks:public Hooks {public:void* FindSymbol(const char* lib,const char* name)override{
 if(std::strstr(name,"GetObsoleteClass"))return reinterpret_cast<void*>(NoObsoleteHistory);
 if(std::strstr(name,"SetEntryPointsForObsoleteMethod"))return reinterpret_cast<void*>(SchemaObsolete);
 if(std::strstr(name,"Class10Implements")||std::strstr(name,"Class16IsAssignableFrom"))return nullptr;
 if(std::strstr(name,"Class13GetDescriptor"))return reinterpret_cast<void*>(ResolutionDescriptor);
 if(std::strstr(name,"ArtField7GetNameEv"))return reinterpret_cast<void*>(ResolutionName);
 if(std::strstr(name,"ArtField17GetTypeDescriptorEv"))return reinterpret_cast<void*>(ResolutionType);
 if(std::strstr(name,"VisitReflectiveTargets"))return reinterpret_cast<void*>(Visit);
 if(std::strstr(name,"HasSameNameAndSignature"))return reinterpret_cast<void*>(SameIndex);
 return Hooks::FindSymbol(lib,name);
}};
std::vector<unsigned char> File(const char* path){std::ifstream in(path,std::ios::binary);assert(in.good());return {std::istreambuf_iterator<char>(in),{}};}
void LinkDex(class_metadata::Class17* klass,std::vector<unsigned char>& bytes){
 auto* prefix=new(heap.New(sizeof(dex_mapping_abi::Prefix)))dex_mapping_abi::Prefix{nullptr,bytes.data(),bytes.size(),bytes.data(),bytes.size()};
 auto* cache=heap.New(sizeof(abi::arm64::android11::LegacyCache));const uint64_t link=uintptr_t(prefix);
 std::memcpy(static_cast<char*>(cache)+offsetof(abi::arm64::android11::LegacyCache,dex_file),&link,8);klass->dex_cache=uint32_t(uintptr_t(cache));
}
void Methods(class_metadata::Class17* klass,bool deletion){
 auto* array=static_cast<unsigned char*>(heap.New(8+3*32));Put32(array,0,deletion?2:3);klass->methods=uintptr_t(array);
 for(size_t n=0;n<(deletion?2u:3u);++n){const uint32_t index=deletion?uint32_t(n+1):uint32_t(n);auto* m=reinterpret_cast<abi::arm64::android12::Method12*>(array+8+32*n);
  m->declaring_class=uint32_t(uintptr_t(klass));m->dex_method_index=index;m->access_flags=index==0?0x101:index==1?1:9;m->data=index==0?uintptr_t(&binding_value):0;
 }
 if(klass==old_class)old_method=reinterpret_cast<abi::arm64::android12::Method12*>(array+8);
}
}
#include "native_receiver_class_source_cases.h"
int main(int argc,char** argv){
 assert(argc==8);InitJni();InitializeHeap();InterfaceFixtures();
 const uint32_t field_indexes[]{1,3,0,2};for(size_t i=0;i<4;++i){old_fields[i]->dex_field_index=field_indexes[i];new_fields[i]->dex_field_index=field_indexes[i];}
 ProviderHooks hooks;std::string error;
 std::array<modern_field_metadata::FieldOracle,4> oracle{};for(size_t i=0;i<4;++i)oracle[i]={reinterpret_cast<jobject>(&mirrors[i]),names[i],types[i],i==0?8u:1u};
 modern_field_metadata::FieldMetadata metadata;assert(metadata.Bind(hooks,&error));
 struct Cal{modern_field_metadata::FieldMetadata* metadata;const modern_field_metadata::FieldOracle* oracle;std::string* error;} cal{&metadata,oracle.data(),&error};
 auto calibrate=[](const runnable_jni::Context& ctx,void* raw){auto& c=*static_cast<Cal*>(raw);return c.metadata->Calibrate(ctx,reinterpret_cast<jobject>(old_class),"LFixture;",c.oracle,4,c.error);};
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),calibrate,&cal)==runnable_jni::Result::kSuccess);
 size_t thread_extent=0,card_extent=0;assert(method_identity::ReadableMappingExtent(thread.data(),&thread_extent)&&thread_extent>=thread.size());assert(method_identity::ReadableMappingExtent(cards,&card_extent));
 assert(metadata.InterfaceDeclarationCalibrated());
 CheckGraphExceptionStops(metadata);
 CheckActualClassSource();
 std::vector<void*> observed_interfaces;assert(native_interface_graph::Read(metadata,public_list,&observed_interfaces,&error)&&observed_interfaces.size()==3);
 const uint32_t positive_table=static_cast<class_metadata::Class17*>(public_list)->iftable;
 modern_field_metadata::ReadContext interface_interval{&heap,[](void*raw,const void*p,size_t*out){auto&h=*static_cast<Heap*>(raw);const auto begin=uintptr_t(h.base),at=uintptr_t(p);if(!out||at<begin||at>=begin+h.used)return false;*out=begin+h.used-at;return true;}};
 forbid_provider_allocation=true;
 native_interface_graph::View interface_view;assert(native_interface_graph::ReadView(metadata,public_list,&interface_view,nullptr,&interface_interval)&&interface_view.Count()==3);
 for(size_t i=0;i<3;++i){void*selected=nullptr;assert(native_interface_graph::InterfaceAt(metadata,interface_view,i,&selected,&interface_interval)&&selected==observed_interfaces[i]);}
 forbid_provider_allocation=false;
 for(size_t i=0;i<3;++i){void*selected=nullptr;assert(native_interface_graph::InterfaceAt(metadata,interface_view,i,&selected)&&selected==observed_interfaces[i]);}
 void*selected=reinterpret_cast<void*>(uintptr_t(0x1234));assert(!native_interface_graph::InterfaceAt(metadata,interface_view,3,&selected)&&selected==reinterpret_cast<void*>(uintptr_t(0x1234)));
 modern_field_metadata::ReadContext no_view{&metadata,[](void*,const void*,size_t*){return false;}};
 assert(!native_interface_graph::InterfaceAt(metadata,interface_view,0,&selected,&no_view)&&selected==reinterpret_cast<void*>(uintptr_t(0x1234)));
 native_interface_graph::View failed_view=interface_view;assert(!native_interface_graph::ReadView(metadata,public_list,&failed_view,&error,&no_view)&&failed_view.Count()==3);
 auto*live_table=reinterpret_cast<void*>(uintptr_t(positive_table));uint32_t table_class=0;std::memcpy(&table_class,live_table,4);
 Put32(live_table,0,uint32_t(uintptr_t(old_class)));assert(!native_interface_graph::InterfaceAt(metadata,interface_view,0,&selected)&&selected==reinterpret_cast<void*>(uintptr_t(0x1234)));Put32(live_table,0,table_class);
 Put32(live_table,8,4);assert(!native_interface_graph::InterfaceAt(metadata,interface_view,0,&selected));Put32(live_table,8,6);
 uint32_t first_interface=0;std::memcpy(&first_interface,static_cast<char*>(live_table)+12,4);Put32(live_table,12,uint32_t(uintptr_t(old_class)));assert(!native_interface_graph::InterfaceAt(metadata,interface_view,0,&selected));Put32(live_table,12,first_interface);

 static_cast<class_metadata::Class17*>(public_list)->iftable=uint32_t(uintptr_t(IfTable({interface_a,interface_b}))); // inherited C omitted
 assert(!native_interface_graph::InterfaceAt(metadata,interface_view,0,&selected));
 modern_field_metadata::FieldMetadata missing_closure;assert(missing_closure.Bind(hooks,&error));
 cal.metadata=&missing_closure;assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),calibrate,&cal)!=runnable_jni::Result::kSuccess&&!missing_closure.Calibrated());cal.metadata=&metadata;
 static_cast<class_metadata::Class17*>(public_list)->iftable=positive_table;
 const uint32_t fixture_table=old_class->iftable;
 for(unsigned invalid=0;invalid<5;++invalid){void* bad=IfTable(invalid==3?std::vector<void*>{interface_a,interface_a}:invalid==4?std::vector<void*>{old_class}:std::vector<void*>{});
  if(invalid==0)old_class->iftable=0;else{old_class->iftable=uint32_t(uintptr_t(bad));if(invalid==1)Put32(bad,8,1);if(invalid==2)Put32(bad,8,1000000);}
  const auto saved=observed_interfaces;assert(!native_interface_graph::Read(metadata,old_class,&observed_interfaces,&error)&&observed_interfaces==saved);
 }
 old_class->iftable=fixture_table;
 modern_field_storage::Service fields;if(!fields.Calibrate(hooks,&env,reinterpret_cast<jobject>(old_class),reinterpret_cast<jobject>(old_instance),metadata,oracle.data(),4,&error)){std::cerr<<"Fixture field storage calibration: threadExtent="<<thread_extent<<" cardExtent="<<card_extent<<" bias="<<bias<<" current="<<current<<" "<<error<<std::endl;return 1;}
 native_cache::Service caches;assert(caches.Bind(hooks,&thread_list,&error)&&caches.Prepare(&error));
 Methods(old_class,false);Methods(new_class,false);
 std::array<method_identity::Oracle,3> method_oracles{};
 for(size_t i=0;i<3;++i){auto* m=reinterpret_cast<abi::arm64::android12::Method12*>(reinterpret_cast<char*>(old_method)+i*32);executable[i]=new(heap.New(sizeof(executable_mirror::Declaration)))executable_mirror::Declaration{};executable[i]->art_method=uintptr_t(m);executable[i]->dex_method_index=uint32_t(i);executable[i]->access_flags=i==0?0x101:i==1?1:9;method_oracles[i]={reinterpret_cast<jobject>(executable[i]),uint32_t(i),executable[i]->access_flags};}
 method_identity::Calibration model{};std::unique_ptr<executable_mirror::Layout> layout;std::unique_ptr<modern_methods::Binding> binding;
 struct Proof{ProviderHooks* hooks;method_identity::Calibration* model;std::unique_ptr<executable_mirror::Layout>* layout;std::unique_ptr<modern_methods::Binding>* binding;const method_identity::Oracle* oracle;std::string* error;} proof{&hooks,&model,&layout,&binding,method_oracles.data(),&error};
 auto prove=[](const runnable_jni::Context& ctx,void* raw){auto& r=*static_cast<Proof*>(raw);if(!method_identity::Calibrate(ctx,Leaf,r.oracle,3,r.model,r.error))return false;*r.layout=executable_mirror::Layout::Probe(ctx,Leaf,DecodeMirror,r.oracle,3,r.error);if(!*r.layout)return false;*r.binding=modern_methods::Service::BindOwned(*r.hooks,ctx,Leaf,r.oracle[0],&binding_value,&linker,r.model->model,r.error);return bool(*r.layout)&&bool(*r.binding);};
 if(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),prove,&proof)!=runnable_jni::Result::kSuccess){std::cerr<<"Fixture native method calibration: "<<error<<std::endl;return 1;}
 native_structural_reflection::Service reflection;assert(reflection.Bind(hooks,&runtime,*layout,&error));
 std::array<std::vector<unsigned char>,4> dex{File(argv[1]),File(argv[2]),File(argv[3]),File(argv[4])};
 LinkDex(old_class,dex[0]);
 source_commit::Plan plan{old_class,class_array,{old_class},{new_class},{old_instance},{new_instance},true};
 native_source_commit::Diagnostics diagnostics;diagnostics.cleared_threads=87;diagnostics.declaring_roots_verified=true;
 native_member_provider::Dependencies dependencies;dependencies.hooks=&hooks;dependencies.methods=binding.get();dependencies.method_model=&model;dependencies.metadata=&metadata;dependencies.fields=&fields;dependencies.cache=&caches;dependencies.reflection=&reflection;dependencies.is_retired=Retired;dependencies.allow_field_removal=true;dependencies.diagnostics=&diagnostics;
 dependencies.retirement={&retired_methods,NotRetired,NotRetired,RetireMethod,RetireField};dependencies.handle_guards={&retired_methods,Mh,Vh};
 dependencies.runtime.runtime=&runtime;dependencies.runtime.instrumentation=&instrumentation;dependencies.runtime.receivers.thread_list=&thread_list;dependencies.runtime.receivers.jit=&jit;dependencies.runtime.receivers.jit_code_cache=&jit_cache;
 OrdinaryAliasOwner alias_owner;alias_owner.oracles=oracle.data();
 auto capture_alias=[](const runnable_jni::Context&,void*raw){auto&o=*static_cast<OrdinaryAliasOwner*>(raw);for(size_t i=0;i<4;++i)o.native[i]=FieldLeaf(o.oracles[i].strong_global_field);return true;};
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),capture_alias,&alias_owner)==runnable_jni::Result::kSuccess);
 Stop(nullptr,nullptr,false);
 auto shape=[&](bool methods,bool removed_fields){Methods(new_class,methods);Put32(reinterpret_cast<void*>(new_class->fields),0,removed_fields?3:4);LinkDex(new_class,dex[(methods?1:0)+(removed_fields?2:0)]);};
 auto rejected=[&](native_member_provider::Dependencies d,const source_commit::Plan& p,const char* expected=nullptr){const std::vector<unsigned char> bytes(heap.base,heap.base+heap.used);const int before=mutations;native_member_provider::Service provider;assert(!provider.Prepare(p,d,&error));assert(!error.empty());if(expected&&error.find(expected)==std::string::npos){std::cerr<<"Expected "<<expected<<" got "<<error<<"\n";std::abort();}assert(mutations==before&&events.empty()&&retired_methods==0&&retired_fields==0);assert(!std::memcmp(bytes.data(),heap.base,bytes.size()));assert(diagnostics.cleared_threads==87&&diagnostics.declaring_roots_verified);};
 shape(true,false);dependencies.expected_removal=native_member_provider::Removal::kFieldsOnly;rejected(dependencies,plan,"transaction shape");
 shape(false,true);dependencies.expected_removal=native_member_provider::Removal::kMethodsOnly;rejected(dependencies,plan,"transaction shape");
 shape(true,true);rejected(dependencies,plan,"transaction shape");dependencies.expected_removal=native_member_provider::Removal::kFieldsOnly;rejected(dependencies,plan,"transaction shape");
 shape(false,false);rejected(dependencies,plan,"transaction shape");dependencies.expected_removal=native_member_provider::Removal::kMethodsOnly;rejected(dependencies,plan,"transaction shape");
 shape(true,false);visit_mh=true;rejected(dependencies,plan,"unsupported MethodHandle");mh_guard=true;visit_mh=false;
 shape(false,true);dependencies.expected_removal=native_member_provider::Removal::kFieldsOnly;visit_vh=true;rejected(dependencies,plan,"unsupported MethodHandle");vh_guard=true;visit_vh=false;
 // A Field visited through MethodHandle kind 3 needs its own installed owner
 // proof: neither ordinary-method readiness nor VarHandle readiness proves it.
 visit_field_mh=true;mh_guard=true;vh_guard=true;
 rejected(dependencies,plan,"unsupported MethodHandle"); // Missing fourth callback.
 dependencies.handle_guards.field_method_handles=FieldMh;
 rejected(dependencies,plan,"unsupported MethodHandle"); // Independent proof false.
 field_mh_guard=true;mh_guard=false;vh_guard=false;
 {native_member_provider::Service field_mh_provider;const std::vector<unsigned char> bytes(heap.base,heap.base+heap.used);auto valid=field_mh_provider.Prepare(plan,dependencies,&error);assert(valid&&valid->Validate(plan));
  assert(!std::memcmp(bytes.data(),heap.base,bytes.size())&&!retired_methods&&!retired_fields&&!mutations&&events.empty());
  field_mh_guard=false;assert(!source_commit::Run(plan,*valid));field_mh_guard=true;
  assert(!std::memcmp(bytes.data(),heap.base,bytes.size())&&!retired_methods&&!retired_fields&&!mutations&&events.empty());
  assert(diagnostics.cleared_threads==87&&diagnostics.declaring_roots_verified);}
 visit_field_mh=false;mh_guard=true;vh_guard=true;
 auto missing=dependencies;
 for(int n=0;n<8;++n){missing=dependencies;switch(n){case 0:missing.hooks=nullptr;break;case 1:missing.methods=nullptr;break;case 2:missing.method_model=nullptr;break;case 3:missing.metadata=nullptr;break;case 4:missing.fields=nullptr;break;case 5:missing.cache=nullptr;break;case 6:missing.reflection=nullptr;break;case 7:missing.is_retired=nullptr;break;}rejected(missing,plan,"Incomplete independently");}
 auto invalid_model=model;invalid_model.model=static_cast<method_identity::Model>(99);missing=dependencies;missing.method_model=&invalid_model;rejected(missing,plan,"complete independent source calibration");
 source_commit::Plan missing_pair=plan;missing_pair.new_classes.clear();rejected(dependencies,missing_pair,"Incomplete independently");
 missing=dependencies;missing.allow_field_removal=false;rejected(missing,plan);missing=dependencies;missing.retirement.retire_field=nullptr;rejected(missing,plan);
 current=foreign_thread.data();rejected(dependencies,plan,"Incomplete independently");current=thread.data();
 // Full actual provider preparation/facts/reflection/source validation with an
 // explicit immutable mapping view. Denials must never query the OS instead.
 ExplicitMappings mappings;mappings.Cover(heap.base);mappings.Cover(names[0]);
 for(auto text:names)mappings.Cover(text);for(auto text:types)mappings.Cover(text);
 mappings.Cover(Descriptor(old_class,nullptr));mappings.Cover(Descriptor(parent_class,nullptr));
 for(const auto& bytes:dex)mappings.Cover(bytes.data());
 modern_field_metadata::ReadContext read_context{&mappings,ExplicitMappings::Extent};
 auto contextual=dependencies;contextual.read_context=&read_context;
 shape(true,false);contextual.expected_removal=native_member_provider::Removal::kMethodsOnly;
 const auto context_queries=method_identity::MappingOsQueries();
 native_member_provider::Service contextual_provider;auto contextual_service=contextual_provider.Prepare(plan,contextual,&error);
 if(!contextual_service){std::cerr<<"Explicit provider mapping: "<<error<<'\n';return 1;}
 RegisteredRoots contextual_roots;assert(contextual_provider.CollectManagedRoots(*contextual_service,{&contextual_roots,RegisteredRoots::Add}));
 assert(contextual_provider.RefreshManagedRoots(plan,*contextual_service,{&contextual_roots,RegisteredRoots::Get}));
 assert(contextual_service->Validate(plan)&&mappings.calls&&method_identity::MappingOsQueries()==context_queries);
 for(const void* denied:{static_cast<const void*>(new_class),static_cast<const void*>(old_fields[0]),static_cast<const void*>(class_array)}){
  mappings.denied=denied;assert(!contextual_service->Validate(plan));assert(method_identity::MappingOsQueries()==context_queries);
 }
 mappings.denied=dex[0].data();native_member_provider::Service denied_text_provider;
 assert(!denied_text_provider.Prepare(plan,contextual,&error)&&method_identity::MappingOsQueries()==context_queries);
 mappings.denied=nullptr;assert(contextual_service->Validate(plan));
 mappings.reject_all=true;native_member_provider::Service denied_provider;assert(!denied_provider.Prepare(plan,contextual,&error));
 assert(method_identity::MappingOsQueries()==context_queries);mappings.reject_all=false;
 modern_field_metadata::ReadContext missing_extent{};contextual.read_context=&missing_extent;
 native_member_provider::Service missing_context_provider;assert(!missing_context_provider.Prepare(plan,contextual,&error));
 assert(method_identity::MappingOsQueries()==context_queries);

 missing=dependencies;missing.diagnostics=nullptr; // Diagnostics are optional to the adapter; missing guards are not.
 missing.handle_guards={};visit_vh=true;rejected(missing,plan);visit_vh=false;
 shape(true,false);auto invalid_policy=dependencies;invalid_policy.expected_removal=static_cast<native_member_provider::Removal>(99);rejected(invalid_policy,plan,"Unknown native");
 shape(false,true);vh_guard=true;visit_vh=true;{native_member_provider::Service fields_provider;auto valid=fields_provider.Prepare(plan,dependencies,&error);assert(valid&&valid->Validate(plan)&&!retired_fields&&!retired_methods&&!mutations);}visit_vh=false;
 shape(true,true);auto any=dependencies;any.expected_removal=native_member_provider::Removal::kAny;{native_member_provider::Service mixed_provider;auto valid=mixed_provider.Prepare(plan,any,&error);assert(valid&&valid->Validate(plan)&&!retired_fields&&!retired_methods&&!mutations);}
 shape(true,false);dependencies.expected_removal=native_member_provider::Removal::kMethodsOnly;mh_guard=true;visit_mh=true;
 hooks.missing="ReplaceReferences";rejected(dependencies,plan);hooks.missing=nullptr;
 native_member_provider::Service provider;auto service=provider.Prepare(plan,dependencies,&error);if(!service){std::cerr<<error<<'\n';return 1;}assert(service->Validate(plan));assert(retired_methods==0&&retired_fields==0&&mutations==0&&events.empty());
 // Single-Class MethodsOnly reaches the same static alias authority as schema.
 // Missing real sole consumer must reject; install its actual source proxy,
 // retain the independently calibrated public field tuple, and validate again.
 OrdinaryAliasHooks alias_hooks;
 auto ordinary_alias_dependencies=dependencies;ordinary_alias_dependencies.retirement.owner=&alias_owner;
 ordinary_alias_dependencies.retirement.static_alias_ready=OrdinaryAliasOwner::Ready;
 native_member_provider::Service missing_alias_provider;assert(!missing_alias_provider.Prepare(plan,ordinary_alias_dependencies,&error)&&error.find("Static alias")!=std::string::npos);
 void* alias_roots[]{old_class};exclusive_owned_roots::Context alias_context{current,alias_roots,1};
 assert(alias_owner.observer.Bind(alias_hooks,{&alias_owner,Current,OrdinaryAliasOwner::Root},&error));
 assert(alias_owner.observer.SetNormalizerAtExclusive({&alias_owner,OrdinaryAliasOwner::Normalize},alias_context,&error));
 assert(alias_owner.observer.InstallAtExclusive(alias_hooks,alias_context,&error));
 native_member_provider::Service ordinary_alias_provider;auto ordinary_alias_service=ordinary_alias_provider.Prepare(plan,ordinary_alias_dependencies,&error);assert(ordinary_alias_service&&ordinary_alias_service->Validate(plan)&&!retired_methods&&!retired_fields&&!mutations);
 assert(alias_owner.observer.RemoveAtExclusive(alias_hooks,alias_context,&error));assert(!ordinary_alias_service->Validate(plan));
 const std::vector<unsigned char> prepared_heap(heap.base,heap.base+heap.used);
 mh_guard=false;assert(!source_commit::Run(plan,*service));mh_guard=true;
 current=foreign_thread.data();assert(!source_commit::Run(plan,*service));current=thread.data();
 assert(!std::memcmp(prepared_heap.data(),heap.base,prepared_heap.size())&&!retired_methods&&!retired_fields&&!mutations&&events.empty());
 assert(diagnostics.cleared_threads==87&&diagnostics.declaring_roots_verified);
 // Native provenance integration: a prewarmed ancestor field is not in the
 // transaction's old->new map. The real source-kind callback must still clear
 // it before mapping when the replacement child declares the same field.
 shape(false,false);visit_mh=false;
 native_method_table::View before_table,after_table;
 assert(native_method_table::Read(metadata,model,old_class,&before_table,&error));
 assert(native_method_table::Read(metadata,model,new_class,&after_table,&error));
 auto table=modern_methods::Service::PrepareTables(*binding,old_class,before_table.methods,new_class,after_table.methods,[](void*){return false;},&error);
 assert(table);
 const uint64_t saved_parent_fields=static_cast<class_metadata::Class17*>(parent_class)->fields;
 auto* ancestor_array=static_cast<unsigned char*>(heap.New(4+sizeof(abi::arm64::Field)));Put32(ancestor_array,0,1);
 ancestor_cached_field=new(ancestor_array+4)abi::arm64::Field{};
 ancestor_cached_field->declaring_class=uint32_t(uintptr_t(parent_class));ancestor_cached_field->access_flags=8;
 static_cast<class_metadata::Class17*>(parent_class)->fields=uintptr_t(ancestor_array);
 auto mapping=native_structural_reflection::Plan::Prepare(plan,metadata,{table.get()},dependencies.retirement,&error,dependencies.handle_guards,{&hooks,&model,&fields});
 assert(mapping);
 auto* ancestor_method=new(heap.New(sizeof(abi::arm64::android12::Method12)))abi::arm64::android12::Method12{};
 ancestor_method->declaring_class=uint32_t(uintptr_t(parent_class));ancestor_method->access_flags=9;ancestor_method->dex_method_index=2;
 visit_resolution=true;resolved_method_slot=reinterpret_cast<art::ArtMethod*>(ancestor_method);resolved_field_slot=reinterpret_cast<art::ArtField*>(ancestor_cached_field);
 assert(reflection.Validate(*mapping,&error)&&resolved_field_slot==reinterpret_cast<art::ArtField*>(ancestor_cached_field)&&resolved_method_slot==reinterpret_cast<art::ArtMethod*>(ancestor_method));
 reflection.Update(*mapping);assert(!resolved_field_slot&&!resolved_method_slot);
 // A native cache callback can legally point outside the transaction's ancestry.
 // A proxy or array needs a platform string for GetDescriptor, but resolution
 // eligibility must not request its descriptor or inspect its field storage.
 auto* unrelated=new(heap.New(sizeof(class_metadata::Class17)))class_metadata::Class17{};
 unrelated->fields=UINT64_MAX;unrelated->object.klass=uint32_t(uintptr_t(meta_class));
 auto* unrelated_method=new(heap.New(sizeof(abi::arm64::android12::Method12)))abi::arm64::android12::Method12{};
 unrelated_method->declaring_class=uint32_t(uintptr_t(unrelated));unrelated_method->access_flags=9;unrelated_method->dex_method_index=2;
 auto* unrelated_field=new(heap.New(sizeof(abi::arm64::Field)))abi::arm64::Field{};
 unrelated_field->declaring_class=uint32_t(uintptr_t(unrelated));unrelated_field->access_flags=8;
 forbidden_descriptor_class=unrelated;
 for(unsigned mode=0;mode<3;++mode){unrelated->access_flags=mode==0?0x40000u:1u;unrelated->component_type=mode==1?uint32_t(uintptr_t(parent_class)):0;unrelated->primitive_type=mode==2?5:0;
  resolved_method_slot=reinterpret_cast<art::ArtMethod*>(unrelated_method);resolved_field_slot=reinterpret_cast<art::ArtField*>(unrelated_field);
  assert(reflection.Validate(*mapping,&error));reflection.Update(*mapping);
  assert(resolved_method_slot==reinterpret_cast<art::ArtMethod*>(unrelated_method)&&resolved_field_slot==reinterpret_cast<art::ArtField*>(unrelated_field));}
 assert(forbidden_descriptor_calls==0);forbidden_descriptor_class=nullptr;
 // Excluded Runtime/virtual/constructor and private-field entries do not need
 // a declaring Class root; still exercise the production TU native observer.
 unrelated_method->declaring_class=0;unrelated_field->declaring_class=0;unrelated_field->access_flags=2;
 for(uint32_t flags:{1u,0x10001u}){unrelated_method->access_flags=flags;
  resolved_method_slot=reinterpret_cast<art::ArtMethod*>(unrelated_method);resolved_field_slot=reinterpret_cast<art::ArtField*>(unrelated_field);
  assert(reflection.Validate(*mapping,&error));reflection.Update(*mapping);assert(resolved_method_slot&&resolved_field_slot);}
 // An eligible missing root remains a readonly refusal, with precise first
 // callback diagnostics. It must never become an unrelated-cache Keep.
 unrelated_method->access_flags=9;
 resolved_method_slot=reinterpret_cast<art::ArtMethod*>(unrelated_method);resolved_field_slot=nullptr;
 assert(!reflection.Validate(*mapping,&error));assert(error.find("source=8 kind=method")!=std::string::npos&&error.find("stage=declaring-class")!=std::string::npos);
 assert(resolved_method_slot==reinterpret_cast<art::ArtMethod*>(unrelated_method));
 unrelated_method->declaring_class=uint32_t(uintptr_t(unrelated));unrelated->access_flags=0x200;unrelated->primitive_type=0;unrelated->component_type=0;
 resolved_method_slot=reinterpret_cast<art::ArtMethod*>(unrelated_method);
 assert(reflection.Validate(*mapping,&error)); // Proven empty holder IfTable.
 const uint32_t original_table=old_class->iftable;
 old_class->iftable=uint32_t(uintptr_t(IfTable({unrelated})));
 assert(!reflection.Validate(*mapping,&error)); // Prepared graph is immutable.
 mapping=native_structural_reflection::Plan::Prepare(plan,metadata,{table.get()},dependencies.retirement,&error,dependencies.handle_guards,{&hooks,&model,&fields});assert(mapping);
 assert(reflection.Validate(*mapping,&error));reflection.Update(*mapping);assert(!resolved_method_slot); // Exact interface identity shadows.
 old_class->iftable=uint32_t(uintptr_t(IfTable({interface_a,interface_b,interface_c})));
 mapping=native_structural_reflection::Plan::Prepare(plan,metadata,{table.get()},dependencies.retirement,&error,dependencies.handle_guards,{&hooks,&model,&fields});assert(mapping);
 resolved_method_slot=reinterpret_cast<art::ArtMethod*>(unrelated_method);assert(reflection.Validate(*mapping,&error));reflection.Update(*mapping);assert(resolved_method_slot); // Other interfaces do not match by descriptor.
 old_class->iftable=0;assert(!reflection.Validate(*mapping,&error)&&error.find("live-holder-interface-table-invalid")!=std::string::npos);
 old_class->iftable=original_table;
 visit_resolution=false;static_cast<class_metadata::Class17*>(parent_class)->fields=saved_parent_fields;
 auto unchanged_without_provenance=native_structural_reflection::Plan::Prepare(plan,metadata,{table.get()},dependencies.retirement,&error,dependencies.handle_guards);
 assert(unchanged_without_provenance);
 // A new direct declaration must never silently fall back to the old mapping
 // adapter when complete resolution provenance is omitted.
 LinkDex(old_class,dex[1]);Methods(old_class,true);LinkDex(new_class,dex[0]);Methods(new_class,false);
 assert(native_method_table::Read(metadata,model,old_class,&before_table,&error));
 assert(native_method_table::Read(metadata,model,new_class,&after_table,&error));
 table=modern_methods::Service::PrepareTables(*binding,old_class,before_table.methods,new_class,after_table.methods,[](void*){return false;},&error);assert(table);
 assert(!native_structural_reflection::Plan::Prepare(plan,metadata,{table.get()},dependencies.retirement,&error,dependencies.handle_guards));
 assert(error.find("Added declarations require")!=std::string::npos);
 auto additive=native_structural_reflection::Plan::Prepare(plan,metadata,{table.get()},dependencies.retirement,&error,dependencies.handle_guards,{&hooks,&model,&fields});
 assert(additive&&reflection.Validate(*additive,&error));
 // Full schema mode consumes exact independent native/DEX declarations.
 // Static type changes and instance->static storage do not reinterpret values;
 // new static/instance slots retain allocation defaults, while old exact references copy.
 shape(false,false);auto evolved_dex=File(argv[5]);LinkDex(new_class,evolved_dex);evolved_fields=true;
 old_class->access_flags=new_class->access_flags=1;
 new_fields[0]->access_flags=8;new_fields[1]->access_flags=9;new_fields[1]->offset=144;
 new_fields[2]->access_flags=65;new_fields[3]->access_flags=1;
 assert(native_method_table::Read(metadata,model,old_class,&before_table,&error));
 assert(native_method_table::Read(metadata,model,new_class,&after_table,&error));
 native_schema_policy::Plan schema;assert(native_schema_policy::Plan::Prepare(before_table.declarations,after_table.declarations,&schema,&error));
 std::vector<native_schema_policy::Plan> schemas{schema};auto schema_dependencies=dependencies;
 schema_dependencies.expected_removal=native_member_provider::Removal::kSchemaEvolution;schema_dependencies.schemas=&schemas;schema_dependencies.allow_field_removal=true;
 uint64_t initialized=0;std::memcpy(reinterpret_cast<char*>(new_class)+128,&initialized,8);Put32(new_class,144,0);
 // Reject forged immutable native flags before touching the actual transaction.
 new_fields[2]->access_flags=1;rejected(schema_dependencies,plan,"field flags/index");new_fields[2]->access_flags=65;
 auto no_schema=schema_dependencies;no_schema.schemas=nullptr;rejected(no_schema,plan,"explicit complete policies");
 // Adding final permissions must not be authorized by the field-copy plan.
 // The real guard verifier is independently required before any mutation.
 auto final_dex=File(argv[6]);LinkDex(new_class,final_dex);new_fields[3]->access_flags=17;
 native_method_table::View final_table;assert(native_method_table::Read(metadata,model,new_class,&final_table,&error));
 native_schema_policy::Plan final_schema;assert(native_schema_policy::Plan::Prepare(before_table.declarations,final_table.declarations,&final_schema,&error));
 schemas[0]=final_schema;rejected(schema_dependencies,plan,"independent loaded-Class/final-field proof");
 LinkDex(new_class,evolved_dex);new_fields[3]->access_flags=1;schemas[0]=schema;
 // Hierarchy policies must not authorize unpaired native parent changes without
 // the actual same-owner rooted allocator/receiver/cache proof. This exercises
 // the production binding with real complete DEX tables, not a policy-only mock.
 auto hierarchy_dex=File(argv[7]);LinkDex(new_class,hierarchy_dex);
 const auto original_parent=new_class->super_class;new_class->super_class=uint32_t(uintptr_t(meta_class));
 native_method_table::View hierarchy_table;assert(native_method_table::Read(metadata,model,new_class,&hierarchy_table,&error));
 native_schema_policy::Plan hierarchy_schema;assert(native_schema_policy::Plan::Prepare(before_table.declarations,hierarchy_table.declarations,&hierarchy_schema,&error));
 assert(hierarchy_schema.Requires(native_schema_policy::GuardKind::HierarchyGraph));schemas[0]=hierarchy_schema;
 rejected(schema_dependencies,plan,"independent loaded-Class/final-field proof");
 new_class->super_class=original_parent;LinkDex(new_class,evolved_dex);schemas[0]=schema;
 mappings.Cover(evolved_dex.data());
 schema_dependencies.read_context=&read_context;
 const auto schema_os_queries=method_identity::MappingOsQueries();
 native_member_provider::Service schema_provider;auto schema_service=schema_provider.Prepare(plan,schema_dependencies,&error);
 if(!schema_service){std::cerr<<"Native schema preparation: "<<error<<'\n';return 1;}
 assert(schema_service->Validate(plan));assert(method_identity::MappingOsQueries()==schema_os_queries);const auto roots_before_retire=retired_fields;
 const auto method_array_before=new_class->methods;
 auto* moved_methods=heap.New(8+3*32);std::memcpy(moved_methods,reinterpret_cast<void*>(uintptr_t(method_array_before)),8+3*32);
 new_class->methods=uintptr_t(moved_methods);assert(!source_commit::Run(plan,*schema_service));new_class->methods=method_array_before;
 const auto declaring_before=new_fields[0]->declaring_class;new_fields[0]->declaring_class=uint32_t(uintptr_t(old_class));
 assert(!source_commit::Run(plan,*schema_service));new_fields[0]->declaring_class=declaring_before;
 new_fields[2]->access_flags=1;assert(!source_commit::Run(plan,*schema_service));new_fields[2]->access_flags=65;
 assert(retired_fields==roots_before_retire&&!mutations&&!replaced);
 // Collected prepares every table/schedule/map before a moving GC. Every
 // managed ancestor is registered, including untouched bootstrap Object.
 RegisteredRoots roots;assert(schema_provider.CollectManagedRoots(*schema_service,{&roots,RegisteredRoots::Add}));
 auto* previous_old=old_class;auto* previous_new=new_class;void* previous_parent=parent_class;
 old_class=static_cast<class_metadata::Class17*>(heap.New(160));new_class=static_cast<class_metadata::Class17*>(heap.New(160));parent_class=heap.New(160);
 std::memcpy(old_class,previous_old,160);std::memcpy(new_class,previous_new,160);std::memcpy(parent_class,previous_parent,160);
 old_class->super_class=uint32_t(uintptr_t(parent_class));new_class->super_class=uint32_t(uintptr_t(parent_class));
 static_cast<class_metadata::Class17*>(public_array_class)->component_type=uint32_t(uintptr_t(parent_class));
 for(auto* field:old_fields)field->declaring_class=uint32_t(uintptr_t(old_class));for(auto* field:new_fields)field->declaring_class=uint32_t(uintptr_t(new_class));
 for(auto* klass:{old_class,new_class}){auto* methods=reinterpret_cast<unsigned char*>(uintptr_t(klass->methods));for(uint32_t i=0;i<Read32(methods);++i)Put32(methods,8+size_t(i)*32,uint32_t(uintptr_t(klass)));}
 Put32(old_instance,0,uint32_t(uintptr_t(old_class)));Put32(new_instance,0,uint32_t(uintptr_t(new_class)));Put32(class_array,12,uint32_t(uintptr_t(new_class)));
 plan.mirror=old_class;plan.old_classes[0]=old_class;plan.new_classes[0]=new_class;
 roots.Move(previous_old,old_class);roots.Move(previous_new,new_class);roots.Move(previous_parent,parent_class);
 const auto registered_count=roots.values.size();events.reserve(events.size()+32);
 forbid_provider_allocation=true;
 assert(schema_provider.RefreshManagedRoots(plan,*schema_service,{&roots,RegisteredRoots::Get}));
 assert(schema_service->Validate(plan));
 new_fields[2]->access_flags=1;assert(!source_commit::Run(plan,*schema_service));new_fields[2]->access_flags=65;
 const auto refreshed_methods=new_class->methods;new_class->methods=uintptr_t(moved_methods);
 assert(!source_commit::Run(plan,*schema_service));new_class->methods=refreshed_methods;
 assert(retired_fields==roots_before_retire&&!mutations&&!replaced);
 // Actual shared source commit, reflection/resolution scan and replacement
 // hash rebuilding all execute with the allocation failure guard active.
 assert(source_commit::Run(plan,*schema_service));
 forbid_provider_allocation=false;assert(roots.values.size()==registered_count);
 assert(method_identity::MappingOsQueries()==schema_os_queries);
 uint64_t initialized_after=0;std::memcpy(&initialized_after,reinterpret_cast<char*>(new_class)+128,8);
 assert(initialized_after==0&&Read32(new_class,144)==0); // No encoded values or clinit rerun.
 assert(Read32(new_instance,12)==uintptr_t(ref_a)&&Read32(new_instance,16)==uintptr_t(ref_b));
 assert(Read32(old_instance,8)==0&&Read32(old_instance,12)==0&&Read32(old_instance,16)==0);
 assert(retired_fields==roots_before_retire+2);
 Resume(nullptr);
 std::cout<<"Native member provider: explicit stopped mapping context denies without OS fallback; complete schema/GC refresh/copy/resolution run has zero OS mapping queries and no allocation; public field/method/Executable calibration, complete DEX tables, MethodsOnly/FieldsOnly/mixed rejection, independent live method/field MH and VH guards, missing field policy and wrong thread all refuse before heap mutation/retirement/diagnostics; guarded method/field/mixed preparation stays readonly and final guard/thread refusal never retires\n";
}
