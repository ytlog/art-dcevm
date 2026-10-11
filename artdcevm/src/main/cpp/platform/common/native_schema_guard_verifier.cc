#include "platform/common/readable_mapping_scope.h"
#include "platform/common/native_call_graph_inventory.h"
#include "platform/common/native_call_graph_dispatch.h"
#include "platform/common/native_interface_dispatch.h"
#include "platform/common/native_dex_identity.h"
#include "platform/common/native_dex_instruction_width.h"
#include "platform/common/bounded_root_cohort.h"
#include <optional>
#include "platform/common/native_schema_guard_verifier.h"
#include "platform/common/native_hierarchy_graph.h"
#include "platform/common/native_interface_domain_error_census.h"
#include "platform/common/loaded_member_guards.h"
#include "platform/common/jvmti_method_flags.h"
#include "platform/common/jvmti_field_flags.h"
#include "platform/common/proxy_method_signature.h"
#include "platform/common/method_array_view.h"
#include "platform/common/redefinition_gate.h"
#include "platform/common/rooted_schema_plan.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <tuple>
#include <unordered_map>
#include <string_view>
#include <set>
#include <limits>
#include <cstdlib>
#include <cstdio>
#if defined(__ANDROID__)
#include <android/log.h>
#endif
namespace artdcevm::native_schema_guard_verifier {
namespace {
bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
bool EnsureCallGraphBytecodes(jvmtiEnv*ti,std::string*error){
 if(!ti||!ti->functions||!ti->functions->GetCapabilities)return Fail(error,"Call graph JVMTI capabilities are unavailable");
 jvmtiCapabilities current{};
 if(ti->GetCapabilities(&current)!=JVMTI_ERROR_NONE)return Fail(error,"Call graph JVMTI capabilities query failed");
 if(current.can_get_bytecodes)return true;
 if(!ti->functions->GetPotentialCapabilities||!ti->functions->AddCapabilities)return Fail(error,"Call graph bytecode capability cannot be requested");
 jvmtiCapabilities potential{};
 if(ti->GetPotentialCapabilities(&potential)!=JVMTI_ERROR_NONE||!potential.can_get_bytecodes)return Fail(error,"Call graph bytecode capability is not available in this JVMTI phase");
 jvmtiCapabilities requested{};requested.can_get_bytecodes=1;
 if(ti->AddCapabilities(&requested)!=JVMTI_ERROR_NONE)return Fail(error,"Call graph bytecode capability request was rejected");
 current={};
 if(ti->GetCapabilities(&current)!=JVMTI_ERROR_NONE||!current.can_get_bytecodes)return Fail(error,"Call graph bytecode capability was not granted");
 return true;
}
std::vector<size_t> WalkCallGraphDomain(const std::vector<std::vector<size_t>>&children,size_t owner){
 if(owner>=children.size())return {};
 std::vector<size_t>queue{owner};std::vector<uint8_t>seen(children.size(),0);seen[owner]=1;
 for(size_t at=0;at<queue.size();++at)for(auto child:children[queue[at]]){
  if(child>=children.size())return {};if(!seen[child]){seen[child]=1;queue.push_back(child);}}
 return queue;
}
constexpr size_t absent=SIZE_MAX;
// Ephemeral index of one complete freshly borrowed snapshot. Never retain
// native keys across a JNI safepoint or a managed-root refresh.
using NativeOrdinals=std::unordered_map<void*,size_t>;
bool IndexNativeClasses(const std::vector<void*>& classes,NativeOrdinals* output,std::string* error){
 if(!output)return Fail(error,"Native Class ordinal index needs an output");
 NativeOrdinals candidate;candidate.reserve(classes.size());
 for(size_t n=0;n<classes.size();++n){
  if(!classes[n])return Fail(error,"Null staged native Class identity");
  if(!candidate.emplace(classes[n],n).second)return Fail(error,"Duplicate staged native Class identity");
 }
 *output=std::move(candidate);return true;
}

constexpr uint32_t skip_access_checks=0x00080000u,native_flag=0x100u;
struct Node {
 bounded_managed_roots::Slot root{},loader{};
 evolution::ClassSchema schema;
 std::vector<jmethodID> method_ids;
 std::vector<std::vector<unsigned char>> method_bytes;
 std::vector<jfieldID> field_ids;
 std::vector<bounded_managed_roots::Slot> interfaces;
 bounded_managed_roots::Slot parent{},component{},class_name{};
 std::string class_name_text,negative_class_name;
 bool affected=false;
};
// Immutable descriptor buckets contain indices only: every lookup still proves
// the current managed Class identity through the independently rooted JNI slot.
// No raw managed address or ClassLoader hash survives a safepoint here.
struct PublicClassIndex {
 std::map<std::string,std::vector<size_t>> buckets;
 void Add(const std::string& descriptor,size_t index){buckets[descriptor].push_back(index);}
 enum class Result {Missing,Unique,Duplicate};
 template<class Match> Result Find(const std::string& descriptor,Match match,size_t* index)const {
  *index=absent;auto found=buckets.find(descriptor);if(found==buckets.end())return Result::Missing;
  for(size_t candidate:found->second)if(match(candidate)){if(*index!=absent)return Result::Duplicate;*index=candidate;}
  return *index==absent?Result::Missing:Result::Unique;
 }
};
struct Snapshot {
 std::vector<void*> classes,loaders;
 std::vector<size_t> parents;
 std::vector<std::vector<void*>> declared,all_methods;
 std::vector<proxy_method_signature::Witness> proxy_signatures;
};
// Public interface getSuperclass is null; ART ClassLinker retains bootstrap
// Object in the native super_class field. Preserve both identities instead of
// changing the public parent graph or exempting interfaces from native proof.
bool NativeSuperclass(const std::vector<Node>& nodes,const Snapshot& roots,size_t index,void** out)noexcept{
 if(!out||index>=nodes.size()||roots.classes.size()!=nodes.size()||roots.loaders.size()!=nodes.size()||roots.parents.size()!=nodes.size())return false;
 const auto parent=roots.parents[index];
 if(nodes[index].schema.access&0x200u){
  if(parent!=absent)return false;void* object=nullptr;
  for(size_t i=0;i<nodes.size();++i)if(nodes[i].schema.descriptor=="Ljava/lang/Object;"&&!roots.loaders[i]){
   if(object||!roots.classes[i]||(nodes[i].schema.access&0x200u))return false;object=roots.classes[i];}
  if(!object)return false;*out=object;return true;
 }
 if(parent==absent){*out=nullptr;return true;}
 if(parent>=nodes.size()||!roots.classes[parent])return false;*out=roots.classes[parent];return true;
}
bool SamePlan(const source_commit::Plan& a,const source_commit::Plan& b){
 if(a.mirror!=b.mirror||a.new_classes_root!=b.new_classes_root||a.abstract_instances!=b.abstract_instances||a.old_classes!=b.old_classes||a.new_classes!=b.new_classes||a.old_instances!=b.old_instances||a.new_instances!=b.new_instances||a.class_root_origins.size()!=b.class_root_origins.size())return false;
 for(size_t i=0;i<a.class_root_origins.size();++i)if(a.class_root_origins[i].array!=b.class_root_origins[i].array||a.class_root_origins[i].index!=b.class_root_origins[i].index)return false;
 return true;
}
bool CallerClass(jint status){return !(status&JVMTI_CLASS_STATUS_ERROR);}
bool PreparedCaller(jint status){return (status&(JVMTI_CLASS_STATUS_ARRAY|JVMTI_CLASS_STATUS_PRIMITIVE))||(status&JVMTI_CLASS_STATUS_PREPARED);}
// This is a rejectable, ordinary-JNI window. Report public identity only;
// descriptors do not grant membership in the staged caller graph.
size_t DiagnosticPrefix(const char* identity,size_t limit){
 size_t length=0;while(length<limit&&identity[length])++length;
 while(length&&identity[length]&&(static_cast<unsigned char>(identity[length])&0xc0u)==0x80u)--length;
 return length;
}
bool ClassWindowFailure(std::string* error,const char* reason,const char* descriptor,jint status){
 if(error){
  const char* identity=descriptor?descriptor:"<unavailable>";
  const size_t length=DiagnosticPrefix(identity,384);
  char text[768];std::snprintf(text,sizeof(text),"%s [descriptor=%.*s status=0x%08x]",reason,int(length),identity,static_cast<unsigned>(status));*error=text;
 }
 return false;
}
bool MethodWindowProof(bool mapped,bool same_owner,uint32_t raw,uint32_t public_flags)noexcept {
 return mapped&&same_owner&&jvmti_method_flags::Public(raw)==public_flags;
}
bool MethodWindowFailure(std::string* error,const evolution::ClassSchema& klass,const evolution::Member& method,bool mapped,const char* native_owner,uint32_t raw){
 if(error){
  const auto& name=method.name;const auto& signature=method.descriptor;const auto& descriptor=klass.descriptor;
  const char* owner=native_owner?native_owner:"<outside-rooted-graph>";char text[768];
  std::snprintf(text,sizeof(text),"Public/native method owner or flags changed after staging [class=%.*s method=%.*s%.*s mapped=%d owner=%.*s raw=0x%08x projected=0x%08x public=0x%08x]",
   int(DiagnosticPrefix(descriptor.c_str(),160)),descriptor.c_str(),int(DiagnosticPrefix(name.c_str(),96)),name.c_str(),int(DiagnosticPrefix(signature.c_str(),160)),signature.c_str(),int(mapped),int(DiagnosticPrefix(owner,96)),owner,raw,jvmti_method_flags::Public(raw),method.access);
  *error=text;
 }
 return false;
}
bool ClassIdentityFailure(std::string* error,const char* descriptor,const char* kind,bool mapped,bool identity,bool same_descriptor,bool same_loader,const char* detail,const char* native_descriptor=nullptr){
 if(error){char text[768];const char* reason=detail&&*detail?detail:"<no native identity error>";
  std::snprintf(text,sizeof(text),"Public/native Class descriptor or loader changed [class=%.*s kind=%s mapped=%d identity=%d descriptorMatch=%d loaderMatch=%d nativeDescriptor=%.*s nativeError=%.*s]",
   int(DiagnosticPrefix(descriptor,192)),descriptor,kind,int(mapped),int(identity),int(same_descriptor),int(same_loader),int(DiagnosticPrefix(native_descriptor?native_descriptor:"<unavailable>",96)),native_descriptor?native_descriptor:"<unavailable>",int(DiagnosticPrefix(reason,256)),reason);*error=text;}
 return false;
}
bool FieldWindowFailure(std::string* error,const evolution::ClassSchema& klass,const evolution::Member& field,
 bool mapped,bool owner,bool text,bool name,bool descriptor,uint32_t raw,const char* detail){
 if(error){char message[1024];const char* why=detail&&*detail?detail:"<none>";
  std::snprintf(message,sizeof(message),"Contextual public/native field identity or flags changed after staging [class=%.*s field=%.*s:%.*s mapped=%d ownerMatch=%d text=%d nameMatch=%d descriptorMatch=%d raw=0x%08x public=0x%08x nativeError=%.*s]",
   int(DiagnosticPrefix(klass.descriptor.c_str(),192)),klass.descriptor.c_str(),int(DiagnosticPrefix(field.name.c_str(),96)),field.name.c_str(),
   int(DiagnosticPrefix(field.descriptor.c_str(),160)),field.descriptor.c_str(),int(mapped),int(owner),int(text),int(name),int(descriptor),raw,field.access,
   int(DiagnosticPrefix(why,256)),why);*error=message;
 }return false;
}
struct MemberKey {
 std::string_view name,descriptor;uint32_t flags;
 bool operator==(const MemberKey& other)const{return name==other.name&&descriptor==other.descriptor&&flags==other.flags;}
};
struct MemberKeyHash {
 size_t operator()(const MemberKey& key)const noexcept{
  size_t value=std::hash<std::string_view>{}(key.name);
  const auto mix=[&](size_t next){value^=next+size_t(0x9e3779b9u)+(value<<6)+(value>>2);};
  mix(std::hash<std::string_view>{}(key.descriptor));mix(std::hash<uint32_t>{}(key.flags));return value;
 }
};
MemberKey ExactMemberKey(const evolution::Member& member){return {member.name,member.descriptor,native_schema_policy::JavaFlags(member.access)};}
bool MembersEqual(const std::vector<evolution::Member>& a,const std::vector<evolution::Member>& b){
 if(a.size()!=b.size())return false;
 // Preserve the original exact count==1 proof (including duplicate refusal),
 // with keys borrowing these two immutable declarations only for this call.
 std::unordered_map<MemberKey,size_t,MemberKeyHash> counts;counts.reserve(b.size());
 for(const auto& member:b)++counts[ExactMemberKey(member)];
 return std::all_of(a.begin(),a.end(),[&](const auto& member){const auto found=counts.find(ExactMemberKey(member));return found!=counts.end()&&found->second==1;});
}
std::string Package(const std::string& descriptor){const auto slash=descriptor.rfind('/');return slash==std::string::npos?std::string{}:descriptor.substr(0,slash);}
bool Virtual(const evolution::Member& method){return !(method.access&(8u|2u))&&method.name!="<init>"&&method.name!="<clinit>";}
template<class T>T Read(const void* p,size_t offset=0){T value;std::memcpy(&value,static_cast<const unsigned char*>(p)+offset,sizeof(value));return value;}
// Native methods arrays are identical named fields in all three fixed source
// declarations. Public exact JNI method identities prove the stride and header;
// copied entries must match a public declaration at their actual native owner.
static_assert(offsetof(class_metadata::Class11,methods)==48&&offsetof(class_metadata::Class16,methods)==48&&offsetof(class_metadata::Class17,methods)==48);
static_assert(offsetof(class_metadata::Class11,iftable)==24&&offsetof(class_metadata::Class16,iftable)==24&&offsetof(class_metadata::Class17,iftable)==24);
struct Strings {jvmtiEnv* ti;char* name=nullptr;char* signature=nullptr;char* generic=nullptr;~Strings(){for(auto p:{name,signature,generic})if(p)ti->Deallocate(reinterpret_cast<unsigned char*>(p));}};
bool PublicMembers(JNIEnv* env,jvmtiEnv* ti,jclass klass,Node* out,std::string* error,const Bindings* bindings=nullptr){
 Strings klass_text{ti};jint modifiers=0;
 if(ti->GetClassSignature(klass,&klass_text.signature,nullptr)!=JVMTI_ERROR_NONE||!klass_text.signature||ti->GetClassModifiers(klass,&modifiers)!=JVMTI_ERROR_NONE)return Fail(error,"Public Class declaration unavailable");
 out->schema.descriptor=klass_text.signature;out->schema.access=native_schema_policy::JavaFlags(uint32_t(modifiers));
 jint method_count=0; jmethodID* methods=nullptr;
 if(ti->GetClassMethods(klass,&method_count,&methods)!=JVMTI_ERROR_NONE||method_count<0)return Fail(error,"Complete public declared method IDs unavailable");
 struct Methods {jvmtiEnv* ti;jmethodID* p;~Methods(){if(p)ti->Deallocate(reinterpret_cast<unsigned char*>(p));}} release_methods{ti,methods};
 for(jint i=0;i<method_count;++i){Strings text{ti};jint flags=0;
  if(!methods[i]||ti->GetMethodName(methods[i],&text.name,&text.signature,nullptr)!=JVMTI_ERROR_NONE||!text.name||!text.signature||ti->GetMethodModifiers(methods[i],&flags)!=JVMTI_ERROR_NONE)return Fail(error,"Public method identity/modifiers unavailable");
  std::vector<unsigned char> bytes;
  if(!(flags&0x500)){
   if(!ti->functions||!ti->functions->GetBytecodes)return Fail(error,"Call graph public GetBytecodes is unavailable");
   jint count=0;unsigned char*raw=nullptr;
   const auto result=ti->GetBytecodes(methods[i],&count,&raw);
   struct Bytes {jvmtiEnv*ti;unsigned char*p;~Bytes(){if(p)ti->Deallocate(p);}} release{ti,raw};
   if(result!=JVMTI_ERROR_NONE||count<=0||(count&1)||!raw)return Fail(error,"Call graph public DEX bytecodes unavailable");
   bytes.assign(raw,raw+count);
  }
  out->method_bytes.push_back(std::move(bytes));
  out->method_ids.push_back(methods[i]);out->schema.methods.push_back({text.name,text.signature,native_schema_policy::JavaFlags(uint32_t(flags)),UINT32_MAX});
 }
 jint field_count=0; jfieldID* fields=nullptr;
 if(ti->GetClassFields(klass,&field_count,&fields)!=JVMTI_ERROR_NONE||field_count<0)return Fail(error,"Complete public declared field IDs unavailable");
 struct Fields {jvmtiEnv* ti;jfieldID* p;~Fields(){if(p)ti->Deallocate(reinterpret_cast<unsigned char*>(p));}} release_fields{ti,fields};
 for(jint i=0;i<field_count;++i){Strings text{ti};jint flags=0;
  if(!fields[i]||ti->GetFieldName(klass,fields[i],&text.name,&text.signature,nullptr)!=JVMTI_ERROR_NONE||!text.name||!text.signature||ti->GetFieldModifiers(klass,fields[i],&flags)!=JVMTI_ERROR_NONE)return Fail(error,"Public field identity/modifiers unavailable");
  if(bindings&&(!bindings->stage_field||!bindings->stage_field(bindings->owner,env,klass,fields[i],(flags&8)!=0,error))){
   if(error){const std::string cause=*error;
    *error="Public contextual Field stage [class="+out->schema.descriptor+" field="+text.name+" type="+text.signature+"] "+cause;}
   return false;
  }
  out->field_ids.push_back(fields[i]);out->schema.fields.push_back({text.name,text.signature,native_schema_policy::JavaFlags(uint32_t(flags)),UINT32_MAX});
 }
 if(env->ExceptionCheck())return Fail(error,"Public schema stage left a pending exception");
 if(std::set<jmethodID>(out->method_ids.begin(),out->method_ids.end()).size()!=out->method_ids.size()||std::set<jfieldID>(out->field_ids.begin(),out->field_ids.end()).size()!=out->field_ids.size())return Fail(error,"Duplicate public member IDs");
 return true;
}
bool SamePublic(const Node& a,const Node& b){return a.schema.descriptor==b.schema.descriptor&&a.schema.access==b.schema.access&&MembersEqual(a.schema.fields,b.schema.fields)&&MembersEqual(a.schema.methods,b.schema.methods)&&a.method_ids==b.method_ids&&a.method_bytes==b.method_bytes&&a.field_ids==b.field_ids;}
}
struct Verifier::State {
 const modern_field_metadata::ReadContext* stopped_context=nullptr;
 mutable size_t ordinary_mapping_queries=0;
 const HierarchyAuthority* hierarchy_authority=nullptr;
 native_interface_dispatch_domain::detail::ErrorCensus error_census;
 mutable std::vector<uint64_t> domain_seen;mutable uint64_t domain_epoch=0;
 Bindings bindings{};PublicClassIndex public_index;std::vector<Node> nodes;std::vector<bounded_managed_roots::Slot> inputs;bounded_managed_roots::Store roots;
 uint64_t census_revision=0;
 bool staged=false,verified=false,collected=false,sealed=false,interfaces_proven=false;
 source_commit::Plan expected{};
 mutable std::vector<uint32_t*> access_slots,readonly_access_slots;
 mutable bool access_proven=false,final_fields_proven=false,aliases_proven=false;
 struct Grant {size_t pair;void* loader;native_schema_policy::Requirement requirement;};
 struct ClassProof {void* methods=nullptr;void* instances=nullptr;void* statics=nullptr;void* dex_cache=nullptr;void* interfaces=nullptr;uint32_t flags=0,kind=0,primitive=0,instance_count=0,static_count=0;std::vector<void*> interface_classes;};
 struct MemberProof {void* member;size_t owner;uint32_t flags,index,offset;bool field;jfieldID field_id=nullptr;};
 struct InterfaceProof {void* klass=nullptr;void* table=nullptr;std::vector<void*> values;};
 Snapshot frozen_snapshot;std::vector<ClassProof> class_proofs;std::vector<MemberProof> member_proofs;std::vector<InterfaceProof> replacement_interfaces;
 mutable const Snapshot* batch_snapshot=nullptr;
 mutable const std::vector<native_schema_policy::GuardRequest>* preparing_requests=nullptr;
 mutable const source_commit::Plan* requirements_plan=nullptr;
 mutable std::vector<native_schema_policy::GuardRequest> required_requests;
 bool HierarchyRequired()const noexcept {
  const auto& requests=preparing_requests?*preparing_requests:required_requests;
  return std::any_of(requests.begin(),requests.end(),[](const auto& r){return r.requirement.kind==native_schema_policy::GuardKind::HierarchyGraph;});
 }
 bool CompleteRequests(const std::vector<native_schema_policy::GuardRequest>& requests)const noexcept {
  if(requests.size()!=required_requests.size())return false;
  for(size_t i=0;i<requests.size();++i){const auto&a=requests[i];const auto&b=required_requests[i];
   if(a.class_pair!=b.class_pair||a.loader!=b.loader||a.requirement.kind!=b.requirement.kind||
      a.requirement.member_name!=b.requirement.member_name||a.requirement.descriptor!=b.requirement.descriptor)return false;}
  return true;
 }
 mutable std::vector<Grant> grants;prepared_roots::Bindings frozen_roots,expected_roots;bool frozen=false,refreshed=false;
 bool FrozenProof()const noexcept {
  if(!frozen||!redefinition_gate::Owned(bindings.vm)||class_proofs.size()!=nodes.size()||frozen_snapshot.classes.size()!=nodes.size())return false;
  const bool split=bindings.metadata->StorageModel()==modern_field_metadata::Model::kSplit;
  const auto& layout=bindings.methods->model==method_identity::Model::kMethod11?method_metadata::kAndroid11:method_metadata::kAndroid12Onward;
  for(size_t n=0;n<nodes.size();++n){void* klass=nullptr;const auto& proof=class_proofs[n];
   if(!roots.Read(nodes[n].root,&klass)||klass!=frozen_snapshot.classes[n])return false;
   void* loader=nullptr;if(nodes[n].loader&&!roots.Read(nodes[n].loader,&loader))return false;
   if(loader!=frozen_snapshot.loaders[n]||bindings.metadata->Reference(klass,offsetof(class_metadata::Class11,class_loader))!=loader)return false;
   void* component=nullptr;if(nodes[n].component&&!roots.Read(nodes[n].component,&component))return false;
   if(bindings.metadata->Reference(klass,offsetof(class_metadata::Class11,component_type))!=component)return false;
   if(nodes[n].class_name){void* name=nullptr;if(!roots.Read(nodes[n].class_name,&name)||bindings.metadata->Reference(klass,offsetof(class_metadata::Class11,name))!=name)return false;}
   void* parent=nullptr;if(!NativeSuperclass(nodes,frozen_snapshot,n,&parent)||bindings.metadata->Reference(klass,offsetof(class_metadata::Class11,super_class))!=parent)return false;
   const size_t flags=split?offsetof(class_metadata::Class11,access_flags):offsetof(class_metadata::Class17,access_flags);
   if((Read<uint32_t>(klass,flags)&0x40000u)!=proof.kind||Read<uint32_t>(klass,split?offsetof(class_metadata::Class11,primitive_type):offsetof(class_metadata::Class17,primitive_type))!=proof.primitive)return false;
   if(native_schema_policy::JavaFlags(__atomic_load_n(reinterpret_cast<uint32_t*>(static_cast<char*>(klass)+flags),__ATOMIC_RELAXED))!=proof.flags||
      Read<uint64_t>(klass,offsetof(class_metadata::Class11,methods))!=uintptr_t(proof.methods)||
      (proof.methods?Read<uint32_t>(proof.methods):0)!=frozen_snapshot.all_methods[n].size()||
      Read<uint64_t>(klass,40)!=uintptr_t(proof.instances)||(split&&Read<uint64_t>(klass,56)!=uintptr_t(proof.statics))||
      (proof.instances?Read<uint32_t>(proof.instances):0)!=proof.instance_count||(proof.statics?Read<uint32_t>(proof.statics):0)!=proof.static_count||
      bindings.metadata->Reference(klass,offsetof(class_metadata::Class11,dex_cache))!=proof.dex_cache||
      bindings.metadata->Reference(klass,offsetof(class_metadata::Class11,iftable))!=proof.interfaces)return false;
   if(proof.interfaces){if(Read<uint32_t>(proof.interfaces,8)!=2*proof.interface_classes.size())return false;
    for(size_t i=0;i<proof.interface_classes.size();++i)if(bindings.metadata->Reference(proof.interfaces,12+8*i)!=proof.interface_classes[i])return false;}
   for(size_t i=0;i<nodes[n].method_ids.size();++i)if(bindings.decode_method(bindings.owner,nodes[n].method_ids[i])!=frozen_snapshot.declared[n][i])return false;
  }
  for(const auto& witness:frozen_snapshot.proxy_signatures)if(!witness.Unchanged())return false;
  for(const auto& proof:replacement_interfaces){if(bindings.metadata->Reference(proof.klass,24)!=proof.table)return false;
   if(proof.table){if(Read<uint32_t>(proof.table,8)!=2*proof.values.size())return false;
    for(size_t n=0;n<proof.values.size();++n)if(bindings.metadata->Reference(proof.table,12+8*n)!=proof.values[n])return false;}}
  for(auto slot:readonly_access_slots)if(__atomic_load_n(slot,__ATOMIC_RELAXED)&skip_access_checks)return false;
  for(const auto& proof:member_proofs){void* owner=frozen_snapshot.classes[proof.owner];
   if(proof.field){const auto* field=static_cast<const abi::arm64::Field*>(proof.member);
    if(bindings.decode_native_field(bindings.owner,owner,proof.field_id)!=proof.member||bindings.metadata->FieldDeclaringClass(proof.member)!=owner||jvmti_field_flags::Public(field_array::Flags(proof.member))!=proof.flags||field->dex_field_index!=proof.index||field->offset!=proof.offset)return false;
   }else{method_metadata::Identity value;
    if(bindings.method_class(bindings.owner,proof.member)!=owner||!method_metadata::ReadIdentity(layout,proof.member,layout.size,&value)||jvmti_method_flags::Public(value.access_flags)!=proof.flags||value.dex_method_index!=proof.index)return false;}
  }
  return true;
 }
 bool Roots(Snapshot* out,std::string* error)const{
  struct Diagnostics {std::string* error;uint64_t before;bool success=false;~Diagnostics(){
   if(!success&&error&&!error->empty()){char suffix[64];std::snprintf(suffix,sizeof(suffix)," [mappingOsQueries=%llu]",static_cast<unsigned long long>(method_identity::MappingOsQueries()-before));try{error->append(suffix);}catch(const std::bad_alloc&){}}
  }} diagnostics{error,method_identity::MappingOsQueries()};
  const modern_field_metadata::ReadContext read_context{bindings.owner,bindings.extent};
  // This traversal invokes only borrowed native root/metadata readers: no JNI
  // operation or managed safepoint follows until it returns. Capture one fresh
  // ordinary mapping view here instead of querying the OS for every Class,
  // Field, DEX and method in the complete graph. Explicit stopped/owner views
  // retain precedence, including denial; never recapture to bypass a refused
  // provided view or retain this ordinary snapshot across a source callee.
  ordinary_mapping_queries=0;
  std::optional<method_identity::ScopedReadableMappings> ordinary;
  modern_field_metadata::ReadContext ordinary_context{};
  if(!stopped_context&&!bindings.extent){
   ordinary.emplace();ordinary_mapping_queries=ordinary->OsQueries();if(!ordinary->Ready())return Fail(error,"Fresh ordinary schema root mapping capture failed");
   ordinary_context={&*ordinary,method_identity::ScopedReadableMappings::ReadExtent};
  }
  const auto* context=stopped_context?stopped_context:bindings.extent?&read_context:&ordinary_context;
  const auto Mapped=[&](const void* address,size_t bytes){return address&&bindings.readable&&bindings.readable(bindings.owner,address,bytes);};
  if(!redefinition_gate::Owned(bindings.vm)||!bindings.metadata||!bindings.metadata->Calibrated()||!bindings.methods||bindings.methods->identities.size()<3||bindings.methods->readable_extents.size()!=bindings.methods->identities.size()||!bindings.decode_global||!bindings.decode_method||!bindings.decode_native_field||!bindings.method_class||!bindings.same_method||!bindings.readable||!bindings.writable||!bindings.metadata||!bindings.methods)return Fail(error,"Schema verifier lacks the calibrated native process owner");
  Snapshot result;result.classes.resize(nodes.size());result.loaders.resize(nodes.size());result.parents.resize(nodes.size(),absent);result.declared.resize(nodes.size());result.all_methods.resize(nodes.size());
  for(size_t n=0;n<nodes.size();++n){
   if(!roots.Read(nodes[n].root,&result.classes[n])||!result.classes[n])return Fail(error,"Staged Class strong root could not be decoded");
   if(nodes[n].loader&&!roots.Read(nodes[n].loader,&result.loaders[n]))return Fail(error,"Staged ClassLoader strong root could not be decoded");

  }
  NativeOrdinals class_index;if(!IndexNativeClasses(result.classes,&class_index,error))return false;
  const auto& layout=bindings.methods->model==method_identity::Model::kMethod11?method_metadata::kAndroid11:method_metadata::kAndroid12Onward;
  for(size_t n=0;n<nodes.size();++n){const auto& node=nodes[n];void* klass=result.classes[n];
   if(node.parent){void* parent=nullptr;if(!roots.Read(node.parent,&parent))return false;auto it=class_index.find(parent);if(it==class_index.end())return Fail(error,"Staged ancestor is outside complete loaded roots");result.parents[n]=it->second;}
   field_copy::CopyClassView identity;
   std::string identity_error;void* terminal=klass;size_t dimension=0;
   while(dimension<node.schema.descriptor.size()&&node.schema.descriptor[dimension]=='['){
    if(!Mapped(terminal,offsetof(class_metadata::Class11,component_type)+4))return Fail(error,"Array component chain has unreadable Class prefix");
    terminal=bindings.metadata->Reference(terminal,offsetof(class_metadata::Class11,component_type));++dimension;
   }
   void* proxy_name=nullptr;const char* name=nullptr;const char* negative=nullptr;
   const auto terminal_node=class_index.find(terminal);
   if(terminal_node!=class_index.end()){const auto& source=nodes[terminal_node->second];
    if(source.class_name){if(!roots.Read(source.class_name,&proxy_name))return Fail(error,"Public proxy name root could not be decoded");name=source.class_name_text.c_str();negative=source.negative_class_name.c_str();}}
   const bool identified=bindings.metadata->ClassIdentityVerified(klass,node.schema.descriptor.c_str(),proxy_name,name,negative,&identity,&identity_error,context);
   const bool same_descriptor=identified&&identity.descriptor&&node.schema.descriptor==identity.descriptor;
   const bool same_loader=identified&&identity.loader==result.loaders[n];
   if(!identified||!same_descriptor||!same_loader){
    const bool split=bindings.metadata->StorageModel()==modern_field_metadata::Model::kSplit;
    const bool mapped=Mapped(klass,split?sizeof(class_metadata::Class11):sizeof(class_metadata::Class17));
    const uint32_t flags=mapped?Read<uint32_t>(klass,split?offsetof(class_metadata::Class11,access_flags):offsetof(class_metadata::Class17,access_flags)):0;
    const bool array=mapped&&bindings.metadata->Reference(klass,offsetof(class_metadata::Class11,component_type));
    const char* kind=!mapped?"unmapped":array?"array":(flags&0x40000u)?"proxy":(flags&0x200u)?"interface":"ordinary";
    return ClassIdentityFailure(error,node.schema.descriptor.c_str(),kind,mapped,identified,same_descriptor,same_loader,identity_error.c_str(),identified?identity.descriptor:nullptr);
   }
   void* expected_parent=nullptr;if(!NativeSuperclass(nodes,result,n,&expected_parent))return Fail(error,"Native superclass lacks exact public parent/bootstrap Object proof");
   if(identity.parent!=expected_parent)return Fail(error,"Native superclass differs from public rooted identity");
   void* public_component=nullptr;if(node.component&&!roots.Read(node.component,&public_component))return false;
   if(bindings.metadata->Reference(klass,offsetof(class_metadata::Class11,component_type))!=public_component)return Fail(error,"Native array component differs from independent public Class identity");
   if(node.class_name&&bindings.metadata->Reference(klass,offsetof(class_metadata::Class11,name))!=proxy_name)return Fail(error,"Native proxy cached name differs from public String identity");
   for(size_t i=0;i<node.field_ids.size();++i){
    void* field=bindings.decode_native_field(bindings.owner,klass,node.field_ids[i]);field_copy::CopyFieldView identity;
    const bool mapped=Mapped(field,sizeof(field_array::Field));
    const bool owner=mapped&&bindings.metadata->FieldDeclaringClass(field)==klass;
    std::string native_error;const bool text=owner&&bindings.metadata->FieldIdentity(field,&identity,&native_error,context);
    const bool name=text&&identity.name&&node.schema.fields[i].name==identity.name;
    const bool descriptor=text&&identity.descriptor&&node.schema.fields[i].descriptor==identity.descriptor;
    const uint32_t raw=mapped?field_array::Flags(field):0;
    if(!name||!descriptor||jvmti_field_flags::Public(raw)!=node.schema.fields[i].access)
     return FieldWindowFailure(error,node.schema,node.schema.fields[i],mapped,owner,text,name,descriptor,raw,native_error.c_str());
   }
   for(size_t i=0;i<node.method_ids.size();++i){void* method=bindings.decode_method(bindings.owner,node.method_ids[i]);
    const bool mapped=Mapped(method,layout.size);void* owner=mapped?bindings.method_class(bindings.owner,method):nullptr;
    const uint32_t raw=mapped?method_metadata::ReadAccessFlags(method):0;
    if(!MethodWindowProof(mapped,owner==klass,raw,node.schema.methods[i].access)){
     const char* owner_name=mapped?nullptr:"<unmapped>";const auto index=class_index.find(owner);
     if(index!=class_index.end())owner_name=nodes[index->second].schema.descriptor.c_str();
     return MethodWindowFailure(error,node.schema,node.schema.methods[i],mapped,owner_name,raw);
    }
    result.declared[n].push_back(method);
   }
  }
  std::map<void*,proxy_method_signature::Declaration> arena;
  const auto Admit=[&](void* method,size_t n,size_t i){
   const auto& member=nodes[n].schema.methods[i];
   return arena.emplace(method,proxy_method_signature::Declaration{member.name,member.descriptor,bool(nodes[n].class_name)}).second;
  };
  for(size_t n=0;n<nodes.size();++n)for(size_t i=0;i<result.declared[n].size();++i)
   if(!Admit(result.declared[n][i],n,i))return Fail(error,"Duplicate native method has conflicting public declarations");
  size_t declared_total=0;for(const auto& list:result.declared)declared_total+=list.size();
  for(size_t n=0;n<nodes.size();++n){void* klass=result.classes[n];
   if(!Mapped(klass,56))return Fail(error,"Native Class methods prefix is unreadable");
   void* array=reinterpret_cast<void*>(uintptr_t(Read<uint64_t>(klass,48)));uint32_t count=0;
   if(array){if(reinterpret_cast<uintptr_t>(array)%8||!Mapped(array,8))return Fail(error,"Native methods arena header is invalid");count=Read<uint32_t>(array);}
   if(count<result.declared[n].size()||count>declared_total||count>(SIZE_MAX-8)/layout.size||(array&&!Mapped(array,8+size_t(count)*layout.size)))return Fail(error,"Native methods arena extent/count lacks complete public provenance");
   std::set<void*> found;
   for(uint32_t i=0;i<count;++i){void* method=static_cast<unsigned char*>(array)+8+size_t(i)*layout.size;result.all_methods[n].push_back(method);
    if(std::find(result.declared[n].begin(),result.declared[n].end(),method)!=result.declared[n].end()){found.insert(method);continue;}
    void* owner=bindings.method_class(bindings.owner,method);auto cls=class_index.find(owner);
    if(cls==class_index.end()||owner==klass)return Fail(error,"Nondeclared native method lacks foreign copied-member ownership");
    const size_t source=cls->second;
    // A proxy source must first be normalized using its own independently
    // proved data witness. Do not call the DEX comparator on a raw proxy.
    if(nodes[source].class_name)return Fail(error,"Copied native method has an unproved proxy source");
    bool match=false;size_t public_index=0;
    for(size_t j=0;j<result.declared[source].size();++j)if(bindings.same_method(bindings.owner,method,result.declared[source][j])){
     if(match)return Fail(error,"Copied native method matches ambiguous public declarations");match=true;public_index=j;}
    if(!match||!Admit(method,source,public_index))return Fail(error,"Copied native method has no unique independent public declaration");
   }
   if(found.size()!=result.declared[n].size())return Fail(error,"Public method ID is outside its source-declared native arena");
  }
  bool distinct_signature=false,shared_signature=false;
  struct Signature {void* method;size_t node,index;};
  std::map<std::pair<std::string,std::string>,Signature> signatures;
  const auto Failure=[&](const char* reason,size_t n,size_t i,const Signature* other){
   if(error){const auto& member=nodes[n].schema.methods[i];char text[768];
    const char* previous=other?nodes[other->node].schema.descriptor.c_str():"<none>";
    std::snprintf(text,sizeof(text),"%s [class=%.*s method=%.*s%.*s proxy=%d previousClass=%.*s]",reason,
     int(DiagnosticPrefix(nodes[n].schema.descriptor.c_str(),160)),nodes[n].schema.descriptor.c_str(),
     int(DiagnosticPrefix(member.name.c_str(),96)),member.name.c_str(),int(DiagnosticPrefix(member.descriptor.c_str(),160)),member.descriptor.c_str(),
     int(bool(nodes[n].class_name)),int(DiagnosticPrefix(previous,160)),previous);*error=text;
   }return false;
  };
  for(size_t n=0;n<nodes.size();++n)for(size_t i=0;i<result.declared[n].size();++i){
   void* method=result.declared[n][i];const auto& member=nodes[n].schema.methods[i];
   if(nodes[n].class_name){proxy_method_signature::Witness witness;
    if(!proxy_method_signature::Resolve(layout,method,member.name,member.descriptor,arena,&witness))
     return Failure("Proxy signature source lacks independent public/native arena provenance",n,i,nullptr);
    result.proxy_signatures.push_back(witness);method=witness.source;
   }
   if(!bindings.same_method(bindings.owner,method,method))return Failure("Named signature comparator rejected a public self identity",n,i,nullptr);
   auto key=std::make_pair(member.name,member.descriptor);auto previous=signatures.find(key);
   if(previous!=signatures.end()&&previous->second.method!=method){
    if(!bindings.same_method(bindings.owner,method,previous->second.method))return Failure("Named signature comparator differs from independent matching public declarations",n,i,&previous->second);
    shared_signature=true;
   }
   if(!signatures.empty()&&signatures.begin()->first!=key){
    if(bindings.same_method(bindings.owner,method,signatures.begin()->second.method))return Failure("Named signature comparator accepted independently different public declarations",n,i,&signatures.begin()->second);
    distinct_signature=true;
   }
   signatures.emplace(std::move(key),Signature{method,n,i});
  }
  if(!distinct_signature||!shared_signature)return Fail(error,"Whole loaded graph lacks positive and negative signature comparator oracles");
  *out=std::move(result);diagnostics.success=true;return true;
 }
 bool Interfaces(const Snapshot& roots,const source_commit::Plan& transaction,std::string* error)const{
  const auto Mapped=[&](const void* address,size_t bytes){return address&&bindings.readable&&bindings.readable(bindings.owner,address,bytes);};
  if(!interfaces_proven)return Fail(error,"Native interface table lacks public positive/negative calibration");
  NativeOrdinals class_index;if(!IndexNativeClasses(roots.classes,&class_index,error))return false;
  const auto mapped=[&](void* value){for(size_t i=0;i<transaction.old_classes.size();++i)if(transaction.old_classes[i]==value)return transaction.new_classes[i];return value;};
  for(size_t n=0;n<nodes.size();++n){if(!nodes[n].affected)continue;
   std::vector<void*> expected;std::vector<size_t> visiting;
   const auto collect=[&](const auto& self,size_t index)->bool {
    if(std::find(visiting.begin(),visiting.end(),index)!=visiting.end())return Fail(error,"Cyclic staged interface hierarchy");visiting.push_back(index);
    if(roots.parents[index]!=absent&&!self(self,roots.parents[index]))return false;
    for(auto iface:nodes[index].interfaces){void* native=nullptr;if(!this->roots.Read(iface,&native))return false;auto it=class_index.find(native);if(it==class_index.end())return Fail(error,"Interface identity is outside staged Class roots");
     if(std::find(expected.begin(),expected.end(),native)==expected.end())expected.push_back(native);
     if(!self(self,it->second))return false;
    }visiting.pop_back();return true;
   };
   if(!collect(collect,n))return false;
   for(bool after:{false,true}){
    // The original public identities remain this verifier's independent proof.
    // A changed transitive replacement table belongs to the sealed authority.
    if(after&&HierarchyRequired())continue;
    void* klass=after?mapped(roots.classes[n]):roots.classes[n];void* table=bindings.metadata->Reference(klass,24);uint32_t length=0;
    if(table){if(!Mapped(table,12))return Fail(error,"Native interface array is unreadable");length=Read<uint32_t>(table,8);}
    if(length%2||size_t(length)/2!=expected.size()||(table&&!Mapped(table,12+size_t(length)*4)))return Fail(error,"Native interface table count differs from public transitive identities");
    std::set<void*> seen;for(size_t i=0;i<expected.size();++i){void* actual=bindings.metadata->Reference(table,12+i*8);
     // ART orders inherited and direct interfaces differently from reflection.
     bool found=false;for(auto candidate:expected)found|=actual==(after?mapped(candidate):candidate);
     if(!actual||!found||!seen.insert(actual).second)return Fail(error,"Native interface identity differs from the original public graph");
    }
   }
  }
  return true;
 }
};
Verifier::Verifier():state_(new State){}
Verifier::~Verifier(){if(!state_->nodes.empty()||!state_->inputs.empty()||!state_->roots.Empty())std::abort();}
void Verifier::Clear(JNIEnv* env){if(!env&&(!state_->nodes.empty()||!state_->inputs.empty()||!state_->roots.Empty()))std::abort();if(env)state_->roots.Clear(env);*state_=State{};}
bool Verifier::CaptureCallGraph(const runnable_jni::Context&context,
 const receiver_guard::SourceProducer&source,native_call_graph_inventory::Inventory*out,std::string*error)const{
 const auto&s=*state_;
 if(!out||out->roots_||!s.staged||s.stopped_context||!runnable_jni::Bridge::BorrowOriginalScope(context)||
    context.vm!=s.bindings.vm)return Fail(error,"Call graph requires actual original ordinary Runnable scope");
 Snapshot snapshot;if(!s.Roots(&snapshot,error))return false;
 const auto&layout=s.bindings.methods->model==method_identity::Model::kMethod11?method_metadata::kAndroid11:method_metadata::kAndroid12Onward;
 method_identity::ScopedReadableMappings mappings;if(!mappings.Ready())return Fail(error,"Call graph current native mapping capture failed");
 modern_field_metadata::ReadContext read{&mappings,method_identity::ScopedReadableMappings::ReadExtent};
 NativeOrdinals classes; if(!IndexNativeClasses(snapshot.classes,&classes,error))return false;
 std::unordered_map<void*,uint64_t> loader_tokens;
 for(size_t n=0;n<snapshot.loaders.size();++n)if(snapshot.loaders[n])loader_tokens.emplace(snapshot.loaders[n],uint64_t(n)+1);
 std::unordered_map<void*,std::pair<size_t,size_t>> declarations;
 std::vector<std::unordered_map<uint32_t,size_t>> dex_indices(s.nodes.size());
 for(size_t n=0;n<s.nodes.size();++n)for(size_t i=0;i<snapshot.declared[n].size();++i){
  void*method=snapshot.declared[n][i];size_t extent=0;method_metadata::Identity identity{};
  if(!declarations.emplace(method,std::make_pair(n,i)).second||!read.extent(read.owner,method,&extent)||
     !method_metadata::ReadIdentity(layout,method,extent,&identity)||
     !dex_indices[n].emplace(identity.dex_method_index,i).second)return Fail(error,"Call graph declaration index is ambiguous");
 }
 std::map<std::pair<std::string,std::string>,std::vector<size_t>> signatures;
 std::set<std::pair<std::string,std::string>> affected_fields;
 std::vector<std::vector<size_t>> ancestors(s.nodes.size());
 for(size_t n=0;n<s.nodes.size();++n){
  for(const auto&m:s.nodes[n].schema.methods)signatures[{m.name,m.descriptor}].push_back(n);
  if(s.nodes[n].affected)for(const auto&f:s.nodes[n].schema.fields)affected_fields.emplace(f.name,f.descriptor);
  if(snapshot.parents[n]!=SIZE_MAX)ancestors[n].push_back(snapshot.parents[n]);
  else if(s.nodes[n].schema.access&0x200u){void*object=nullptr;
   if(!NativeSuperclass(s.nodes,snapshot,n,&object))return Fail(error,"Call graph interface Object ancestry is unavailable");
   auto found=classes.find(object);if(found==classes.end())return Fail(error,"Call graph interface Object root is absent");ancestors[n].push_back(found->second);
  }
  for(auto root:s.nodes[n].interfaces){void*klass=nullptr;if(!s.roots.Read(root,&klass))return false;
   auto found=classes.find(klass);if(found==classes.end())return Fail(error,"Call graph actual reference graph is outside rooted census");ancestors[n].push_back(found->second);}
 }
 std::map<std::tuple<std::string,std::string,std::string>,bool> candidate_cache;
 const auto related=[&](const evolution::MappedDexMethodReference&ref){
  const auto key=std::make_tuple(ref.declaring_class,ref.name,ref.descriptor);
  auto cached=candidate_cache.find(key);if(cached!=candidate_cache.end())return cached->second;
  bool present=false;auto owners=s.public_index.buckets.find(ref.declaring_class);
  auto methods=signatures.find({ref.name,ref.descriptor});
  if(owners!=s.public_index.buckets.end()&&methods!=signatures.end()){
   std::vector<uint8_t>declared(s.nodes.size(),0);for(auto n:methods->second)declared[n]=1;
   for(auto owner:owners->second){auto lineage=WalkCallGraphDomain(ancestors,owner);
    for(auto n:lineage)if(declared[n]){present=true;break;}if(present)break;}
  }
  candidate_cache.emplace(key,present);return present;
 };
 for(size_t containing=0;containing<s.nodes.size();++containing)for(void*method:snapshot.all_methods[containing]){
  void*declaring=s.bindings.method_class(s.bindings.owner,method);
  auto cls=classes.find(declaring);
  if(cls==classes.end())return Fail(error,"Call graph actual declaring Class is outside rooted census");
  const size_t owner=cls->second;const auto&node=s.nodes[owner];size_t public_index=SIZE_MAX;
  auto exact=declarations.find(method);
  if(exact!=declarations.end()&&exact->second.first==owner)public_index=exact->second.second;
  else if(owner!=containing){size_t extent=0;method_metadata::Identity copied{};
   if(!read.extent(read.owner,method,&extent)||!method_metadata::ReadIdentity(layout,method,extent,&copied))return Fail(error,"Call graph copied method prefix is unreadable");
   auto original=dex_indices[owner].find(copied.dex_method_index);
   if(original!=dex_indices[owner].end()&&s.bindings.same_method(s.bindings.owner,method,snapshot.declared[owner][original->second]))public_index=original->second;
  }
  if(public_index==SIZE_MAX||public_index>=node.method_bytes.size())return Fail(error,"Call graph method lacks independent public declaration");
  const auto&member=node.schema.methods[public_index];
  if(member.access&0x400u)continue; // Abstract declarations have no executable body.
  if(node.class_name)return Fail(error,"Call graph proxy implementation source is unsupported");
  size_t extent=0;method_metadata::Identity identity{};
  if(!read.extent(read.owner,method,&extent)||!method_metadata::ReadIdentity(layout,method,extent,&identity)||
    identity.dex_method_index==UINT32_MAX||(identity.access_flags&0x40000u))return Fail(error,"Call graph fresh method identity is unavailable");
  evolution::MappedDexMetadata dex{};field_copy::CopyClassView named{};
  if(!native_interface_dispatch::ReadCurrentNamespace(*s.bindings.metadata,declaring,read,&dex,&named)||
   !native_dex_identity::DexIdentity(dex).Matches(identity.dex_method_index,node.schema.descriptor,member.name,member.descriptor))
    return Fail(error,"Call graph independent public declaration differs from actual DEX namespace");
  native_call_graph_inventory::Inventory::Method record;
  record.native={method,node.root,s.nodes[containing].root,identity.dex_method_index,member.access};
  record.declaring=owner;record.containing=containing;record.owner=node.schema.descriptor;record.name=member.name;
  record.signature=member.descriptor;record.bytes=node.method_bytes[public_index];
  const bool native=(member.access&0x100u)!=0;
  if(!native){
   if(!source.Code(context.self,method,&record.code,read)||record.code.source_dex.begin!=dex.begin||
      record.code.units>SIZE_MAX/2||record.bytes.size()!=record.code.units*2||
      std::memcmp(record.bytes.data(),record.code.instructions,record.bytes.size()))return Fail(error,"Call graph native CodeItem differs from independently staged public bytes");
   const char*failure=nullptr;if(!native_call_graph::ScanCode(record.code,&record.calls,&failure))return Fail(error,failure);
   for(const auto&site:record.calls){
    if(site.instruction.kind==receiver_guard::InvokeKind::Custom||site.instruction.kind==receiver_guard::InvokeKind::Polymorphic){record.related.push_back(false);continue;}
    evolution::MappedDexMethodReference reference;std::string reason;
    if(!evolution::ReadMappedDexMethodReference(record.code.source_dex,site.instruction.index,&reference,&reason))return Fail(error,"Call graph actual invoke reference is malformed");
    record.related.push_back(related(reference));
   }
   for(size_t pc=0;pc<record.code.units;){uint32_t width=0;bool payload=false;
    if(!dex_instruction::Step(record.code.instructions,record.code.units,pc,&width,&payload))return Fail(error,"Call graph field instruction boundary is unavailable");
    const unsigned op=record.code.instructions[pc]&255u;
    if(!payload&&op>=0x52&&op<=0x6d){uint32_t fields=0;
     std::memcpy(&fields,record.code.source_dex.begin+80,4);
     if(width!=2||record.code.instructions[pc+1]>=fields)return Fail(error,"Call graph actual field operand is outside source DEX namespace");
     evolution::MappedDexFieldReference reference;std::string reason;
     if(!evolution::ReadMappedDexFieldReference(record.code.source_dex,record.code.instructions[pc+1],&reference,&reason)||
       !reference.name||!reference.descriptor)return Fail(error,"Call graph actual field reference is malformed");
     record.fields|=affected_fields.find({reference.name,reference.descriptor})!=affected_fields.end();
    }
    pc+=width;
   }
  }
  uint64_t loader_token=snapshot.loaders[owner]?loader_tokens.at(snapshot.loaders[owner]):0;
  if(!native&&(s.nodes[containing].affected||node.affected))out->affected_seeds_.push_back(out->nodes_.size());
  out->nodes_.push_back({{dex.begin,loader_token,method},native?native_call_graph::MethodKind::Native:native_call_graph::MethodKind::Managed});
  out->methods_.push_back(std::move(record));
 }
 out->roots_=&s.roots;out->generation_=s.roots.Generation();out->census_revision_=s.census_revision;return true;
}
bool Verifier::ResolveCallGraphSuper(const runnable_jni::Context&context,receiver_guard::SourceProducer&source,
 const native_call_graph_super::Resolver&resolver,size_t caller,size_t site,void*declaration,
 native_call_graph_inventory::Inventory*inventory,std::string*error)const{
 const auto&s=*state_;
 if(!inventory||inventory->verifier_!=this||caller>=inventory->methods_.size()||
   site>=inventory->methods_[caller].calls.size()||s.stopped_context||
   inventory->generation_!=s.roots.Generation())return Fail(error,"Call graph super rooted owner unavailable");
 const auto&m=inventory->methods_[caller];const auto&call=m.calls[site];
 evolution::MappedDexMethodReference reference;
 if(!evolution::ReadMappedDexMethodReference(m.code.source_dex,call.instruction.index,&reference,error))return false;
 // This public bucket is only a candidate index. Actual current managed root
 // identity, read immediately after the potentially moving original call,
 // is the authority; a descriptor never substitutes for a loader identity.
 auto candidates=s.public_index.buckets.find(reference.declaring_class);
 if(candidates==s.public_index.buckets.end())return Fail(error,"Super referenced type requires restaging");
 method_identity::ScopedReadableMappings mappings;if(!mappings.Ready())return false;
 modern_field_metadata::ReadContext read{&mappings,method_identity::ScopedReadableMappings::ReadExtent};
 native_call_graph_super::Observation observed{};
 if(!resolver.ResolveOriginal(context,source,m.native.method,m.code,m.bytes,call,declaration,read,&observed,error))return false;
 // No JNI or other resolver may run before these borrowed Class addresses are
 // converted to slots owned by the independently complete staged census.
 void*actual=nullptr;
 if(!s.roots.Read(m.native.declaring_root,&actual)||actual!=observed.caller_class||
   observed.caller!=m.native.method||observed.declaration!=declaration||observed.dex_pc!=call.dex_pc||
   observed.dex_namespace!=m.code.source_dex.begin||observed.type_index!=reference.class_index)return Fail(error,"Super caller source changed; needs-restage");
 bounded_managed_roots::Slot referenced{};size_t matches=0;
 for(auto n:candidates->second){if(!s.roots.Read(s.nodes[n].root,&actual))return false;
  if(actual==observed.referenced_class){referenced=s.nodes[n].root;++matches;}}
 if(matches!=1)return Fail(error,"Super referenced type outside current rooted census; needs-restage");
 // Interface default selection needs a full frozen conflict/override proof.
 // Its ordinary result alone cannot authorize a stopped publication.
 if(observed.interface_super)return true;
 if(m.declaring>=s.nodes.size()||!s.nodes[m.declaring].parent||
   !s.roots.Read(s.nodes[m.declaring].parent,&actual)||actual!=observed.super_class)return Fail(error,"Super parent source changed; needs-restage");
 auto target=inventory->method_ordinals_.find(observed.target);
 if(target==inventory->method_ordinals_.end())return Fail(error,"Super target outside rooted method census; needs-restage");
 const auto&selected=inventory->methods_[target->second];void*containing=nullptr;
 if(!s.roots.Read(selected.native.declaring_root,&actual)||
   actual!=s.bindings.metadata->MethodDeclaringClass(observed.target)||
   !s.roots.Read(selected.native.containing_root,&containing)||!containing)
  return Fail(error,"Super selected declaration/containing roots changed; needs-restage");
 inventory->supers_.push_back({caller,site,referenced,s.nodes[m.declaring].parent,
  declaration,observed.target,observed.type_index,observed.slot,std::move(reference)});return true;
}
bool Verifier::ApplyCallGraphResolution(const runnable_jni::Context&context,
 const std::vector<native_call_graph_resolution::Result>&results,
 native_call_graph_inventory::Inventory*inventory,std::string*error)const{
 const auto&s=*state_;
 if(!inventory||inventory->verifier_!=this||s.stopped_context||s.verified||s.collected||
    !runnable_jni::Bridge::BorrowOriginalScope(context)||context.vm!=s.bindings.vm||
    inventory->generation_!=s.roots.Generation())return Fail(error,"Call graph fresh resolved source window is unavailable");
 Snapshot snapshot;if(!s.Roots(&snapshot,error))return false;
 NativeOrdinals classes;if(!IndexNativeClasses(snapshot.classes,&classes,error))return false;
 std::unordered_map<void*,size_t> graph_methods,declaring;
 for(size_t i=0;i<inventory->methods_.size();++i)graph_methods.emplace(inventory->methods_[i].native.method,i);
 // Abstract declarations remain independently indexed even though they have
 // no executable graph node. The actual dispatch target comes from ART tables.
 for(size_t n=0;n<snapshot.declared.size();++n)for(void*method:snapshot.declared[n])declaring.emplace(method,n);
 for(size_t n=0;n<snapshot.all_methods.size();++n)for(void*method:snapshot.all_methods[n]){
  auto cls=classes.find(s.bindings.method_class(s.bindings.owner,method));
  if(cls==classes.end())return Fail(error,"Call graph resolved copied owner requires restaging");
  declaring.emplace(method,cls->second);
 }
 method_identity::ScopedReadableMappings mappings;if(!mappings.Ready())return Fail(error,"Call graph resolved mapping snapshot failed");
 modern_field_metadata::ReadContext read{&mappings,method_identity::ScopedReadableMappings::ReadExtent};
 for(const auto&m:inventory->methods_){
  if(m.containing>=snapshot.all_methods.size()||m.declaring>=snapshot.classes.size()||
    s.bindings.method_class(s.bindings.owner,m.native.method)!=snapshot.classes[m.declaring])return Fail(error,"Call graph resolved source roots changed; restage required");
  if(!(m.native.modifiers&0x100u)){receiver_guard::CodeView current{};
   if(!inventory->source_->Code(context.self,m.native.method,&current,read)||current.code_item!=m.code.code_item||
     current.source_dex.begin!=m.code.source_dex.begin||current.units>SIZE_MAX/2||current.units*2!=m.bytes.size()||
     std::memcmp(m.bytes.data(),current.instructions,m.bytes.size()))return Fail(error,"Call graph source body changed during resolving; restage required");
  }
 }
 // Reverse actual rooted parent/interface edges. Domain traversal visits each
 // identity once and is cached per resolved declaration's declaring Class.
 std::vector<std::vector<size_t>> children(s.nodes.size());
 for(size_t n=0;n<s.nodes.size();++n){if(snapshot.parents[n]!=SIZE_MAX)children[snapshot.parents[n]].push_back(n);
  for(auto root:s.nodes[n].interfaces){void*interface=nullptr;if(!s.roots.Read(root,&interface))return false;
   auto cls=classes.find(interface);if(cls==classes.end())return Fail(error,"Call graph interface domain requires restaging");children[cls->second].push_back(n);}}
 std::unordered_map<size_t,std::vector<size_t>> domains;
 const auto domain=[&](size_t owner)->const std::vector<size_t>&{
  auto existing=domains.find(owner);if(existing!=domains.end())return existing->second;
  return domains.emplace(owner,WalkCallGraphDomain(children,owner)).first->second;
 };
 const auto&layout=s.bindings.methods->model==method_identity::Model::kMethod11?method_metadata::kAndroid11:method_metadata::kAndroid12Onward;
 std::optional<native_call_graph_dispatch::Reader> dispatch;
 if(s.bindings.abstract_instances&&s.bindings.abstract_instances->Ready())dispatch.emplace(*s.bindings.metadata,*s.bindings.abstract_instances,layout);
 using CacheKey=std::pair<void*,native_call_graph::InvokeKind>;
 std::map<CacheKey,std::vector<size_t>> dispatched;
 std::map<std::pair<size_t,uint32_t>,const native_call_graph_inventory::Inventory::Super*> supers;
 for(const auto&w:inventory->supers_)if(w.caller>=inventory->methods_.size()||w.site>=inventory->methods_[w.caller].calls.size()||
   !supers.emplace(std::make_pair(w.caller,inventory->methods_[w.caller].calls[w.site].dex_pc),&w).second)return Fail(error,"Super source receipt is ambiguous");
 std::vector<native_call_graph::Edge> edges;size_t at=0;
 for(size_t caller=0;caller<inventory->methods_.size();++caller)for(const auto&site:inventory->methods_[caller].calls){
  const auto kind=native_call_graph::Kind(site.instruction.kind);
  const auto unresolved=[&]{edges.push_back({caller,SIZE_MAX,site.dex_pc,kind,native_call_graph::Coverage::Unresolved});};
  if(kind==native_call_graph::InvokeKind::Custom||kind==native_call_graph::InvokeKind::Polymorphic){unresolved();continue;}
  if(at>=results.size())return Fail(error,"Call graph original result count mismatch");const auto&r=results[at++];
  if(r.status!=native_call_graph_resolution::Status::Resolved){unresolved();continue;}
  if(r.caller!=inventory->methods_[caller].native.method||r.dex_pc!=site.dex_pc||
    r.dex_namespace!=inventory->nodes_[caller].identity.dex_namespace||
    !native_call_graph_resolution::detail::Same(r.instruction,site.instruction))return Fail(error,"Call graph original result namespace/callsite mismatch");
  auto owner=declaring.find(r.declaration);
  if(owner==declaring.end())return Fail(error,"Call graph resolved declaration is outside loaded census; needs-restage");
  if(kind==native_call_graph::InvokeKind::Direct||kind==native_call_graph::InvokeKind::Static){
   auto target=graph_methods.find(r.declaration);
   if(target==graph_methods.end()){unresolved();continue;}
   const bool is_static=(inventory->methods_[target->second].native.modifiers&8u)!=0;
   if(is_static!=(kind==native_call_graph::InvokeKind::Static)){unresolved();continue;}
   edges.push_back({caller,target->second,site.dex_pc,kind,native_call_graph::Coverage::Exact});continue;
  }
  if(kind==native_call_graph::InvokeKind::Super){
   auto witness=supers.find({caller,site.dex_pc});
   if(witness==supers.end()||witness->second->declaration!=r.declaration){unresolved();continue;}
   auto target=graph_methods.find(witness->second->target);if(target==graph_methods.end())return Fail(error,"Super selected method outside current census; needs-restage");
   edges.push_back({caller,target->second,site.dex_pc,kind,native_call_graph::Coverage::Exact});continue;
  }
  if(!dispatch){unresolved();continue;}
  const CacheKey key{r.declaration,kind};auto cached=dispatched.find(key);
  if(cached==dispatched.end()){
   std::vector<size_t> targets;std::vector<native_call_graph_inventory::Inventory::Dispatch> witnesses;bool closed=true;
   for(auto receiver:domain(owner->second)){
    // An actual object cannot have an abstract/interface runtime Class. All
    // concrete descendants still appear through the complete rooted graph.
    if(s.nodes[receiver].schema.access&0x600u)continue;
    native_call_graph_dispatch::Observation observed{};const char*failure=nullptr;
    if(!dispatch->Observe(kind,r.declaration,snapshot.classes[receiver],read,&observed,&failure)||
       observed.declaration!=r.declaration||observed.declaring_class!=snapshot.classes[owner->second]||
       observed.receiver_class!=snapshot.classes[receiver]){closed=false;break;}
    auto target=graph_methods.find(observed.target);
    if(target==graph_methods.end()){closed=false;break;}targets.push_back(target->second);
    witnesses.push_back({r.declaration,kind,s.nodes[receiver].root,s.nodes[owner->second].root,observed.target});
   }
   // A call with no currently loaded concrete receiver remains unresolved;
   // future definitions require a new inventory, never a vacuous safe grant.
   if(!closed||targets.empty()){dispatched.emplace(key,std::vector<size_t>{});unresolved();continue;}
   std::sort(targets.begin(),targets.end());targets.erase(std::unique(targets.begin(),targets.end()),targets.end());
   inventory->dispatches_.insert(inventory->dispatches_.end(),witnesses.begin(),witnesses.end());
   cached=dispatched.emplace(key,std::move(targets)).first;
  }
  if(cached->second.empty()){unresolved();continue;}
  for(auto target:cached->second)edges.push_back({caller,target,site.dex_pc,kind,native_call_graph::Coverage::ClosedDispatch});
 }
 if(at!=results.size())return Fail(error,"Call graph original result count mismatch");
 inventory->edges_=std::move(edges);inventory->census_revision_=s.census_revision;return true;
}
bool Verifier::ValidateCallGraph(const native_call_graph_inventory::Inventory&inventory,const modern_field_metadata::ReadContext&read)const noexcept{
 return ValidateCallGraphSource(inventory,read,true);
}
bool Verifier::ValidateCallGraphOrdinary(const native_call_graph_inventory::Inventory&inventory,const modern_field_metadata::ReadContext&read)const noexcept{
 return ValidateCallGraphSource(inventory,read,false);
}
bool Verifier::ValidateCallGraphSource(const native_call_graph_inventory::Inventory&inventory,
 const modern_field_metadata::ReadContext&read,bool final,const native_source_stop::Session*stop)const noexcept{
 const auto&s=*state_;
 if(inventory.verifier_!=this||inventory.roots_!=&s.roots||inventory.generation_!=s.roots.Generation()||
   (inventory.resolved_&&s.census_revision<=inventory.census_revision_)||
   (final&&(s.stopped_context!=&read||!s.FrozenProof()))||!redefinition_gate::Owned(s.bindings.vm)||!read.owner||!read.extent)return false;
 const auto&layout=s.bindings.methods->model==method_identity::Model::kMethod11?method_metadata::kAndroid11:method_metadata::kAndroid12Onward;
 for(size_t i=0;i<inventory.methods_.size();++i){const auto&m=inventory.methods_[i];void*declaring=nullptr;
  if(!s.roots.Read(m.native.declaring_root,&declaring)||s.bindings.method_class(s.bindings.owner,m.native.method)!=declaring)return false;
  void*containing=nullptr;
  if(!s.roots.Read(m.native.containing_root,&containing))return false;
  size_t extent=0;if(!read.extent(read.owner,containing,&extent)||extent<56)return false;
  uint64_t array=Read<uint64_t>(containing,offsetof(class_metadata::Class11,methods));
  if(!read.extent(read.owner,reinterpret_cast<void*>(uintptr_t(array)),&extent)||
    !native_selective_interpretation::detail::Member(reinterpret_cast<void*>(uintptr_t(array)),extent,m.native.method,layout.size))return false;
  method_metadata::Identity identity{};
  if(!read.extent(read.owner,m.native.method,&extent)||!method_metadata::ReadIdentity(layout,m.native.method,extent,&identity)||
    identity.dex_method_index!=m.native.dex_index||jvmti_method_flags::Public(identity.access_flags)!=m.native.modifiers)return false;
  evolution::MappedDexMetadata dex{};field_copy::CopyClassView named{};
  if(!native_interface_dispatch::ReadCurrentNamespace(*s.bindings.metadata,declaring,read,&dex,&named)||dex.begin!=inventory.nodes_[i].identity.dex_namespace||
    !native_dex_identity::DexIdentity(dex).Matches(identity.dex_method_index,m.owner,m.name,m.signature))return false;
  if(!(m.native.modifiers&0x100u)){receiver_guard::CodeView code{};
   if(!inventory.source_||!inventory.source_->Code(inventory.self_,m.native.method,&code,read)||
      code.source_dex.begin!=dex.begin||code.code_item!=m.code.code_item||code.instructions!=m.code.instructions||
      code.units>SIZE_MAX/2||m.bytes.size()!=code.units*2||
      std::memcmp(m.bytes.data(),code.instructions,m.bytes.size()))return false;
  }
 }
 if(!inventory.dispatches_.empty()){
  if(!s.bindings.abstract_instances||!s.bindings.abstract_instances->Ready())return false;
  native_call_graph_dispatch::Reader dispatch(*s.bindings.metadata,*s.bindings.abstract_instances,layout);
  for(const auto&w:inventory.dispatches_){void*receiver=nullptr;void*declaring=nullptr;
   if(!s.roots.Read(w.receiver,&receiver)||!s.roots.Read(w.declaring,&declaring))return false;
   native_call_graph_dispatch::Observation current{};
   if(!dispatch.Observe(w.kind,w.declaration,receiver,read,&current)||current.target!=w.target||
      current.declaring_class!=declaring||current.receiver_class!=receiver)return false;
  }
 }

 if(!inventory.supers_.empty()){
  if(!stop||!inventory.super_resolver_||!inventory.source_)return false;
  for(const auto&w:inventory.supers_){
   if(w.caller>=inventory.methods_.size())return false;const auto&m=inventory.methods_[w.caller];
   if(w.site>=m.calls.size())return false;
   void*caller=nullptr;void*referenced=nullptr;void*parent=nullptr;
   if(!s.roots.Read(m.native.declaring_root,&caller)||!s.roots.Read(w.referenced,&referenced)||
     !s.roots.Read(w.parent,&parent))return false;
   receiver_guard::CodeView code{};
   if(!inventory.source_->Code(inventory.self_,m.native.method,&code,read))return false;
   native_call_graph_super::Observation current{m.native.method,referenced,caller,parent,w.declaration,w.target,
     m.code.source_dex.begin,m.calls[w.site].dex_pc,w.type_index,w.slot,false};
   if(!inventory.super_resolver_->ValidateObserved(*stop,inventory.self_,code,m.bytes,m.calls[w.site],w.reference,current,read))return false;
  }
 }
 return true;
}
bool Verifier::Stage(JNIEnv* env,jvmtiEnv* ti,const std::vector<jclass>& inputs,const Bindings& bindings,std::string* error){
 if(!env||!ti||inputs.empty()||!redefinition_gate::Owned(bindings.vm)||env->ExceptionCheck()||!state_->nodes.empty())return Fail(error,"Invalid or repeated schema graph stage");if(!EnsureCallGraphBytecodes(ti,error))return false;state_->bindings=bindings;state_->roots.Bind(bindings.roots);
 for(auto klass:inputs){bounded_managed_roots::Slot slot;if(!state_->roots.Stage(env,klass,&slot,error))return false;state_->inputs.push_back(slot);}
 struct NameMethods {JNIEnv* env;jclass proxy=nullptr,klass=nullptr;~NameMethods(){if(proxy)env->DeleteLocalRef(proxy);if(klass)env->DeleteLocalRef(klass);}} public_names{env};
 public_names.proxy=env->FindClass("java/lang/reflect/Proxy");public_names.klass=env->FindClass("java/lang/Class");
 if(!public_names.proxy||!public_names.klass||env->ExceptionCheck())return Fail(error,"Public Class kind/name service unavailable");
 const auto is_proxy=env->GetStaticMethodID(public_names.proxy,"isProxyClass","(Ljava/lang/Class;)Z");
 const auto get_name=env->GetMethodID(public_names.klass,"getName","()Ljava/lang/String;");
 const auto get_component=env->GetMethodID(public_names.klass,"getComponentType","()Ljava/lang/Class;");
 if(!is_proxy||!get_name||!get_component||env->ExceptionCheck())return Fail(error,"Public Class kind/name declarations unavailable");
 // Each public reference is retained as a movable JNI local until its
 // cohort is attested. Only Slot destinations (not native Class pointers)
 // survive JNI calls; node/interface storage is reserved before queueing.
 using RootSlot=bounded_managed_roots::Slot;
 bounded_managed_roots::RootCohort<RootSlot*> cohort{
  state_->roots,env,error,nullptr,[](void*,RootSlot* const& destination,RootSlot slot)noexcept{
   if(!destination)return false;*destination=slot;return true;
  }};
 auto& staged_names=state_->public_index.buckets;
 for(;;){
 const size_t before=state_->nodes.size()+state_->error_census.Size();size_t known=0;
 jint count=0;jclass* classes=nullptr;if(ti->GetLoadedClasses(&count,&classes)!=JVMTI_ERROR_NONE||count<0)return Fail(error,"Complete loaded caller graph unavailable");
 struct Classes {JNIEnv* env;jvmtiEnv* ti;jint count;jclass* values;~Classes(){for(jint i=0;i<count;++i)env->DeleteLocalRef(values[i]);if(values)ti->Deallocate(reinterpret_cast<unsigned char*>(values));}} release{env,ti,count,classes};
 if(!cohort.Flush())return false;
 state_->nodes.reserve(state_->nodes.size()+size_t(count));
 for(jint i=0;i<count;++i){jint status=0;if(ti->GetClassStatus(classes[i],&status)!=JVMTI_ERROR_NONE)return false;
  if(!CallerClass(status)){
   if(!cohort.Flush())return false;bool existing=false;
   if(!state_->error_census.Stage(env,ti,classes[i],status,state_->roots,&existing,error))return false;
   if(existing)++known;continue;
  }
  if(!PreparedCaller(status))return Fail(error,"Unprepared loaded Class prevents complete caller provenance");
  Strings identity{ti};if(ti->GetClassSignature(classes[i],&identity.signature,nullptr)!=JVMTI_ERROR_NONE||!identity.signature)return Fail(error,"Loaded Class signature unavailable while closing staging");
  bool existing=false;const auto candidates=staged_names.find(identity.signature);
  if(candidates!=staged_names.end()){
   if(!cohort.Flush())return false; // Compare current public roots, including this enumeration's pending nodes.
   for(size_t index:candidates->second)existing|=state_->roots.Same(env,state_->nodes[index].root,classes[i]);
  }
  if(env->ExceptionCheck())return false;
  if(existing){++known;continue;}
#if defined(__ANDROID__)
  // Ordinary-stage diagnostics only: no managed identity is retained and no
  // completion or permission is inferred from these inventory counters.
  if(state_->nodes.size()%64==0)__android_log_print(ANDROID_LOG_INFO,"artdcevm",
   "Caller inventory staged=%zu enumeration=%d next=%s",state_->nodes.size(),int(count),identity.signature);
#endif
  Node node;if(!PublicMembers(env,ti,classes[i],&node,error,&bindings))return false;
  state_->nodes.push_back(std::move(node));auto& stored=state_->nodes.back();staged_names[stored.schema.descriptor].push_back(state_->nodes.size()-1);
  if(!cohort.Add(classes[i],&stored.root))return false;
  jobject loader=nullptr;if(ti->GetClassLoader(classes[i],&loader)!=JVMTI_ERROR_NONE)return false;if(loader){const bool pass=cohort.Add(loader,&stored.loader);env->DeleteLocalRef(loader);if(!pass)return false;}
  if(status&JVMTI_CLASS_STATUS_ARRAY){auto component=env->CallObjectMethodA(classes[i],get_component,nullptr);
   const bool pass=component&&!env->ExceptionCheck()&&cohort.Add(component,&stored.component);if(component)env->DeleteLocalRef(component);if(!pass)return Fail(error,"Public array component identity unavailable");}
  jvalue class_arg{};class_arg.l=classes[i];const bool proxy=env->CallStaticBooleanMethodA(public_names.proxy,is_proxy,&class_arg);
  if(env->ExceptionCheck())return Fail(error,"Public proxy Class classification failed");
  if(proxy){
   struct Name {JNIEnv* env;jstring value;const char* chars=nullptr;~Name(){if(chars)env->ReleaseStringUTFChars(value,chars);if(value)env->DeleteLocalRef(value);}} name{env,static_cast<jstring>(env->CallObjectMethodA(classes[i],get_name,nullptr))};
   if(!name.value||env->ExceptionCheck())return Fail(error,"Public proxy Class name unavailable");
   name.chars=env->GetStringUTFChars(name.value,nullptr);
   if(!name.chars||env->ExceptionCheck())return false;
   stored.class_name_text=name.chars;stored.negative_class_name=stored.class_name_text+"!";
   if(!cohort.Add(name.value,&stored.class_name))return false;
  }
  auto parent=env->GetSuperclass(classes[i]);if(parent){const bool pass=cohort.Add(parent,&stored.parent);env->DeleteLocalRef(parent);if(!pass)return false;}
  jint interfaces_count=0;jclass* interfaces=nullptr;if(ti->GetImplementedInterfaces(classes[i],&interfaces_count,&interfaces)!=JVMTI_ERROR_NONE||interfaces_count<0)return false;
  stored.interfaces.resize(size_t(interfaces_count));
  for(jint j=0;j<interfaces_count;++j){const bool pass=cohort.Add(interfaces[j],&stored.interfaces[size_t(j)]);env->DeleteLocalRef(interfaces[j]);if(!pass){for(jint k=j+1;k<interfaces_count;++k)env->DeleteLocalRef(interfaces[k]);if(interfaces)ti->Deallocate(reinterpret_cast<unsigned char*>(interfaces));return false;}}
  if(interfaces)ti->Deallocate(reinterpret_cast<unsigned char*>(interfaces));
  for(auto input:inputs)if(env->IsAssignableFrom(classes[i],input))stored.affected=true;
  if(env->ExceptionCheck())return false;
 }
 if(!cohort.Flush())return false;
 // ToReflectedField resolves declared field types, and interface/root
 // staging may also load Classes. Only newly observed identities are staged;
 // finish after a complete fresh enumeration adds none. There is no SDK,
 // descriptor exemption, repeated mirror replay or arbitrary pass limit.
 if(known!=before)return Fail(error,"Loaded caller graph lost a rooted Class while closing staging");
 if(state_->nodes.size()+state_->error_census.Size()==before)break;
 }
 if(state_->nodes.empty())return Fail(error,"Empty public Class graph");state_->staged=true;++state_->census_revision;return true;
}
bool Verifier::MatchesAffected(JNIEnv* env,const std::vector<jclass>& actual,std::string* error)const {
 const auto& s=*state_;if(!env||!s.staged||env->ExceptionCheck())return Fail(error,"Closed caller graph is unavailable");
 size_t affected=0;for(const auto& node:s.nodes)if(node.affected){++affected;size_t matches=0;for(auto klass:actual)matches+=s.roots.Same(env,node.root,klass);if(matches!=1)return Fail(error,"Actual Holder graph differs from public staged identities");}
 if(affected!=actual.size())return Fail(error,"Duplicate or unstaged Holder Class");
 return !env->ExceptionCheck();
}
bool Verifier::CheckWindow(JNIEnv*env,jvmtiEnv*ti,const std::vector<jclass>&actual,Window window,std::string*error){
 return CheckCallerWindow(env,ti,actual,window,error);
}
bool Verifier::RefreshCallGraphCensus(JNIEnv*env,jvmtiEnv*ti,const std::vector<jclass>&inputs,std::string*error){
 if(native_source_class_loading::Run::Current())return Fail(error,"Call graph census requires ordinary pre-source JNI");
 return CheckCallerWindow(env,ti,inputs,std::nullopt,error);
}
bool Verifier::CheckCallerWindow(JNIEnv* env,jvmtiEnv* ti,const std::vector<jclass>& actual,std::optional<Window> window,std::string* error){
 auto& s=*state_;if(!s.staged||(window&&s.collected)||(!window&&(s.frozen||s.stopped_context))||(window==Window::Verified&&s.verified)||!redefinition_gate::Owned(s.bindings.vm)||!env||!ti||env->ExceptionCheck()||(window==Window::Collected&&!s.verified))return Fail(error,"Schema window order or context is invalid");
 if(window){if(!MatchesAffected(env,actual,error))return false;}
 else {
  if(actual.size()!=s.inputs.size())return Fail(error,"Call graph census inputs differ from original stage");
  for(auto root:s.inputs){size_t count=0;for(auto klass:actual)count+=s.roots.Same(env,root,klass);
   if(count!=1||env->ExceptionCheck())return Fail(error,"Call graph census inputs differ from original rooted identities");}
 }
 jint count=0;jclass* loaded=nullptr;if(ti->GetLoadedClasses(&count,&loaded)!=JVMTI_ERROR_NONE||count<0)return false;
 struct Loaded {JNIEnv* env;jvmtiEnv* ti;jint count;jclass* values;~Loaded(){for(jint i=0;i<count;++i)env->DeleteLocalRef(values[i]);if(values)ti->Deallocate(reinterpret_cast<unsigned char*>(values));}} release{env,ti,count,loaded};
 std::vector<uint8_t> seen(s.nodes.size(),0);s.error_census.BeginCheck();
 size_t live=0;for(jint i=0;i<count;++i){jint status=0;if(ti->GetClassStatus(loaded[i],&status)!=JVMTI_ERROR_NONE)return false;
  if(!CallerClass(status)){if(!s.error_census.Check(env,ti,loaded[i],status,s.roots,error))return false;continue;}
  if(!PreparedCaller(status)){
   Strings text{ti};ti->GetClassSignature(loaded[i],&text.signature,nullptr);
   return ClassWindowFailure(error,"Unprepared Class appeared before native schema sealing",text.signature,status);
  }
  Node current;if(!PublicMembers(env,ti,loaded[i],&current,error))return false;
  size_t matched=absent;
  const auto membership=s.public_index.Find(current.schema.descriptor,[&](size_t candidate){
   return !env->ExceptionCheck()&&s.roots.Same(env,s.nodes[candidate].root,loaded[i]);
  },&matched);
  if(env->ExceptionCheck())return Fail(error,"Public Class identity comparison raised an exception inside source window");
  if(membership==PublicClassIndex::Result::Missing)return ClassWindowFailure(error,"Class was defined after caller staging; retry complete schema operation",current.schema.descriptor.c_str(),status);
  if(membership!=PublicClassIndex::Result::Unique||seen[matched])return Fail(error,"Loaded caller graph repeats an independently rooted Class identity");
  seen[matched]=1;auto node=s.nodes.begin()+matched;
  if(!SamePublic(*node,current))return Fail(error,"Public member flags/identities changed inside the source window");
  jobject loader=nullptr;if(ti->GetClassLoader(loaded[i],&loader)!=JVMTI_ERROR_NONE)return Fail(error,"Paused ClassLoader identity unavailable");const bool same_loader=s.roots.Same(env,node->loader,loader);if(loader)env->DeleteLocalRef(loader);if(!same_loader)return Fail(error,"Public ClassLoader identity changed inside source window");
  auto parent=env->GetSuperclass(loaded[i]);const bool same_parent=s.roots.Same(env,node->parent,parent);if(parent)env->DeleteLocalRef(parent);if(!same_parent)return Fail(error,"Public superclass identity changed inside the source window");
  bool affects=false;for(auto input:s.inputs){auto ref=static_cast<jclass>(s.roots.LocalRef(env,input));if(!ref||env->ExceptionCheck())return false;affects|=env->IsAssignableFrom(loaded[i],ref);env->DeleteLocalRef(ref);}if(affects!=node->affected)return Fail(error,"Loaded Class assignability changed after staging");++live;
 }
 if(live!=s.nodes.size()||!s.error_census.EndCheck()||env->ExceptionCheck())return Fail(error,"Loaded caller graph lost staged Class identity");
 if(window==Window::Verified)s.verified=true;else if(window==Window::Collected)s.collected=true;++s.census_revision;return true;
}
bool Verifier::Seal(const source_commit::Plan& transaction,std::string* error){
 auto& s=*state_;if(!s.collected||s.frozen||!redefinition_gate::Owned(s.bindings.vm)||transaction.old_classes.empty()||transaction.old_classes.size()!=transaction.new_classes.size())return Fail(error,"Schema graph cannot seal before its real collected window");
 s.sealed=false;s.access_proven=false;s.final_fields_proven=false;s.aliases_proven=false;s.access_slots.clear();s.readonly_access_slots.clear();s.grants.clear();s.required_requests.clear();s.requirements_plan=nullptr;s.hierarchy_authority=nullptr;
 Snapshot roots;if(!s.Roots(&roots,error))return false;
 NativeOrdinals class_index;if(!IndexNativeClasses(roots.classes,&class_index,error))return false;
 const auto Mapped=[&](const void* address,size_t bytes){return address&&s.bindings.readable(s.bindings.owner,address,bytes);};
 size_t count=0;for(size_t n=0;n<s.nodes.size();++n)if(s.nodes[n].affected){++count;if(std::count(transaction.old_classes.begin(),transaction.old_classes.end(),roots.classes[n])!=1)return Fail(error,"Native Holder Class is outside public affected graph");}
 if(count!=transaction.old_classes.size())return Fail(error,"Native Holder repeats or omits a staged Class");
 // Calibrate the common IfTable source prefix from a real positive and empty
 // public transitive graph, never from a successful zero-interface read alone.
 bool positive=false,negative=false;
 for(size_t n=0;n<s.nodes.size();++n){if(s.nodes[n].schema.descriptor.empty()||s.nodes[n].schema.descriptor.front()!='L')continue;
  std::set<void*> expected;std::set<size_t> traversed;
  const auto walk=[&](const auto& self,size_t k)->bool{if(!traversed.insert(k).second)return true;if(roots.parents[k]!=absent&&!self(self,roots.parents[k]))return false;for(auto iface:s.nodes[k].interfaces){void* native=nullptr;if(!s.roots.Read(iface,&native))return false;auto it=class_index.find(native);if(it==class_index.end())return false;expected.insert(native);if(!self(self,it->second))return false;}return true;};
  if(!walk(walk,n))return Fail(error,"Public interface calibration roots are incomplete");void* table=s.bindings.metadata->Reference(roots.classes[n],24);uint32_t length=0;if(table){if(!Mapped(table,12))return Fail(error,"Collected source IfTable header is unreadable in the current mapping view");length=Read<uint32_t>(table,8);}
  if(length%2||size_t(length)/2!=expected.size()||(table&&!Mapped(table,12+size_t(length)*4)))return Fail(error,"Source IfTable differs from independently observed interfaces");
  std::set<void*> observed;for(size_t i=0;i<expected.size();++i){void* value=s.bindings.metadata->Reference(table,12+i*8);if(!expected.count(value)||!observed.insert(value).second)return Fail(error,"Source IfTable contains a different interface identity");}
  positive|=expected.size()>=2;negative|=expected.empty();
 }
 if(!positive||!negative)return Fail(error,"Whole graph lacks independent positive and empty interface oracles");
 if(s.bindings.final_fields){if(!s.bindings.final_fields->VerifyAliases(transaction,error))return false;s.aliases_proven=true;}
 s.interfaces_proven=true;s.expected=transaction;s.sealed=true;return true;
}
bool Verifier::Verify(const source_commit::Plan& transaction,size_t pair,void* loader,const native_schema_policy::Requirement& requirement,std::string* error)const{
 auto& s=*state_;if(!s.sealed||!redefinition_gate::Owned(s.bindings.vm)||pair>=transaction.old_classes.size()||!SamePlan(transaction,s.expected))return Fail(error,"Schema guard has no exact sealed native Holder proof");
 if(requirement.kind==native_schema_policy::GuardKind::FinalFields&&!s.bindings.final_fields)
  return Fail(error,"Final-field guard lacks the independently calibrated permission owner");
 if(requirement.kind==native_schema_policy::GuardKind::AccessPermissions&&!s.bindings.access_permissions)
  return Fail(error,"Access guard lacks the independently calibrated permission owner");
 if(requirement.kind==native_schema_policy::GuardKind::HierarchyGraph){
  if(!s.frozen||!s.refreshed||(!s.batch_snapshot&&!s.FrozenProof())||!s.hierarchy_authority)
   return Fail(error,"Hierarchy guard lacks the actual frozen source authority");
  return s.hierarchy_authority->VerifyHierarchy(transaction,pair,loader)||
   Fail(error,"Hierarchy authority rejected the exact Plan/native source window");
 }
 if(s.frozen){
  if(s.HierarchyRequired()&&(!s.requirements_plan||s.requirements_plan!=&transaction||!s.hierarchy_authority||
      !s.hierarchy_authority->VerifyHierarchy(transaction,pair,loader)))return false;
  if(!s.refreshed||(!s.batch_snapshot&&!s.FrozenProof()))return false;
  if(!std::any_of(s.grants.begin(),s.grants.end(),[&](const auto& grant){return grant.pair==pair&&grant.loader==loader&&grant.requirement.kind==requirement.kind&&grant.requirement.member_name==requirement.member_name&&grant.requirement.descriptor==requirement.descriptor;}))return false;
  if(requirement.kind==native_schema_policy::GuardKind::ExistingInstances||requirement.kind==native_schema_policy::GuardKind::AbstractMethodDependents)
   return s.bindings.abstract_instances&&s.bindings.abstract_instances->Validate(transaction,pair,*s.bindings.metadata,nullptr,s.stopped_context);
  return true;
 }
 const auto prove=[&]()->bool {
 Snapshot local;const Snapshot* episode=s.batch_snapshot;if(!episode){if(!s.Roots(&local,error))return false;episode=&local;}const auto& roots=*episode;NativeOrdinals class_index;if(!IndexNativeClasses(roots.classes,&class_index,error))return false;auto it=class_index.find(transaction.old_classes[pair]);if(it==class_index.end())return false;const size_t target=it->second;
 if(!s.nodes[target].affected||roots.loaders[target]!=loader)return Fail(error,"Schema guard owner/ClassLoader identity differs");
 field_copy::CopyClassView old_identity,new_identity;if(!s.bindings.metadata->ClassIdentity(transaction.old_classes[pair],&old_identity,error)||!s.bindings.metadata->ClassIdentity(transaction.new_classes[pair],&new_identity,error)||old_identity.loader!=loader||new_identity.loader!=loader)return false;
 const auto mapped=[&](void* value){for(size_t i=0;i<transaction.old_classes.size();++i)if(transaction.old_classes[i]==value)return transaction.new_classes[i];return value;};
 if(mapped(old_identity.parent)!=new_identity.parent&&(!s.preparing_requests||!s.HierarchyRequired()))
  return Fail(error,"Schema superclass change lacks the mandatory hierarchy proof request");
 // Independent member/modifier proofs inspect the original loaded caller graph.
 // Superclass/interface evolution is attested separately by the mandatory
 // HierarchyGraph requirement and its actual rooted native authority.
 evolution::ClassSchema before,after;if(!native_schema_policy::ReadNativeDeclarations(*s.bindings.metadata,*s.bindings.methods,transaction.old_classes[pair],&before,error)||!native_schema_policy::ReadNativeDeclarations(*s.bindings.metadata,*s.bindings.methods,transaction.new_classes[pair],&after,error)||!MembersEqual(before.methods,s.nodes[target].schema.methods)||!MembersEqual(before.fields,s.nodes[target].schema.fields))return Fail(error,"Schema guard native declarations differ from independent public stage");
 const auto parent=[&](size_t node){return node==absent?absent:roots.parents[node];};
 const auto subtype=[&](size_t node){
  std::set<size_t> visited;
  const auto walk=[&](const auto& self,size_t current)->bool {
   if(current==target)return true;if(current==absent||!visited.insert(current).second)return false;
   if(self(self,parent(current)))return true;
   for(auto iface:s.nodes[current].interfaces){void* value=nullptr;if(!s.roots.Read(iface,&value))continue;auto found=class_index.find(value);if(found!=class_index.end()&&self(self,found->second))return true;}
   return false;
  };return walk(walk,node);
 };
 const auto parent_token=[&](size_t token){if(!token)return size_t{0};const auto value=parent(token-1);return value==absent?size_t{0}:value+1;};
 using Kind=native_schema_policy::GuardKind;
 switch(requirement.kind){
  case Kind::LoadedSubclasses:
   for(size_t n=0;n<s.nodes.size();++n)if(n!=target&&s.nodes[n].affected&&subtype(n))return Fail(error,"Cannot make a Class final while an actual loaded subclass exists");return true;
  case Kind::FinalMethodOverrides:case Kind::AbstractMethodDependents:{
   auto method=std::find_if(before.methods.begin(),before.methods.end(),[&](const auto& m){return m.name==requirement.member_name&&m.descriptor==requirement.descriptor;});if(method==before.methods.end()||!Virtual(*method))return Fail(error,"Loaded-method guard lacks its exact virtual declaration");
   for(size_t n=0;n<s.nodes.size();++n){if(n==target||!s.nodes[n].affected||!subtype(n))continue;
    auto public_method=std::find_if(s.nodes[target].schema.methods.begin(),s.nodes[target].schema.methods.end(),[&](const auto& candidate){return candidate.name==method->name&&candidate.descriptor==method->descriptor;});
    if(public_method==s.nodes[target].schema.methods.end())return Fail(error,"Method requirement is outside public declaration proof");
    void* target_method=roots.declared[target][size_t(public_method-s.nodes[target].schema.methods.begin())];
    const auto matches=[&](size_t cursor,bool concrete){for(void* native:roots.all_methods[cursor]){uint32_t flags=method_metadata::ReadAccessFlags(native);if(flags&(8u|2u|0x10000u))continue;if(concrete&&(s.bindings.method_class(s.bindings.owner,native)!=roots.classes[cursor]||(flags&0x400u)))continue;if(s.bindings.same_method(s.bindings.owner,native,target_method))return true;}return false;};
    if(requirement.kind==Kind::FinalMethodOverrides){const bool accessible=loaded_member_guards::Accessible(n+1,target+1,method->access,parent_token,[&](size_t a,size_t b){--a;--b;return roots.loaders[a]==roots.loaders[b]&&Package(s.nodes[a].schema.descriptor)==Package(s.nodes[b].schema.descriptor);},(s.nodes[n].schema.access&0x200u)!=0);if(accessible&&matches(n,false))return Fail(error,"A loaded subclass already overrides the requested final method");}
    else if(!(s.nodes[n].schema.access&0x400u)){const bool concrete=loaded_member_guards::ConcreteBelow(n+1,target+1,parent_token,[&](size_t token){return matches(token-1,true);});if(!concrete)return Fail(error,"A loaded concrete subclass depends on the requested abstract method");}
   }
   if(requirement.kind==Kind::AbstractMethodDependents)return s.bindings.abstract_instances&&s.bindings.abstract_instances->Ready()&&s.bindings.abstract_instances->Validate(transaction,pair,*s.bindings.metadata,error);return true;
  }
  case Kind::ExistingInstances:return s.bindings.abstract_instances&&s.bindings.abstract_instances->Ready()&&s.bindings.abstract_instances->Validate(transaction,pair,*s.bindings.metadata,error);
  case Kind::FinalFields:
   if(!s.bindings.final_fields||!s.bindings.final_fields->Verify(transaction,pair,loader,before,after,requirement,error))return false;
   s.final_fields_proven=true;return true;
  case Kind::InterfaceIdentities:return s.Interfaces(roots,transaction,error);
  case Kind::HierarchyGraph:return Fail(error,"Hierarchy changes require the owned rooted allocator/receiver/type-cache proof service");
  case Kind::AccessPermissions:{
   if(!s.bindings.access_permissions||!s.bindings.access_permissions->Verify(transaction,error))return Fail(error,"Access permission changes require the proven process interpreter/deopt owner");
   // A bootstrap ClassLinker lookup cannot resolve a Class defined only in
   // an application/custom defining-loader namespace. Its dynamic reflection,
   // JNI and handle consumers remain governed by their independent entry guards.
   bool child_targets=true;for(void* klass:transaction.old_classes){auto node=class_index.find(klass);if(node==class_index.end()||!roots.loaders[node->second])child_targets=false;}
   std::vector<uint32_t*> slots,readonly;
   const auto add=[&](void* method)->bool {
    void* declaring=s.bindings.method_class(s.bindings.owner,method);auto native_owner=class_index.find(declaring);
    if(native_owner!=class_index.end()){if(child_targets&&!roots.loaders[native_owner->second])return true;}
    else if(std::find(transaction.new_classes.begin(),transaction.new_classes.end(),declaring)==transaction.new_classes.end())return false;
    const uint32_t flags=method_metadata::ReadAccessFlags(method);if(flags&native_flag)return true;
    auto* slot=reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(method)+4);
    if(reinterpret_cast<uintptr_t>(slot)%alignof(uint32_t))return false;
    if(s.bindings.writable(s.bindings.owner,slot,sizeof(uint32_t))){slots.push_back(slot);return true;}
    // Source SetSkipAccessChecks is an unconditional atomic flag write; a
    // genuinely readonly clear flag cannot be enabled by that normal path.
    // Retain a final witness instead of requiring a needless readonly write.
    if(flags&skip_access_checks)return false;readonly.push_back(slot);return true;
   };
   for(size_t n=0;n<roots.all_methods.size();++n){
    for(void* method:roots.all_methods[n])if(!add(method))return Fail(error,"A potential caller has a readonly enabled access shortcut");}
   // Reserve every mutable new slot, including shortcuts currently clear:
   // verification may enable them between Collected and final suspension.
   for(void* klass:transaction.new_classes){evolution::ClassSchema schema;native_method_table::View table;
    if(!native_schema_policy::ReadNativeDeclarations(*s.bindings.metadata,*s.bindings.methods,klass,&schema,error,&table))return false;
    for(uint32_t i=0;i<table.methods.count;++i)if(!add(table.methods.At(i)))return Fail(error,"Replacement method has a readonly enabled access shortcut");}
   std::sort(slots.begin(),slots.end());slots.erase(std::unique(slots.begin(),slots.end()),slots.end());
   std::sort(readonly.begin(),readonly.end());readonly.erase(std::unique(readonly.begin(),readonly.end()),readonly.end());
   s.readonly_access_slots=std::move(readonly);
   s.access_slots=std::move(slots);s.access_proven=true;return true;
  }
 }
 return Fail(error,"Unknown native schema guard requirement");
 };
 if(!prove())return false;
 if(std::none_of(s.grants.begin(),s.grants.end(),[&](const auto& grant){return grant.pair==pair&&grant.loader==loader&&grant.requirement.kind==requirement.kind&&grant.requirement.member_name==requirement.member_name&&grant.requirement.descriptor==requirement.descriptor;}))s.grants.push_back({pair,loader,requirement});
 return true;
}
bool Verifier::PrepareGuardProofs(const source_commit::Plan& transaction,const std::vector<native_schema_policy::GuardRequest>& requests,std::string* error)const {
 auto& s=*state_;if(s.frozen||!s.sealed||s.preparing_requests||s.requirements_plan||!redefinition_gate::Owned(s.bindings.vm)||!SamePlan(transaction,s.expected))
  return Fail(error,"Independent guard preparation lacks the ordinary sealed source Plan");
 // Derive the complete requirements from the actual paired native declarations,
 // so a caller cannot omit hierarchy while asking only for a member proof.
 std::vector<native_schema_policy::GuardRequest> required;
 for(size_t i=0;i<transaction.old_classes.size();++i){evolution::ClassSchema before,after;native_schema_policy::Plan policy;field_copy::CopyClassView identity;
  if(!native_schema_policy::ReadNativeDeclarations(*s.bindings.metadata,*s.bindings.methods,transaction.old_classes[i],&before,error)||
     !native_schema_policy::ReadNativeDeclarations(*s.bindings.metadata,*s.bindings.methods,transaction.new_classes[i],&after,error)||
     !s.bindings.metadata->ClassIdentity(transaction.old_classes[i],&identity,error)||!native_schema_policy::Plan::Prepare(before,after,&policy,error))return false;
  for(const auto& r:policy.Requirements())required.push_back({i,identity.loader,r});
 }
 s.required_requests=std::move(required);
 if(!s.CompleteRequests(requests)){s.required_requests.clear();return Fail(error,"Ordinary guard request set omitted or changed a required native proof");}
 std::vector<native_schema_policy::GuardRequest> independent;independent.reserve(requests.size());
 for(const auto& request:requests)if(request.requirement.kind!=native_schema_policy::GuardKind::HierarchyGraph)independent.push_back(request);
 s.preparing_requests=&requests;
 struct Release{State& s;~Release(){s.preparing_requests=nullptr;}} release{s};
 if(!VerifyBatch(transaction,independent,error)){s.required_requests.clear();s.grants.clear();return false;}
 s.requirements_plan=&transaction;return true;
}
bool Verifier::UseHierarchyAuthority(const HierarchyAuthority* authority)noexcept {
 auto& s=*state_;if(!authority||!s.requirements_plan||!s.frozen||!s.sealed||!s.refreshed||!redefinition_gate::Owned(s.bindings.vm)||
    (s.hierarchy_authority&&s.hierarchy_authority!=authority))return false;
 s.hierarchy_authority=authority;return true;
}

bool Verifier::VerifyBatch(const source_commit::Plan& transaction,const std::vector<native_schema_policy::GuardRequest>& requests,std::string* error)const {
 auto& s=*state_;
 if(s.requirements_plan&&(!s.preparing_requests&&(&transaction!=s.requirements_plan||!s.CompleteRequests(requests))))
  return Fail(error,"Schema batch changed or omitted a prepared guard requirement");
 if(requests.empty())return true;
 if(s.batch_snapshot||!s.sealed||!redefinition_gate::Owned(s.bindings.vm)||!SamePlan(transaction,s.expected))return Fail(error,"Schema batch lacks an exact nonnested native proof episode");
 for(const auto& request:requests)if(request.class_pair>=transaction.old_classes.size())return Fail(error,"Schema batch contains an invalid Class pair");
 Snapshot local;const Snapshot* episode=nullptr;
 if(s.frozen){if(!s.refreshed||!s.FrozenProof())return Fail(error,"Schema batch frozen proof changed");episode=&s.frozen_snapshot;}
 else{if(!s.Roots(&local,error))return false;episode=&local;}
 // This borrowed snapshot survives only these synchronous native consumers.
 // Repeated calls, JNI/root registration and final refresh cannot reuse it.
 s.batch_snapshot=episode;
 struct Release {State& state;~Release(){state.batch_snapshot=nullptr;}} release{s};
 for(const auto& request:requests)if(!Verify(transaction,request.class_pair,request.loader,request.requirement,error))return false;
 return true;
}

bool Verifier::UseStoppedReadContext(const modern_field_metadata::ReadContext* context) noexcept {
 auto& s=*state_;size_t extent=0;
 if(!context||!context->extent||!context->owner||!s.frozen||!s.sealed||
    !redefinition_gate::Owned(s.bindings.vm)||s.expected.old_classes.empty()||
    (s.stopped_context&&s.stopped_context!=context)||
    !context->extent(context->owner,s.expected.old_classes.front(),&extent)||!extent)return false;
 s.stopped_context=context;return true;
}

bool Verifier::CollectManagedRoots(const prepared_roots::Registrar& registrar,std::string* error){
 auto& s=*state_;if(!s.sealed||s.frozen||!redefinition_gate::Owned(s.bindings.vm)||!s.Roots(&s.frozen_snapshot,error))return false;
 const bool split=s.bindings.metadata->StorageModel()==modern_field_metadata::Model::kSplit;
 const auto& layout=s.bindings.methods->model==method_identity::Model::kMethod11?method_metadata::kAndroid11:method_metadata::kAndroid12Onward;
 NativeOrdinals class_index;if(!IndexNativeClasses(s.frozen_snapshot.classes,&class_index,error))return false;
 s.class_proofs.resize(s.nodes.size());s.domain_seen.resize(s.nodes.size()+s.error_census.Size());
 for(size_t n=0;n<s.nodes.size();++n){void* klass=s.frozen_snapshot.classes[n];auto& proof=s.class_proofs[n];
  proof.methods=reinterpret_cast<void*>(uintptr_t(Read<uint64_t>(klass,48)));proof.instances=reinterpret_cast<void*>(uintptr_t(Read<uint64_t>(klass,40)));
  proof.statics=split?reinterpret_cast<void*>(uintptr_t(Read<uint64_t>(klass,56))):proof.instances;
  if((proof.instances&&!s.bindings.readable(s.bindings.owner,proof.instances,4))||(proof.statics&&!s.bindings.readable(s.bindings.owner,proof.statics,4)))return false;
  proof.instance_count=proof.instances?Read<uint32_t>(proof.instances):0;proof.static_count=proof.statics?Read<uint32_t>(proof.statics):0;
  proof.flags=native_schema_policy::JavaFlags(Read<uint32_t>(klass,split?64:56));
  proof.kind=Read<uint32_t>(klass,split?64:56)&0x40000u;
  proof.primitive=Read<uint32_t>(klass,split?offsetof(class_metadata::Class11,primitive_type):offsetof(class_metadata::Class17,primitive_type));
  proof.dex_cache=s.bindings.metadata->Reference(klass,16);proof.interfaces=s.bindings.metadata->Reference(klass,24);
  if(proof.interfaces){if(!s.bindings.readable(s.bindings.owner,proof.interfaces,12))return false;const uint32_t length=Read<uint32_t>(proof.interfaces,8);if(length%2||!s.bindings.readable(s.bindings.owner,proof.interfaces,12+size_t(length)*4))return false;
   for(uint32_t i=0;i<length/2;++i)proof.interface_classes.push_back(s.bindings.metadata->Reference(proof.interfaces,12+size_t(i)*8));}
  for(size_t i=0;i<s.nodes[n].field_ids.size();++i){void* field=s.bindings.decode_native_field(s.bindings.owner,klass,s.nodes[n].field_ids[i]);
   const auto* native=static_cast<const abi::arm64::Field*>(field);s.member_proofs.push_back({field,n,jvmti_field_flags::Public(native->access_flags),native->dex_field_index,native->offset,true,s.nodes[n].field_ids[i]});}
  for(void* method:s.frozen_snapshot.all_methods[n]){void* owner=s.bindings.method_class(s.bindings.owner,method);auto it=class_index.find(owner);method_metadata::Identity identity;
   if(it==class_index.end()||!method_metadata::ReadIdentity(layout,method,layout.size,&identity))return false;
   s.member_proofs.push_back({method,it->second,jvmti_method_flags::Public(identity.access_flags),identity.dex_method_index,0,false});}
 }
 for(void* klass:s.expected.new_classes){State::InterfaceProof proof;proof.klass=klass;proof.table=s.bindings.metadata->Reference(klass,24);
  if(proof.table){if(!s.bindings.readable(s.bindings.owner,proof.table,12))return false;const uint32_t length=Read<uint32_t>(proof.table,8);
   if(length%2||!s.bindings.readable(s.bindings.owner,proof.table,12+size_t(length)*4))return false;
   for(uint32_t n=0;n<length/2;++n)proof.values.push_back(s.bindings.metadata->Reference(proof.table,12+size_t(n)*8));}
  s.replacement_interfaces.push_back(std::move(proof));}
 if(!s.error_census.Freeze(s.roots,registrar))return Fail(error,"Read-only erroneous Class census could not freeze its actual roots");
 if(!rooted_schema_plan::CollectBindings(&s.expected,registrar,&s.expected_roots))return false;
 for(auto* values:{&s.frozen_snapshot.classes,&s.frozen_snapshot.loaders})for(auto& value:*values)if(!s.frozen_roots.Add(&value,registrar))return false;
 for(auto& proof:s.class_proofs){if(!s.frozen_roots.Add(&proof.dex_cache,registrar)||!s.frozen_roots.Add(&proof.interfaces,registrar))return false;
  for(auto& value:proof.interface_classes)if(!s.frozen_roots.Add(&value,registrar))return false;}
 for(auto& proof:s.replacement_interfaces){if(!s.frozen_roots.Add(&proof.klass,registrar)||!s.frozen_roots.Add(&proof.table,registrar))return false;for(auto& value:proof.values)if(!s.frozen_roots.Add(&value,registrar))return false;}
 for(auto& grant:s.grants)if(!s.frozen_roots.Add(&grant.loader,registrar))return false;
 for(auto& request:s.required_requests)if(!s.frozen_roots.Add(&request.loader,registrar))return false;
 s.frozen_roots.Seal();s.frozen=true;return s.FrozenProof();
}

bool Verifier::CanRefreshManagedRoots(const prepared_roots::Resolver& resolver)const noexcept{
 const auto& s=*state_;return s.frozen&&s.sealed&&redefinition_gate::Owned(s.bindings.vm)&&s.expected_roots.CanRefresh(resolver)&&s.frozen_roots.CanRefresh(resolver)&&s.error_census.CanRefresh(resolver);
}
bool Verifier::RefreshManagedRoots(const source_commit::Plan& plan,const prepared_roots::Resolver& resolver) noexcept{
 auto& s=*state_;s.refreshed=false;if(!CanRefreshManagedRoots(resolver))return false;
 s.expected_roots.Refresh(resolver);s.frozen_roots.Refresh(resolver);s.error_census.Refresh(resolver);
 if(!SamePlan(plan,s.expected)||!s.FrozenProof())return false;
 if((s.final_fields_proven||s.aliases_proven)&&(!s.bindings.final_fields||
    !s.bindings.final_fields->RefreshPlan(plan,nullptr)||!s.bindings.final_fields->VerifyAliases(plan,nullptr)))return false;
 if(s.access_proven&&(!s.bindings.access_permissions||!s.bindings.access_permissions->Verify(plan,nullptr)))return false;
 s.refreshed=true;return true;
}

bool Verifier::VerifyHierarchyDomain(const source_commit::Plan&plan,
 const native_hierarchy_graph::Graph&graph,void*interface,
 const modern_field_metadata::ReadContext&read)const noexcept{
 const auto&s=*state_;
 if(!interface||!s.collected||!s.sealed||!s.frozen||!s.refreshed||!s.stopped_context||
    s.stopped_context->owner!=read.owner||s.stopped_context->extent!=read.extent||
    !SamePlan(plan,s.expected)||!s.FrozenProof()||!s.error_census.Fresh(s.roots))return false;
 size_t ordinary=0;for(const auto&n:s.nodes)if(!n.schema.descriptor.empty()&&n.schema.descriptor.front()=='L')++ordinary;
 if(graph.nodes.size()!=ordinary+s.error_census.Size()||s.domain_seen.size()!=s.nodes.size()+s.error_census.Size())return false;
 if(++s.domain_epoch==0){std::fill(s.domain_seen.begin(),s.domain_seen.end(),0);s.domain_epoch=1;}
 // Join two independently rooted censuses by current Class identity, never
 // descriptor equality. Descriptor buckets only narrow the immutable search.
 for(const auto&n:graph.nodes){
  if(!n.klass||!n.RootedClass())return false;
  if(n.resolved_error){
   const native_interface_dispatch_domain::detail::ErrorCensus::Node*matched=nullptr;size_t matched_index=SIZE_MAX;
   for(size_t i=0;i<s.error_census.Size();++i){const auto&error=s.error_census.Nodes()[i];if(error.klass==n.klass){if(matched)return false;matched=&error;matched_index=s.nodes.size()+i;}}
   if(matched_index==SIZE_MAX||s.domain_seen[matched_index]==s.domain_epoch)return false;s.domain_seen[matched_index]=s.domain_epoch;
   if(!matched||n.affected||n.replacement||matched->descriptor!=n.descriptor||matched->native_loader!=n.loader||
      native_schema_policy::JavaFlags(matched->flags)!=native_schema_policy::JavaFlags(n.public_flags.value_or(n.flags)))return false;
   const void*parent=n.parent==SIZE_MAX?nullptr:n.parent<graph.nodes.size()?graph.nodes[n.parent].klass:nullptr;
   if((n.parent!=SIZE_MAX&&n.parent>=graph.nodes.size())||matched->native_parent!=parent||matched->native_interfaces.size()!=n.interfaces.size())return false;
   for(size_t j=0;j<n.interfaces.size();++j)if(n.interfaces[j]>=graph.nodes.size()||matched->native_interfaces[j]!=graph.nodes[n.interfaces[j]].klass)return false;
   continue;
  }
  size_t index=SIZE_MAX;
  const auto found=s.public_index.Find(n.descriptor,[&](size_t candidate){return candidate<s.frozen_snapshot.classes.size()&&s.frozen_snapshot.classes[candidate]==n.klass;},&index);
  if(found!=PublicClassIndex::Result::Unique||index>=s.nodes.size()||s.domain_seen[index]==s.domain_epoch)return false;
  s.domain_seen[index]=s.domain_epoch;const auto&node=s.nodes[index];
  if(node.affected!=n.affected||s.frozen_snapshot.loaders[index]!=n.loader||
     node.schema.access!=native_schema_policy::JavaFlags(n.public_flags.value_or(n.flags)))return false;
  const size_t parent=s.frozen_snapshot.parents[index];
  if((parent==SIZE_MAX)!=(n.parent==SIZE_MAX)||
     (parent!=SIZE_MAX&&(parent>=s.nodes.size()||n.parent>=graph.nodes.size()||s.frozen_snapshot.classes[parent]!=graph.nodes[n.parent].klass)))return false;
  if(node.interfaces.size()!=n.interfaces.size())return false;
  for(size_t j=0;j<node.interfaces.size();++j){void*value=nullptr;
   if(n.interfaces[j]>=graph.nodes.size()||!s.roots.Read(node.interfaces[j],&value)||value!=graph.nodes[n.interfaces[j]].klass)return false;}
 }
 // Fixed ART class_linker.cc initializes the common array IfTable from the
 // actual bootstrap Cloneable/Serializable Class roots (ed6c006, 920--932).
 // Validate both current identities, empty marker ancestry and every observed
 // array table. A marker itself cannot grant an interface-method domain.
 void*markers[2]{};
 for(const auto&n:graph.nodes)if(!n.loader&&(n.descriptor=="Ljava/lang/Cloneable;"||n.descriptor=="Ljava/io/Serializable;")){
  const size_t index=n.descriptor=="Ljava/lang/Cloneable;"?0:1;
  if(markers[index]||n.affected||n.resolved_error||!(n.flags&0x200u)||!n.interfaces.empty())return false;
  field_copy::CopyClassView identity{};
  if(!s.bindings.metadata->ClassIdentityVerified(n.klass,n.descriptor.c_str(),nullptr,nullptr,nullptr,&identity,nullptr,&read)||identity.loader)return false;
  markers[index]=n.klass;
 }
 if(!markers[0]||!markers[1]||markers[0]==markers[1]||interface==markers[0]||interface==markers[1])return false;
 for(size_t i=0;i<s.nodes.size();++i)if(!s.nodes[i].schema.descriptor.empty()&&s.nodes[i].schema.descriptor.front()=='['){
  const auto&interfaces=s.class_proofs[i].interface_classes;
  if(interfaces.size()!=2)return false;
  if(!((interfaces[0]==markers[0]&&interfaces[1]==markers[1])||(interfaces[0]==markers[1]&&interfaces[1]==markers[0])))return false;
 }
 return true;
}

void Verifier::BeginCommit(const source_commit::Plan& transaction)const{
 const auto& s=*state_;if(!s.sealed||(s.frozen&&!s.refreshed)||!redefinition_gate::Owned(s.bindings.vm)||!SamePlan(transaction,s.expected))std::abort();
 if(s.access_proven){s.bindings.access_permissions->BeginCommit(transaction);for(auto slot:s.access_slots)__atomic_fetch_and(slot,~skip_access_checks,__ATOMIC_RELAXED);}
 if(s.final_fields_proven||s.aliases_proven)s.bindings.final_fields->BeginCommit(transaction);
}
}
