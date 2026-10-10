#include "platform/common/native_hierarchy_graph.h"
#include "platform/common/native_hierarchy_parent.h"
#include "platform/common/rooted_graph_class_identity.h"
#include "platform/common/modern_field_metadata.h"
#include "platform/common/native_interface_graph.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/common/readable_mapping_scope.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <map>
#include <unordered_map>
#include <unordered_set>
#if defined(__ANDROID__)
#include <android/log.h>
#include <chrono>
#endif
namespace artdcevm::native_hierarchy_graph {
namespace {
constexpr size_t absent=SIZE_MAX;
// Only public ordinary Stage calls this trace, never stopped/final callbacks.
// Seven aggregate boundaries distinguish public JNI/root staging from native
// metadata and owner CPU work without per-Class logging.
struct StageTrace {
#if defined(__ANDROID__)
 using Clock=std::chrono::steady_clock;Clock::time_point begin=Clock::now(),previous=begin;
 uint64_t queries=method_identity::MappingOsQueries();
 void Mark(const char*phase,size_t nodes){auto now=Clock::now();auto current=method_identity::MappingOsQueries();
  __android_log_print(ANDROID_LOG_INFO,"ARTDCEVM","hierarchy-stage phase=%s nodes=%zu phaseMs=%.3f totalMs=%.3f mappingQueries=%llu",phase,nodes,std::chrono::duration<double,std::milli>(now-previous).count(),std::chrono::duration<double,std::milli>(now-begin).count(),static_cast<unsigned long long>(current-queries));previous=now;queries=current;}
#else
 void Mark(const char*,size_t){}
#endif
};
bool Fail(std::string* e,const char* m){if(e)*e=m;return false;}
std::string Package(const std::string& s){auto p=s.rfind('/');return p==std::string::npos?std::string{}:s.substr(0,p);}
bool Same(const evolution::ClassSchema& a,const evolution::ClassSchema& b){
 const auto members=[](const auto& x,const auto& y){if(x.size()!=y.size())return false;for(size_t i=0;i<x.size();++i)if(x[i].name!=y[i].name||x[i].descriptor!=y[i].descriptor||x[i].access!=y[i].access||x[i].dex_index!=y[i].dex_index)return false;return true;};
 return a.descriptor==b.descriptor&&a.superclass==b.superclass&&a.interfaces==b.interfaces&&a.access==b.access&&members(a.fields,b.fields)&&members(a.methods,b.methods);
}
std::vector<size_t> Closure(const Graph& g,size_t start,bool proposed){
 std::vector<size_t> pending{start},seen,result;
 for(size_t cursor=0;cursor<pending.size();++cursor){size_t n=pending[cursor];if(std::find(seen.begin(),seen.end(),n)!=seen.end())continue;seen.push_back(n);const auto& node=g.nodes[n];
  for(auto i:proposed?node.proposed_interfaces:node.interfaces){if(std::find(result.begin(),result.end(),i)==result.end())result.push_back(i);pending.push_back(i);}
  size_t parent=proposed?node.proposed_parent:node.parent;if(parent!=absent)pending.push_back(parent);
 }return result;
}
bool SetMatches(const std::vector<void*>& actual,const std::vector<size_t>& expected,const Graph& g,bool replacement){
 if(actual.size()!=expected.size())return false;for(auto n:expected){void* k=replacement&&g.nodes[n].replacement?g.nodes[n].replacement:g.nodes[n].klass;if(std::count(actual.begin(),actual.end(),k)!=1)return false;}return true;
}
}
bool CanonicalizeFlags(const modern_field_metadata::FieldMetadata& metadata,Graph* graph,std::string* error,const modern_field_metadata::ReadContext* context){
 if(!graph||!metadata.Calibrated())return Fail(error,"Class flags lack calibrated metadata/rooted graph");
 const auto& layout=metadata.StorageModel()==modern_field_metadata::Model::kSplit?class_metadata::kLegacy:class_metadata::k17;
 constexpr uint32_t source_flags=0x7631u;
 for(auto& node:graph->nodes){size_t extent=0;class_metadata::State state;
  if(node.proposed_flags&&((node.flags^*node.proposed_flags)&~0x410u))return Fail(error,("Public Class proposal changes role/access: "+node.descriptor).c_str());
  if(!node.klass||!modern_field_metadata::ReadMappingExtent(context,node.klass,&extent)||!class_metadata::ReadState(layout,node.klass,extent,&state)||state.IsPrimitive())return Fail(error,("Rooted Class effective flags unavailable from calibrated metadata: "+node.descriptor).c_str());
  const uint32_t public_flags=node.public_flags.value_or(node.flags);
  uint32_t defining_flags=0;
  // Source proxy Classes have no ClassDef or InnerClass annotation. For normal
  // Classes the public query replaces every Java flag from that annotation;
  // compare immutable ClassDef access against calibrated native state instead.
  if(state.access_flags&0x40000u)defining_flags=public_flags;
  else if(!metadata.DefiningClassFlags(node.klass,&defining_flags,error,context))return false;
  if((defining_flags&source_flags)!=(state.access_flags&source_flags))return Fail(error,("Defining DEX/native Class flags disagree: "+node.descriptor+" definingFlags="+std::to_string(defining_flags)+" nativeFlags="+std::to_string(state.access_flags)).c_str());
  node.public_flags=public_flags;node.flags=state.access_flags&source_flags;
  if(node.proposed_flags)node.proposed_flags=(node.flags&~0x410u)|(*node.proposed_flags&0x410u);
 }return true;
}
bool ValidateGraph(Graph* graph,std::string* error){
 if(!graph)return Fail(error,"Empty staged graph");graph->bootstrap_object_.reset();graph->indexed_count_=0;
 if(graph->nodes.empty()||graph->inputs.empty())return Fail(error,"Empty staged graph");auto& g=*graph;const size_t count=g.nodes.size();size_t bootstrap_object=absent;
 std::unordered_set<void*> identities;std::map<std::pair<uintptr_t,std::string>,size_t> namespaces;
 for(size_t n=0;n<count;++n){auto& node=g.nodes[n];node.affected=false;
  if((node.parent!=absent&&node.parent>=count)||std::any_of(node.interfaces.begin(),node.interfaces.end(),[&](auto i){return i>=count;}))return Fail(error,"Original public graph edge out of bounds");
  if(!node.klass||node.descriptor.empty())return Fail(error,"Missing exact public Class identity");
  if(!node.loader&&node.descriptor=="Ljava/lang/Object;")bootstrap_object=n;
  if(!identities.insert(node.klass).second||!namespaces.emplace(std::make_pair(reinterpret_cast<uintptr_t>(node.loader),node.descriptor),n).second)return Fail(error,"Duplicate Class identity in loader namespace");
  if(((node.flags^node.ProposedFlags())&~0x410u)||((node.ProposedFlags()&0x410u)==0x410u))return Fail(error,"Proposed Class changes role/access or combines final and abstract");
  const auto edge=[&](size_t p,bool interface,bool original){if(p>=count||p==n)return false;const auto& target=g.nodes[p];if(bool(target.ProposedFlags()&0x200u)!=interface)return false;if(!interface&&(target.ProposedFlags()&0x10u))return false;return original||(target.ProposedFlags()&1u)||(target.loader==node.loader&&Package(target.descriptor)==Package(node.descriptor));};
  if(node.proposed_parent!=absent&&!edge(node.proposed_parent,false,node.proposed_parent==node.parent))return Fail(error,("Proposed parent has wrong role, finality, identity or access: child="+node.descriptor+" parent="+(node.proposed_parent<count?g.nodes[node.proposed_parent].descriptor:std::string("<out-of-bounds>"))+" retained="+(node.proposed_parent==node.parent?"true":"false")+" childFlags="+std::to_string(node.ProposedFlags())+" parentFlags="+(node.proposed_parent<count?std::to_string(g.nodes[node.proposed_parent].ProposedFlags()):std::string("<none>"))).c_str());
  if(node.parent!=node.proposed_parent&&node.proposed_parent!=absent&&!g.nodes[node.proposed_parent].initialized)return Fail(error,"Added superclass has not independently initialized");
  for(size_t i=0;i<node.proposed_interfaces.size();++i){if(!edge(node.proposed_interfaces[i],true,std::find(node.interfaces.begin(),node.interfaces.end(),node.proposed_interfaces[i])!=node.interfaces.end())||std::find(node.proposed_interfaces.begin(),node.proposed_interfaces.begin()+i,node.proposed_interfaces[i])!=node.proposed_interfaces.begin()+i)return Fail(error,"Proposed interface role, identity, access or duplicates invalid");}
  // java.lang.Object is the sole ordinary class with no superclass. Interfaces
  // have null public superclass even though their DEX superclass is Object.
  if(!(node.flags&0x200u)&&node.proposed_parent==absent&&(node.descriptor!="Ljava/lang/Object;"||node.loader))return Fail(error,"Ordinary class lost bootstrap Object ancestry");
 }
 std::vector<unsigned char> colors(count);struct Frame{size_t n,next;};
 for(size_t root=0;root<count;++root){if(colors[root])continue;std::vector<Frame> stack{{root,0}};colors[root]=1;
  while(!stack.empty()){auto& f=stack.back();const auto& n=g.nodes[f.n];size_t p=absent;
   if(f.next==0){++f.next;p=n.proposed_parent;}else if(f.next<=n.proposed_interfaces.size()){p=n.proposed_interfaces[f.next++-1];}else{colors[f.n]=2;stack.pop_back();continue;}
   if(p==absent)continue;if(p>=count||colors[p]==1)return Fail(error,"Cycle in actual proposed batch graph");if(!colors[p]){colors[p]=1;stack.push_back({p,0});}
  }
 }
 std::vector<std::vector<size_t>> reverse(count);std::vector<size_t> affected;
 for(size_t n=0;n<count;++n){const auto& node=g.nodes[n];for(auto p:{node.parent,node.proposed_parent})if(p!=absent)reverse[p].push_back(n);for(auto p:node.interfaces)reverse[p].push_back(n);for(auto p:node.proposed_interfaces)reverse[p].push_back(n);}
 for(auto n:g.inputs){if(n>=count||g.nodes[n].affected)return Fail(error,"Duplicate or missing structural target");g.nodes[n].affected=true;affected.push_back(n);}
 // Current and proposed reverse reachability includes implicit implementors and
 // subclasses, in linear edge traversal regardless of loaded snapshot order.
 for(size_t cursor=0;cursor<affected.size();++cursor)for(auto n:reverse[affected[cursor]])if(!g.nodes[n].affected){g.nodes[n].affected=true;affected.push_back(n);}
 if(bootstrap_object!=absent)g.bootstrap_object_=bootstrap_object;g.indexed_count_=count;return true;
}
struct Authority::State {
 Bindings bindings{};modern_field_metadata::ReadContext read_context{};bool contextual=false;
 const modern_field_metadata::ReadContext* ordinary_context=nullptr;
 const modern_field_metadata::ReadContext* Context()const noexcept{return contextual?&read_context:ordinary_context;}
 struct OrdinaryRead {
  State& owner;const modern_field_metadata::ReadContext* previous;std::optional<method_identity::ScopedReadableMappings> mappings;modern_field_metadata::ReadContext view{};
  explicit OrdinaryRead(State&s):owner(s),previous(s.ordinary_context){if(!s.contextual&&!previous){mappings.emplace();view={&*mappings,[](void*o,const void*p,size_t*out){return static_cast<method_identity::ScopedReadableMappings*>(o)->Extent(p,out);}};s.ordinary_context=&view;}}
  bool Ready()const noexcept{return !mappings||mappings->Ready();}
  ~OrdinaryRead(){owner.ordinary_context=previous;}
 };
 bool Extent(const void*p,size_t*out)const noexcept{return modern_field_metadata::ReadMappingExtent(Context(),p,out);}
 runnable_jni::Context root_context{};bounded_managed_roots::Store public_roots;
 std::vector<bounded_managed_roots::Slot> loaders;
 static bool RootOracle(void*raw,const runnable_jni::Context&c,jobject g,void**out){auto&s=*static_cast<State*>(raw);return s.bindings.borrow_owned(s.bindings.decoder_owner,c,g,out);}
 static bool RootDecode(void*raw,jobject g,void**out){auto&s=*static_cast<State*>(raw);return RootOracle(raw,s.root_context,g,out);}
 static bool RootPair(void*raw,const runnable_jni::Context&c,jobject a,jobject b,void**left,void**right){auto&s=*static_cast<State*>(raw);return s.bindings.borrow_pair&&s.bindings.borrow_pair(s.bindings.decoder_owner,c,a,b,left,right);}
 static void* RootElement(void*raw,void*array,size_t index){auto&s=*static_cast<State*>(raw);return index<size_t(bounded_managed_roots::Store::kChunkSize)?s.bindings.metadata->Reference(array,12+index*4):nullptr;}
 Graph graph;std::vector<Input> inputs;
 std::vector<std::vector<size_t>> closure_before,closure_after,order_before,order_after;
 std::vector<std::vector<void*>> native_before,native_after;
 std::vector<Prepared> allocations;std::vector<evolution::ClassSchema> allocation_declarations;
 prepared_roots::Bindings roots;
 const source_commit::Plan* plan=nullptr;void* mirror=nullptr;void* new_root=nullptr;
 std::vector<void*> old_classes,new_classes,old_instances,new_instances;
 std::vector<source_commit::ClassRootOrigin> origins;bool abstract_instances=false;
 bool staged=false,roots_prepared=false,sealed=false,collected=false,registration_started=false;
 bool OwnersStaged(std::string* e)const{
  const auto verify=[&](const auto* owner,const char* label){std::string detail;if(owner->VerifyStaged(graph,inputs,&detail))return true;if(e)*e=std::string(label)+(detail.empty()?"":": "+detail);return false;};
  return verify(bindings.allocation,"Allocation owner refused staged graph")&&verify(bindings.receivers,"Receiver owner refused staged graph")&&verify(bindings.caches,"Type-cache owner refused staged graph");
 }
 bool Owners(const source_commit::Plan& p,bool final=false)const noexcept{return bindings.allocation->Verify(graph,p)&&(final?bindings.receivers->VerifyFinal(graph,p):bindings.receivers->Verify(graph,p))&&bindings.caches->Verify(graph,p);}
 bool Borrow(const runnable_jni::Context& c) {
  root_context=c;for(auto& n:graph.nodes){if(!public_roots.Read(n.class_root.slot,&n.klass)||!n.klass)return false;if(!public_roots.Read(n.loader_root.slot,&n.loader)||!public_roots.Read(n.proxy_name_root.slot,&n.proxy_name))return false;}
  if(!bindings.allocation->RefreshAllocations(&graph,&allocations))return false;
  for(size_t i=0;i<graph.nodes.size();++i){for(size_t j=0;j<native_before[i].size();++j)native_before[i][j]=graph.nodes[order_before[i][j]].klass;for(size_t j=0;j<native_after[i].size();++j){auto& n=graph.nodes[order_after[i][j]];native_after[i][j]=n.replacement?n.replacement:n.klass;}}return true;
 }
 bool Flags(void* klass,uint32_t flags)const {
  size_t extent=0;class_metadata::State state;const auto& layout=bindings.metadata->StorageModel()==modern_field_metadata::Model::kSplit?class_metadata::kLegacy:class_metadata::k17;return Extent(klass,&extent)&&class_metadata::ReadState(layout,klass,extent,&state)&&!state.IsPrimitive()&&(state.access_flags&0x7631u)==(flags&0x7631u);
 }
 bool Native(bool replacements,std::string* error=nullptr)const{
  for(size_t n=0;n<graph.nodes.size();++n){const auto& node=graph.nodes[n];field_copy::CopyClassView view;
   const auto reject=[&](const char* reason){if(error)*error=std::string(reason)+": "+node.descriptor;return false;};
   void* original_parent=nullptr;
   if(!NativeSuperclass(graph,node,false,&original_parent))return reject("Native superclass identity unavailable");
   if(!Flags(node.klass,node.flags))return reject("Native/public Class flags differ");
   if(!ClassIdentity(*bindings.metadata,node,node.klass,&view,error,Context()))return false;
   if(view.loader!=node.loader||!view.descriptor||node.descriptor!=view.descriptor)return reject("Native/public Class namespace differs");
   if(view.parent!=original_parent)return reject("Native/public superclass differs");
   if(!native_interface_graph::MatchesInContext(*bindings.metadata,node.klass,native_before[n],error,Context()))return reject("Native/public interface identities differ");
   if(replacements&&node.affected){void* parent=node.proposed_parent==absent?nullptr:(graph.nodes[node.proposed_parent].replacement?graph.nodes[node.proposed_parent].replacement:graph.nodes[node.proposed_parent].klass);
    if(!node.replacement||!Flags(node.replacement,node.ProposedFlags())||!ClassIdentity(*bindings.metadata,node,node.replacement,&view,nullptr,Context())||view.loader!=node.loader||!view.descriptor||node.descriptor!=view.descriptor||view.parent!=parent||!native_interface_graph::MatchesInContext(*bindings.metadata,node.replacement,native_after[n],nullptr,Context()))return false;
   }
  }return true;
 }
};
Authority::Authority():state_(new State){}
void Authority::UseReadContext(const modern_field_metadata::ReadContext&c)noexcept{state_->read_context=c;state_->contextual=true;}
Authority::~Authority(){assert(state_->graph.nodes.empty()&&"Clear rooted graph after complete source cleanup");}
void Authority::Clear(JNIEnv* env){if(!env)return;for(auto& n:state_->graph.nodes){if(n.strong_class)env->DeleteGlobalRef(n.strong_class);if(n.strong_loader)env->DeleteGlobalRef(n.strong_loader);}state_->public_roots.Clear(env);*state_=State{};}
const Graph* Authority::Staged()const noexcept{return state_->staged?&state_->graph:nullptr;}
bool Authority::Stage(const runnable_jni::Context& context,jvmtiEnv* ti,const std::vector<Input>& inputs,const Bindings& bindings,std::string* error){
 JNIEnv* env=context.env;if(!env||!context.self||!ti||inputs.empty()||env->ExceptionCheck()||!state_->graph.nodes.empty()||!bindings.metadata||!bindings.metadata->Calibrated()||!bindings.borrow_owned||!bindings.allocation||!bindings.receivers||!bindings.caches)return Fail(error,"Hierarchy stage lacks exact public/native owners");
 StageTrace trace;trace.Mark("begin",0);
 state_->bindings=bindings;state_->inputs=inputs;auto& s=*state_;s.root_context=context;
 s.public_roots.Bind({&s,nullptr,State::RootOracle,State::RootDecode,State::RootElement,bindings.borrow_pair?State::RootPair:nullptr});bool success=false;
 struct Cleanup{Authority* a;JNIEnv* e;bool& ok;~Cleanup(){if(!ok)a->Clear(e);}} cleanup{this,env,success};
 jclass type=env->FindClass("java/lang/Class");if(!type||env->ExceptionCheck())return Fail(error,"Class API unavailable");
 struct Local{JNIEnv* e;jobject v;~Local(){if(v)e->DeleteLocalRef(v);}} local_type{env,type};
 auto get_name=env->GetMethodID(type,"getName","()Ljava/lang/String;");
 Local proxy_type{env,env->FindClass("java/lang/reflect/Proxy")};if(!proxy_type.v||env->ExceptionCheck())return Fail(error,"Public proxy classification API unavailable");
 auto is_proxy=env->GetStaticMethodID(static_cast<jclass>(proxy_type.v),"isProxyClass","(Ljava/lang/Class;)Z");
 auto parent=env->GetMethodID(type,"getSuperclass","()Ljava/lang/Class;");auto interfaces=env->GetMethodID(type,"getInterfaces","()[Ljava/lang/Class;");auto loader=env->GetMethodID(type,"getClassLoader","()Ljava/lang/ClassLoader;");auto lookup=env->GetStaticMethodID(type,"forName","(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;");
 if(!get_name||!is_proxy||!parent||!interfaces||!loader||!lookup||env->ExceptionCheck())return Fail(error,"Complete public hierarchy API unavailable");
 // Android exposes a BootClassLoader facade for a native null loader, but
 // CreateProxyClass can also store that actual loader instance. Retain its
 // public root until native identity distinguishes these two exact cases.
 Local bootstrap{env,env->FindClass("java/lang/Object")};if(!bootstrap.v||env->ExceptionCheck())return Fail(error,"Bootstrap Object public identity unavailable");
 Local boot_loader{env,env->CallObjectMethod(bootstrap.v,loader)};if(env->ExceptionCheck())return Fail(error,"Bootstrap public loader unavailable");
 jint count=0;jclass* loaded=nullptr;if(ti->GetLoadedClasses(&count,&loaded)!=JVMTI_ERROR_NONE||count<0||(count&&!loaded))return Fail(error,"Complete loaded Class graph unavailable");
 struct Loaded{JNIEnv* e;jvmtiEnv* ti;jclass* p;jint n;~Loaded(){if(p){for(jint i=0;i<n;++i)if(p[i])e->DeleteLocalRef(p[i]);ti->Deallocate(reinterpret_cast<unsigned char*>(p));}}} loaded_cleanup{env,ti,loaded,count};
 std::vector<size_t> facade_loaders;
 for(jint i=0;i<count;++i){jint status=0,flags=0;char* signature=nullptr;
  if(ti->GetClassStatus(loaded[i],&status)!=JVMTI_ERROR_NONE)return Fail(error,"Loaded Class status unavailable");if(status&(JVMTI_CLASS_STATUS_ARRAY|JVMTI_CLASS_STATUS_PRIMITIVE))continue;
  if(status&JVMTI_CLASS_STATUS_ERROR)return Fail(error,"Erroneous loaded Class graph cannot authorize changes");
  if(ti->GetClassSignature(loaded[i],&signature,nullptr)!=JVMTI_ERROR_NONE||!signature)return Fail(error,"Loaded Class descriptor unavailable");
  std::string descriptor(signature);ti->Deallocate(reinterpret_cast<unsigned char*>(signature));if(ti->GetClassModifiers(loaded[i],&flags)!=JVMTI_ERROR_NONE)return false;
  Node n;n.descriptor=std::move(descriptor);n.flags=uint32_t(flags);n.initialized=status&JVMTI_CLASS_STATUS_INITIALIZED;
  n.class_root.owner=&s.public_roots;n.class_root.generation=s.public_roots.Generation();if(!s.public_roots.Stage(context,loaded[i],&n.class_root.slot,error))return false;
  jvalue class_arg{};class_arg.l=loaded[i];const bool proxy=env->CallStaticBooleanMethodA(static_cast<jclass>(proxy_type.v),is_proxy,&class_arg);
  if(env->ExceptionCheck())return Fail(error,"Public proxy Class classification failed");
  if(proxy){
   Local name{env,env->CallObjectMethod(loaded[i],get_name)};if(!name.v||env->ExceptionCheck())return Fail(error,"Public proxy Class name unavailable");
   const auto string=static_cast<jstring>(name.v);const char*text=env->GetStringUTFChars(string,nullptr);if(!text||env->ExceptionCheck())return false;
   struct Utf{JNIEnv*env;jstring value;const char*text;~Utf(){env->ReleaseStringUTFChars(value,text);}} utf{env,string,text};
   n.proxy_name_text=text;n.negative_proxy_name=n.proxy_name_text+"!";n.proxy_name_root.owner=&s.public_roots;n.proxy_name_root.generation=s.public_roots.Generation();
   if(!s.public_roots.Stage(context,name.v,&n.proxy_name_root.slot,error))return false;
  }
  auto l=env->CallObjectMethod(loaded[i],loader);Local local_loader{env,l};if(env->ExceptionCheck())return false;
  if(l&&boot_loader.v&&env->IsSameObject(l,boot_loader.v))facade_loaders.push_back(s.graph.nodes.size());if(env->ExceptionCheck())return false;
  if(l){n.loader_root.owner=&s.public_roots;n.loader_root.generation=s.public_roots.Generation();for(const auto slot:s.loaders)if(s.public_roots.Same(env,slot,l)){n.loader_root.slot=slot;break;}
   if(env->ExceptionCheck())return false;if(!n.loader_root.slot){if(!s.public_roots.Stage(context,l,&n.loader_root.slot,error))return false;s.loaders.push_back(n.loader_root.slot);}}
  s.graph.nodes.push_back(std::move(n));
 }
 trace.Mark("public-roots",s.graph.nodes.size());
 std::unordered_map<std::string,std::vector<size_t>> descriptor_index;for(size_t i=0;i<s.graph.nodes.size();++i)descriptor_index[s.graph.nodes[i].descriptor].push_back(i);
 const auto find=[&](jobject k){char* text=nullptr;if(!k||ti->GetClassSignature(static_cast<jclass>(k),&text,nullptr)!=JVMTI_ERROR_NONE||!text)return absent;auto it=descriptor_index.find(text);ti->Deallocate(reinterpret_cast<unsigned char*>(text));if(it!=descriptor_index.end())for(auto i:it->second)if(s.public_roots.Same(env,s.graph.nodes[i].class_root.slot,k))return i;return absent;};
 for(size_t i=0;i<s.graph.nodes.size();++i){auto& n=s.graph.nodes[i];Local klass{env,s.public_roots.LocalRef(env,n.class_root.slot)};if(!klass.v||env->ExceptionCheck())return false;auto p=env->CallObjectMethod(klass.v,parent);Local lp{env,p};if(env->ExceptionCheck())return false;if(p&&(n.parent=find(p))==absent)return Fail(error,"Public ancestor outside loaded snapshot");
  auto array=static_cast<jobjectArray>(env->CallObjectMethod(klass.v,interfaces));Local la{env,array};if(!array||env->ExceptionCheck())return false;jsize length=env->GetArrayLength(array);if(length<0||env->ExceptionCheck())return false;
  for(jsize j=0;j<length;++j){auto value=env->GetObjectArrayElement(array,j);Local lv{env,value};if(!value||env->ExceptionCheck())return false;size_t index=find(value);if(index==absent)return Fail(error,"Public interface outside loaded snapshot");n.interfaces.push_back(index);}n.proposed_parent=n.parent;n.proposed_interfaces=n.interfaces;
 }
 const auto resolve=[&](const std::string& descriptor,jobject l,size_t* out){if(descriptor.size()<3||descriptor.front()!='L'||descriptor.back()!=';')return false;std::string name=descriptor.substr(1,descriptor.size()-2);std::replace(name.begin(),name.end(),'/','.');jstring text=env->NewStringUTF(name.c_str());Local lt{env,text};if(!text||env->ExceptionCheck())return false;auto klass=env->CallStaticObjectMethod(type,lookup,text,JNI_FALSE,l);Local lk{env,klass};if(!klass||env->ExceptionCheck())return false;*out=find(klass);return *out!=absent;};
 for(size_t input_index=0;input_index<inputs.size();++input_index){const auto& input=inputs[input_index];if(!input.strong_original||env->GetObjectRefType(input.strong_original)!=JNIGlobalRefType)return Fail(error,"Target must be an independently owned strong global Class");size_t n=find(input.strong_original);if(n==absent)return Fail(error,"Original Class absent from complete loaded graph");auto& node=s.graph.nodes[n];
  if(input.before.descriptor!=node.descriptor||input.after.descriptor!=node.descriptor||((input.before.access^input.after.access)&~0x410u)||(input.before.access&0x7631u)!=(node.flags&0x7631u))return Fail(error,"Class role/access changes lack an independent owner");
  node.proposed_flags=(node.flags&~0x7631u)|(input.after.access&0x7631u);
  if((node.flags&0x200u)||node.descriptor=="Ljava/lang/Object;")return Fail(error,"Interface/Object schema owner is not independently supported");
  if(node.parent==absent||input.before.superclass!=s.graph.nodes[node.parent].descriptor||input.before.interfaces.size()!=node.interfaces.size())return Fail(error,"Incoming target before hierarchy differs from public identity");
  for(size_t j=0;j<node.interfaces.size();++j)if(input.before.interfaces[j]!=s.graph.nodes[node.interfaces[j]].descriptor)return false;
  Local actual_loader{env,node.loader_root.slot?s.public_roots.LocalRef(env,node.loader_root.slot):nullptr};if(env->ExceptionCheck())return false;
  if(!resolve(input.after.superclass,actual_loader.v,&node.proposed_parent))return Fail(error,"Proposed parent cannot resolve without initialization in actual loader");node.proposed_interfaces.clear();
  for(const auto& d:input.after.interfaces){size_t index=absent;if(!resolve(d,actual_loader.v,&index))return Fail(error,"Proposed interface unavailable in exact loader");node.proposed_interfaces.push_back(index);}s.graph.inputs.push_back(n);if(node.strong_class)return Fail(error,"Duplicate source input Class");node.strong_class=static_cast<jclass>(env->NewGlobalRef(input.strong_original));if(!node.strong_class||env->ExceptionCheck())return false;s.inputs[input_index].strong_original=node.strong_class;
 }
 trace.Mark("public-edges",s.graph.nodes.size());
 // Capture only after the last public JNI operation. This view lives exactly
 // through the immediate no-JNI native borrow/graph traversal.
 State::OrdinaryRead ordinary{s};if(!ordinary.Ready())return Fail(error,"Fresh ordinary graph mappings unavailable");
 // No JNI call may follow these borrowed native reads in this Stage callback.
 for(auto& n:s.graph.nodes){if(!s.public_roots.Read(n.class_root.slot,&n.klass)||!n.klass||!s.public_roots.Read(n.loader_root.slot,&n.loader)||!s.public_roots.Read(n.proxy_name_root.slot,&n.proxy_name))return false;}
 // Select only native null or the exact independently rooted public facade.
 // Never normalize a foreign native loader into the bootstrap namespace.
 for(const auto index:facade_loaders){auto& n=s.graph.nodes[index];field_copy::CopyClassView identity;
  if(!ClassIdentity(*bindings.metadata,n,n.klass,&identity,error,s.Context()))return false;
  if(!identity.loader){n.loader=nullptr;n.loader_root={};}
  else if(identity.loader!=n.loader)return Fail(error,("Native/public Class namespace differs: "+n.descriptor).c_str());
 }
 trace.Mark("native-borrow",s.graph.nodes.size());
 if(!CanonicalizeFlags(*bindings.metadata,&s.graph,error,s.Context())||!ValidateGraph(&s.graph,error))return false;s.native_before.resize(s.graph.nodes.size());s.native_after.resize(s.graph.nodes.size());s.order_before.resize(s.graph.nodes.size());s.order_after.resize(s.graph.nodes.size());s.allocation_declarations.resize(s.graph.nodes.size());s.allocations.reserve(s.graph.nodes.size());
 trace.Mark("canonical",s.graph.nodes.size());
 // This raw identity index is local to the immediate no-JNI borrow interval.
 // Never retain it across a source callee, JNI operation or moving GC.
 {std::unordered_map<void*,size_t> native_index;native_index.reserve(s.graph.nodes.size());
 for(size_t i=0;i<s.graph.nodes.size();++i)if(!native_index.emplace(s.graph.nodes[i].klass,i).second)return Fail(error,"Duplicate current native Class identity");
 for(size_t i=0;i<s.graph.nodes.size();++i){s.closure_before.push_back(Closure(s.graph,i,false));s.closure_after.push_back(Closure(s.graph,i,true));if(!native_interface_graph::ReadInContext(*bindings.metadata,s.graph.nodes[i].klass,&s.native_before[i],error,s.Context())||!SetMatches(s.native_before[i],s.closure_before[i],s.graph,false))return Fail(error,"Native/public complete interface identities differ");for(auto k:s.native_before[i]){auto it=native_index.find(k);if(it==native_index.end())return Fail(error,"Native interface outside exact public graph");s.order_before[i].push_back(it->second);}}
 }
 trace.Mark("native-interfaces",s.graph.nodes.size());
 if(!s.Native(false,error))return false;if(!s.OwnersStaged(error))return false;trace.Mark("owners",s.graph.nodes.size());s.staged=true;success=true;return true;
}
bool Authority::CheckClass(const runnable_jni::Context& context,jclass original,const evolution::ClassSchema& schema,jvmtiError* expected_failure)noexcept{
 auto& s=*state_;State::OrdinaryRead ordinary{s};if(!ordinary.Ready()||!s.staged||s.sealed||!s.Borrow(context))return false;
 void* klass=nullptr;if(!s.bindings.borrow_owned(s.bindings.decoder_owner,context,original,&klass))return false;
 for(size_t i=0;i<s.inputs.size();++i){const auto& node=s.graph.nodes[s.graph.inputs[i]];if(node.klass!=klass)continue;
  if(!Same(s.inputs[i].after,schema)||!s.Native(false))return false;
  // Preliminary permission for one original diagnostic. Required schema guards
  // consume actual old/new Classes later; this grants no publication permission.
  if(expected_failure)*expected_failure=(node.flags^node.ProposedFlags())&0x410u?
   JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED:JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED;
  return true;
 }return false;
}
bool Authority::AuthorizeAllocation(const runnable_jni::Context& context,const Prepared& p,std::string* error){auto& s=*state_;State::OrdinaryRead ordinary{s};if(!ordinary.Ready()||!s.staged||s.sealed||p.new_class||!p.source_owner||!p.old_class||!p.cache||!p.declarations||!s.Borrow(context))return Fail(error,"Missing preallocation source owner/root proof");
 auto it=std::find_if(s.graph.nodes.begin(),s.graph.nodes.end(),[&](const auto& n){return n.klass==p.old_class;});if(it==s.graph.nodes.end()||!it->affected||it->replacement)return Fail(error,"Preallocation is outside exact affected graph");size_t index=size_t(it-s.graph.nodes.begin());
 auto target=std::find(s.graph.inputs.begin(),s.graph.inputs.end(),index);if(target!=s.graph.inputs.end()&&!Same(*p.declarations,s.inputs[size_t(target-s.graph.inputs.begin())].after))return Fail(error,"Preallocation incoming declarations differ");
 if(it->proposed_parent!=absent&&s.graph.nodes[it->proposed_parent].affected&&!s.graph.nodes[it->proposed_parent].replacement)return Fail(error,"Affected parent has not been actually linked first");
 void* parent=it->proposed_parent==absent?nullptr:(s.graph.nodes[it->proposed_parent].replacement?s.graph.nodes[it->proposed_parent].replacement:s.graph.nodes[it->proposed_parent].klass);if(parent!=p.parent||!s.Native(false))return Fail(error,"Preallocation source parent or staged native graph differs");
 return s.bindings.allocation->VerifyAllocation(s.graph,p,error)&&s.bindings.receivers->VerifyAllocation(s.graph,p,error)&&s.bindings.caches->VerifyAllocation(s.graph,p,error);
}
bool Authority::PrepareLinked(const runnable_jni::Context& context,Prepared* p,std::string* error){auto& s=*state_;
 {State::OrdinaryRead ordinary{s};
 if(!ordinary.Ready()||!p||!s.staged||s.sealed||!p->new_class||!p->source_owner||!p->source_old_root||!p->declarations||!s.Borrow(context)||!s.bindings.allocation->RefreshPending(p))return Fail(error,"Actual linked source roots absent before cache preparation");
 auto it=std::find_if(s.graph.nodes.begin(),s.graph.nodes.end(),[&](const auto& n){return n.klass==p->old_class;});
 if(it==s.graph.nodes.end()||!it->affected||it->replacement)return Fail(error,"Cache preparation is outside exact unallocated graph");
 size_t index=size_t(it-s.graph.nodes.begin());auto target=std::find(s.graph.inputs.begin(),s.graph.inputs.end(),index);
 if(target!=s.graph.inputs.end()&&!Same(*p->declarations,s.inputs[size_t(target-s.graph.inputs.begin())].after))return Fail(error,"Linked cache source declarations differ from immutable input");
 void* parent=it->proposed_parent==absent?nullptr:(s.graph.nodes[it->proposed_parent].replacement?s.graph.nodes[it->proposed_parent].replacement:s.graph.nodes[it->proposed_parent].klass);
 field_copy::CopyClassView identity;
 if(p->parent!=parent||!s.Native(false)||!s.bindings.metadata->ClassIdentity(p->new_class,&identity,error,s.Context())||identity.parent!=parent||identity.loader!=it->loader||!identity.descriptor||it->descriptor!=identity.descriptor||!s.Flags(p->new_class,it->ProposedFlags())||!s.bindings.allocation->VerifyPrepared(s.graph,*p,error))return Fail(error,"Actual captured Class differs before private cache mutation");
 }
 // Cache resolution can safepoint. Drop the view first, then capture again and
 // reborrow the actual source roots after the original cache callee returns.
 if(!s.bindings.caches->PrepareLinked(s.graph,*p,error))return false;
 State::OrdinaryRead ordinary{s};return ordinary.Ready()&&s.Borrow(context)&&s.bindings.allocation->RefreshPending(p);
}
bool Authority::VerifyPrepared(const runnable_jni::Context& context,const Prepared& p,std::string* error){auto& s=*state_;State::OrdinaryRead ordinary{s};if(!ordinary.Ready()||!s.staged||s.sealed||!p.source_owner||!p.old_class||!p.new_class||!p.cache||!p.declarations||!s.Borrow(context))return Fail(error,"Actual linked allocation proof missing");
 auto it=std::find_if(s.graph.nodes.begin(),s.graph.nodes.end(),[&](const auto& n){return n.klass==p.old_class;});if(it==s.graph.nodes.end()||!it->affected||it->replacement)return Fail(error,"Allocation does not belong to exact unallocated affected Class");size_t index=size_t(it-s.graph.nodes.begin());
 auto target=std::find(s.graph.inputs.begin(),s.graph.inputs.end(),index);if(target!=s.graph.inputs.end()){size_t i=size_t(target-s.graph.inputs.begin());if(!Same(*p.declarations,s.inputs[i].after))return Fail(error,"Allocator declarations differ from immutable staged input");}
 else if(p.declarations->descriptor!=it->descriptor||p.declarations->superclass!=(it->proposed_parent==absent?std::string{}:s.graph.nodes[it->proposed_parent].descriptor))return Fail(error,"Implicit allocation declarations differ from staged hierarchy");
 void* parent=it->proposed_parent==absent?nullptr:(s.graph.nodes[it->proposed_parent].replacement?s.graph.nodes[it->proposed_parent].replacement:s.graph.nodes[it->proposed_parent].klass);if(p.parent!=parent)return Fail(error,"Source allocator selected different rooted superclass");
 field_copy::CopyClassView identity;if(!s.Flags(p.new_class,it->ProposedFlags())||!s.bindings.metadata->ClassIdentity(p.new_class,&identity,error,s.Context())||identity.parent!=parent||identity.loader!=it->loader||!identity.descriptor||it->descriptor!=identity.descriptor)return Fail(error,"Linked Class does not match staged namespace and parent");
 // Owner verifies incoming cache's immutable DEX declarations/index and the
 // actual allocator owner; this module never substitutes a supplied schema bit.
 if(!s.bindings.allocation->VerifyPrepared(s.graph,p,error)||!s.bindings.receivers->VerifyPrepared(s.graph,p,error)||!s.bindings.caches->VerifyPrepared(s.graph,p,error))return false;
 std::vector<void*> interfaces;if(!native_interface_graph::ReadInContext(*s.bindings.metadata,p.new_class,&interfaces,error,s.Context())||!SetMatches(interfaces,s.closure_after[index],s.graph,true))return Fail(error,"Linked Class interfaces differ from exact staged graph");
 std::unordered_map<void*,size_t> native_index;native_index.reserve(s.graph.nodes.size());
 for(size_t i=0;i<s.graph.nodes.size();++i){const auto&n=s.graph.nodes[i];if(!native_index.emplace(n.replacement?n.replacement:n.klass,i).second)return false;}
 std::vector<size_t> order;order.reserve(interfaces.size());for(auto k:interfaces){auto found=native_index.find(k);if(found==native_index.end())return false;order.push_back(found->second);}
 evolution::ClassSchema declarations=*p.declarations; // All fallible work precedes publication.
 s.allocation_declarations[index]=std::move(declarations);Prepared owned=p;owned.declarations=&s.allocation_declarations[index];s.allocations.push_back(owned);s.order_after[index]=std::move(order);s.native_after[index]=std::move(interfaces);it->replacement=p.new_class;return true;
}
bool Authority::PrepareRoots(const source_commit::Plan& p,std::string* error){
 auto& s=*state_;State::OrdinaryRead ordinary{s};
 if(!ordinary.Ready()||!s.staged||s.roots_prepared||s.sealed||s.registration_started||p.old_classes.size()!=p.new_classes.size())return Fail(error,"Root preparation requires fresh complete source allocation graph");
 // Original source work and Holder JNI capture can safepoint after linked
 // allocation verification. Reborrow both public Classes and the allocator
 // captured replacement roots in this same ordinary read interval before
 // comparing the fresh Holder Plan; raw graph addresses are not GC roots.
 if(!s.Borrow(s.root_context))return Fail(error,"Root preparation could not refresh actual public/allocated source roots");
 const size_t affected=std::count_if(s.graph.nodes.begin(),s.graph.nodes.end(),[](const auto& n){return n.affected;});
 for(size_t node=0;node<s.graph.nodes.size();++node){const auto& n=s.graph.nodes[node];if(!n.affected)continue;
  size_t pairs=0,old_matches=0,new_matches=0;
  for(size_t i=0;i<p.old_classes.size();++i){const bool old=p.old_classes[i]==n.klass,fresh=p.new_classes[i]==n.replacement;old_matches+=old;new_matches+=fresh;pairs+=old&&fresh;}
  if(pairs!=1){
   // Ordinary preregistration evidence only. Do not refresh, dereference unknown
   // Plan classes, or weaken the exact bijection when reporting a mismatch.
   char detail[384];std::snprintf(detail,sizeof(detail),
    "Source Plan omitted or duplicated actual affected allocation: node=%zu descriptor=%.120s graphAffected=%zu planPairs=%zu matches=%zu oldMatches=%zu newMatches=%zu old=%p new=%p firstOld=%p firstNew=%p",
    node,n.descriptor.c_str(),affected,p.old_classes.size(),pairs,old_matches,new_matches,n.klass,n.replacement,
    p.old_classes.empty()?nullptr:p.old_classes.front(),p.new_classes.empty()?nullptr:p.new_classes.front());
   return Fail(error,detail);
  }
 }
 if(affected!=p.old_classes.size()||!s.Native(true))return Fail(error,"Root preparation lacks actual complete native replacement graph");
 // Native storage allocation happens only here, before managed registration.
 // Final Seal never writes or resizes a registered location.
 s.plan=&p;s.mirror=p.mirror;s.new_root=p.new_classes_root;s.old_classes=p.old_classes;s.new_classes=p.new_classes;
 s.old_instances=p.old_instances;s.new_instances=p.new_instances;s.origins=p.class_root_origins;s.abstract_instances=p.abstract_instances;
 s.roots_prepared=true;return true;
}
bool Authority::Seal(const source_commit::Plan& p,std::string* error){
 auto& s=*state_;
 if(!s.roots_prepared){
  // Legacy direct Seal must remain retryable after a rejected owner proof;
  // do not freeze a bad candidate into the new preregistration layout.
  if(!s.staged||s.sealed||!s.Native(true)||!s.Owners(p)||!PrepareRoots(p,error))return false;
 }
 if(s.sealed||s.plan!=&p||p.old_classes!=s.old_classes||p.new_classes!=s.new_classes||p.mirror!=s.mirror||p.new_classes_root!=s.new_root||p.old_instances!=s.old_instances||p.new_instances!=s.new_instances||p.abstract_instances!=s.abstract_instances||p.class_root_origins.size()!=s.origins.size())return Fail(error,"Source Plan changed after root-layout preparation");
 for(size_t i=0;i<s.origins.size();++i)if(p.class_root_origins[i].array!=s.origins[i].array||p.class_root_origins[i].index!=s.origins[i].index)return Fail(error,"Source origin changed after root-layout preparation");
 size_t affected=0;
 for(const auto& n:s.graph.nodes)if(n.affected){++affected;size_t found=0;for(size_t i=0;i<p.old_classes.size();++i)if(p.old_classes[i]==n.klass&&p.new_classes[i]==n.replacement)++found;if(found!=1)return Fail(error,"Refreshed Plan differs from actual affected allocation");}
 if(affected!=p.old_classes.size()||!s.Native(true)||!s.Owners(p))return Fail(error,"Complete prepared graph or native source owner proof rejected");
 s.sealed=true;return true;
}
bool Authority::Verify(const source_commit::Plan& p,size_t pair)const noexcept{const auto& s=*state_;if(!s.sealed||s.plan!=&p||pair>=p.old_classes.size()||p.old_classes.size()!=p.new_classes.size()||p.old_classes!=s.old_classes||p.new_classes!=s.new_classes||p.mirror!=s.mirror||p.new_classes_root!=s.new_root||p.old_instances!=s.old_instances||p.new_instances!=s.new_instances||p.abstract_instances!=s.abstract_instances||p.class_root_origins.size()!=s.origins.size())return false;for(size_t i=0;i<s.origins.size();++i)if(p.class_root_origins[i].array!=s.origins[i].array||p.class_root_origins[i].index!=s.origins[i].index)return false;
 size_t count=0;for(const auto& n:s.graph.nodes)if(n.affected){++count;size_t matches=0;for(size_t i=0;i<p.old_classes.size();++i)if(p.old_classes[i]==n.klass&&p.new_classes[i]==n.replacement)++matches;if(matches!=1)return false;}return count==p.old_classes.size()&&s.Native(true)&&s.Owners(p,true);
}
bool Authority::CollectManagedRoots(const prepared_roots::Registrar& registrar){auto& s=*state_;if(!s.roots_prepared||s.registration_started)return false;s.registration_started=true;for(auto& n:s.graph.nodes)if(!s.roots.Add(&n.klass,registrar)||!s.roots.Add(&n.loader,registrar)||!s.roots.Add(&n.proxy_name,registrar)||!s.roots.Add(&n.replacement,registrar))return false;for(auto* vectors:{&s.native_before,&s.native_after})for(auto& values:*vectors)for(auto& value:values)if(!s.roots.Add(&value,registrar))return false;for(auto& a:s.allocations)if(!s.roots.Add(&a.old_class,registrar)||!s.roots.Add(&a.new_class,registrar)||!s.roots.Add(&a.cache,registrar)||!s.roots.Add(&a.parent,registrar))return false;if(!s.roots.Add(&s.mirror,registrar)||!s.roots.Add(&s.new_root,registrar))return false;for(auto& origin:s.origins)if(!s.roots.Add(&origin.array,registrar))return false;for(auto* values:{&s.old_classes,&s.new_classes,&s.old_instances,&s.new_instances})for(auto& value:*values)if(!s.roots.Add(&value,registrar))return false;s.roots.Seal();s.collected=true;return true;}
bool Authority::CanRefresh(const prepared_roots::Resolver& r)const noexcept{return state_->collected&&state_->roots.CanRefresh(r);}
void Authority::Refresh(const prepared_roots::Resolver& r)noexcept{assert(CanRefresh(r));state_->roots.Refresh(r);}
void Authority::BeginCommit(const source_commit::Plan& p)const noexcept{const auto& s=*state_;assert(s.sealed&&s.plan==&p);s.bindings.allocation->BeginCommit(s.graph,p);s.bindings.receivers->BeginCommit(s.graph,p);s.bindings.caches->BeginCommit(s.graph,p);}
}
