#include "platform/common/native_schema_guard_verifier.h"
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
#include <set>
#include <limits>
#include <cstdlib>
#include <cstdio>
namespace artdcevm::native_schema_guard_verifier {
namespace {
bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
constexpr size_t absent=SIZE_MAX;
constexpr uint32_t skip_access_checks=0x00080000u,native_flag=0x100u;
struct Node {
 bounded_managed_roots::Slot root{},loader{};
 evolution::ClassSchema schema;
 std::vector<jmethodID> method_ids;
 std::vector<jfieldID> field_ids;
 std::vector<bounded_managed_roots::Slot> interfaces;
 bounded_managed_roots::Slot parent{},component{},class_name{};
 std::string class_name_text,negative_class_name;
 bool affected=false;
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
bool MemberEqual(const evolution::Member& a,const evolution::Member& b){return a.name==b.name&&a.descriptor==b.descriptor&&native_schema_policy::JavaFlags(a.access)==native_schema_policy::JavaFlags(b.access);}
bool MembersEqual(const std::vector<evolution::Member>& a,const std::vector<evolution::Member>& b){return a.size()==b.size()&&std::all_of(a.begin(),a.end(),[&](const auto& m){return std::count_if(b.begin(),b.end(),[&](const auto& n){return MemberEqual(m,n);})==1;});}
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
  out->method_ids.push_back(methods[i]);out->schema.methods.push_back({text.name,text.signature,native_schema_policy::JavaFlags(uint32_t(flags)),UINT32_MAX});
 }
 jint field_count=0; jfieldID* fields=nullptr;
 if(ti->GetClassFields(klass,&field_count,&fields)!=JVMTI_ERROR_NONE||field_count<0)return Fail(error,"Complete public declared field IDs unavailable");
 struct Fields {jvmtiEnv* ti;jfieldID* p;~Fields(){if(p)ti->Deallocate(reinterpret_cast<unsigned char*>(p));}} release_fields{ti,fields};
 for(jint i=0;i<field_count;++i){Strings text{ti};jint flags=0;
  if(!fields[i]||ti->GetFieldName(klass,fields[i],&text.name,&text.signature,nullptr)!=JVMTI_ERROR_NONE||!text.name||!text.signature||ti->GetFieldModifiers(klass,fields[i],&flags)!=JVMTI_ERROR_NONE)return Fail(error,"Public field identity/modifiers unavailable");
  if(bindings&&(!bindings->stage_field||!bindings->stage_field(bindings->owner,env,klass,fields[i],(flags&8)!=0,error)))return false;
  out->field_ids.push_back(fields[i]);out->schema.fields.push_back({text.name,text.signature,native_schema_policy::JavaFlags(uint32_t(flags)),UINT32_MAX});
 }
 if(env->ExceptionCheck())return Fail(error,"Public schema stage left a pending exception");
 if(std::set<jmethodID>(out->method_ids.begin(),out->method_ids.end()).size()!=out->method_ids.size()||std::set<jfieldID>(out->field_ids.begin(),out->field_ids.end()).size()!=out->field_ids.size())return Fail(error,"Duplicate public member IDs");
 return true;
}
bool SamePublic(const Node& a,const Node& b){return a.schema.descriptor==b.schema.descriptor&&a.schema.access==b.schema.access&&MembersEqual(a.schema.fields,b.schema.fields)&&MembersEqual(a.schema.methods,b.schema.methods)&&a.method_ids==b.method_ids&&a.field_ids==b.field_ids;}
}
struct Verifier::State {
 Bindings bindings{};std::vector<Node> nodes;std::vector<bounded_managed_roots::Slot> inputs;bounded_managed_roots::Store roots;
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
  const auto Mapped=[&](const void* address,size_t bytes){return address&&bindings.readable&&bindings.readable(bindings.owner,address,bytes);};
  if(!redefinition_gate::Owned(bindings.vm)||!bindings.metadata||!bindings.metadata->Calibrated()||!bindings.methods||bindings.methods->identities.size()<3||bindings.methods->readable_extents.size()!=bindings.methods->identities.size()||!bindings.decode_global||!bindings.decode_method||!bindings.decode_native_field||!bindings.method_class||!bindings.same_method||!bindings.readable||!bindings.writable||!bindings.metadata||!bindings.methods)return Fail(error,"Schema verifier lacks the calibrated native process owner");
  Snapshot result;result.classes.resize(nodes.size());result.loaders.resize(nodes.size());result.parents.resize(nodes.size(),absent);result.declared.resize(nodes.size());result.all_methods.resize(nodes.size());
  for(size_t n=0;n<nodes.size();++n){
   if(!roots.Read(nodes[n].root,&result.classes[n])||!result.classes[n])return Fail(error,"Staged Class strong root could not be decoded");
   if(nodes[n].loader&&!roots.Read(nodes[n].loader,&result.loaders[n]))return Fail(error,"Staged ClassLoader strong root could not be decoded");

  }
  if(std::set<void*>(result.classes.begin(),result.classes.end()).size()!=nodes.size())return Fail(error,"Duplicate staged native Class identity");
  const auto& layout=bindings.methods->model==method_identity::Model::kMethod11?method_metadata::kAndroid11:method_metadata::kAndroid12Onward;
  for(size_t n=0;n<nodes.size();++n){const auto& node=nodes[n];void* klass=result.classes[n];
   if(node.parent){void* parent=nullptr;if(!roots.Read(node.parent,&parent))return false;auto it=std::find(result.classes.begin(),result.classes.end(),parent);if(it==result.classes.end())return Fail(error,"Staged ancestor is outside complete loaded roots");result.parents[n]=size_t(it-result.classes.begin());}
   field_copy::CopyClassView identity;
   std::string identity_error;void* terminal=klass;size_t dimension=0;
   while(dimension<node.schema.descriptor.size()&&node.schema.descriptor[dimension]=='['){
    if(!Mapped(terminal,offsetof(class_metadata::Class11,component_type)+4))return Fail(error,"Array component chain has unreadable Class prefix");
    terminal=bindings.metadata->Reference(terminal,offsetof(class_metadata::Class11,component_type));++dimension;
   }
   void* proxy_name=nullptr;const char* name=nullptr;const char* negative=nullptr;
   const auto terminal_node=std::find(result.classes.begin(),result.classes.end(),terminal);
   if(terminal_node!=result.classes.end()){const auto& source=nodes[size_t(terminal_node-result.classes.begin())];
    if(source.class_name){if(!roots.Read(source.class_name,&proxy_name))return Fail(error,"Public proxy name root could not be decoded");name=source.class_name_text.c_str();negative=source.negative_class_name.c_str();}}
   const bool identified=bindings.metadata->ClassIdentityVerified(klass,node.schema.descriptor.c_str(),proxy_name,name,negative,&identity,&identity_error);
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
    std::string native_error;const bool text=owner&&bindings.metadata->FieldIdentity(field,&identity,&native_error);
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
     const char* owner_name=mapped?nullptr:"<unmapped>";const auto index=std::find(result.classes.begin(),result.classes.end(),owner);
     if(index!=result.classes.end())owner_name=nodes[size_t(index-result.classes.begin())].schema.descriptor.c_str();
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
    void* owner=bindings.method_class(bindings.owner,method);auto cls=std::find(result.classes.begin(),result.classes.end(),owner);
    if(cls==result.classes.end()||owner==klass)return Fail(error,"Nondeclared native method lacks foreign copied-member ownership");
    const size_t source=size_t(cls-result.classes.begin());
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
  const auto mapped=[&](void* value){for(size_t i=0;i<transaction.old_classes.size();++i)if(transaction.old_classes[i]==value)return transaction.new_classes[i];return value;};
  for(size_t n=0;n<nodes.size();++n){if(!nodes[n].affected)continue;
   std::vector<void*> expected;std::vector<size_t> visiting;
   const auto collect=[&](const auto& self,size_t index)->bool {
    if(std::find(visiting.begin(),visiting.end(),index)!=visiting.end())return Fail(error,"Cyclic staged interface hierarchy");visiting.push_back(index);
    if(roots.parents[index]!=absent&&!self(self,roots.parents[index]))return false;
    for(auto iface:nodes[index].interfaces){void* native=nullptr;if(!this->roots.Read(iface,&native))return false;auto it=std::find(roots.classes.begin(),roots.classes.end(),native);if(it==roots.classes.end())return Fail(error,"Interface identity is outside staged Class roots");
     if(std::find(expected.begin(),expected.end(),native)==expected.end())expected.push_back(native);
     if(!self(self,size_t(it-roots.classes.begin())))return false;
    }visiting.pop_back();return true;
   };
   if(!collect(collect,n))return false;
   for(bool after:{false,true}){void* klass=after?mapped(roots.classes[n]):roots.classes[n];void* table=bindings.metadata->Reference(klass,24);uint32_t length=0;
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
bool Verifier::Stage(JNIEnv* env,jvmtiEnv* ti,const std::vector<jclass>& inputs,const Bindings& bindings,std::string* error){
 if(!env||!ti||inputs.empty()||!redefinition_gate::Owned(bindings.vm)||env->ExceptionCheck()||!state_->nodes.empty())return Fail(error,"Invalid or repeated schema graph stage");state_->bindings=bindings;state_->roots.Bind(bindings.roots);
 for(auto klass:inputs){bounded_managed_roots::Slot slot;if(!state_->roots.Stage(env,klass,&slot,error))return false;state_->inputs.push_back(slot);}
 struct NameMethods {JNIEnv* env;jclass proxy=nullptr,klass=nullptr;~NameMethods(){if(proxy)env->DeleteLocalRef(proxy);if(klass)env->DeleteLocalRef(klass);}} public_names{env};
 public_names.proxy=env->FindClass("java/lang/reflect/Proxy");public_names.klass=env->FindClass("java/lang/Class");
 if(!public_names.proxy||!public_names.klass||env->ExceptionCheck())return Fail(error,"Public Class kind/name service unavailable");
 const auto is_proxy=env->GetStaticMethodID(public_names.proxy,"isProxyClass","(Ljava/lang/Class;)Z");
 const auto get_name=env->GetMethodID(public_names.klass,"getName","()Ljava/lang/String;");
 const auto get_component=env->GetMethodID(public_names.klass,"getComponentType","()Ljava/lang/Class;");
 if(!is_proxy||!get_name||!get_component||env->ExceptionCheck())return Fail(error,"Public Class kind/name declarations unavailable");
 std::map<std::string,std::vector<size_t>> staged_names;
 for(;;){
 const size_t before=state_->nodes.size();size_t known=0;
 jint count=0;jclass* classes=nullptr;if(ti->GetLoadedClasses(&count,&classes)!=JVMTI_ERROR_NONE||count<0)return Fail(error,"Complete loaded caller graph unavailable");
 struct Classes {JNIEnv* env;jvmtiEnv* ti;jint count;jclass* values;~Classes(){for(jint i=0;i<count;++i)env->DeleteLocalRef(values[i]);if(values)ti->Deallocate(reinterpret_cast<unsigned char*>(values));}} release{env,ti,count,classes};
 for(jint i=0;i<count;++i){jint status=0;if(ti->GetClassStatus(classes[i],&status)!=JVMTI_ERROR_NONE)return false;if(!CallerClass(status))continue;
  if(!PreparedCaller(status))return Fail(error,"Unprepared loaded Class prevents complete caller provenance");
  Strings identity{ti};if(ti->GetClassSignature(classes[i],&identity.signature,nullptr)!=JVMTI_ERROR_NONE||!identity.signature)return Fail(error,"Loaded Class signature unavailable while closing staging");
  bool existing=false;const auto candidates=staged_names.find(identity.signature);
  if(candidates!=staged_names.end())for(size_t index:candidates->second)existing|=state_->roots.Same(env,state_->nodes[index].root,classes[i]);
  if(env->ExceptionCheck())return false;
  if(existing){++known;continue;}
  Node node;if(!PublicMembers(env,ti,classes[i],&node,error,&bindings))return false;if(!state_->roots.Stage(env,classes[i],&node.root,error))return false;
  state_->nodes.push_back(std::move(node));auto& stored=state_->nodes.back();staged_names[stored.schema.descriptor].push_back(state_->nodes.size()-1);
  jobject loader=nullptr;if(ti->GetClassLoader(classes[i],&loader)!=JVMTI_ERROR_NONE)return false;if(loader){const bool pass=state_->roots.Stage(env,loader,&stored.loader,error);env->DeleteLocalRef(loader);if(!pass)return false;}
  if(status&JVMTI_CLASS_STATUS_ARRAY){auto component=env->CallObjectMethodA(classes[i],get_component,nullptr);
   const bool pass=component&&!env->ExceptionCheck()&&state_->roots.Stage(env,component,&stored.component,error);if(component)env->DeleteLocalRef(component);if(!pass)return Fail(error,"Public array component identity unavailable");}
  jvalue class_arg{};class_arg.l=classes[i];const bool proxy=env->CallStaticBooleanMethodA(public_names.proxy,is_proxy,&class_arg);
  if(env->ExceptionCheck())return Fail(error,"Public proxy Class classification failed");
  if(proxy){
   struct Name {JNIEnv* env;jstring value;const char* chars=nullptr;~Name(){if(chars)env->ReleaseStringUTFChars(value,chars);if(value)env->DeleteLocalRef(value);}} name{env,static_cast<jstring>(env->CallObjectMethodA(classes[i],get_name,nullptr))};
   if(!name.value||env->ExceptionCheck())return Fail(error,"Public proxy Class name unavailable");
   name.chars=env->GetStringUTFChars(name.value,nullptr);
   if(!name.chars||env->ExceptionCheck())return false;
   stored.class_name_text=name.chars;stored.negative_class_name=stored.class_name_text+"!";
   if(!state_->roots.Stage(env,name.value,&stored.class_name,error))return false;
  }
  auto parent=env->GetSuperclass(classes[i]);if(parent){const bool pass=state_->roots.Stage(env,parent,&stored.parent,error);env->DeleteLocalRef(parent);if(!pass)return false;}
  jint interfaces_count=0;jclass* interfaces=nullptr;if(ti->GetImplementedInterfaces(classes[i],&interfaces_count,&interfaces)!=JVMTI_ERROR_NONE||interfaces_count<0)return false;
  for(jint j=0;j<interfaces_count;++j){bounded_managed_roots::Slot slot;const bool pass=state_->roots.Stage(env,interfaces[j],&slot,error);env->DeleteLocalRef(interfaces[j]);if(!pass){if(interfaces)ti->Deallocate(reinterpret_cast<unsigned char*>(interfaces));return false;}stored.interfaces.push_back(slot);}
  if(interfaces)ti->Deallocate(reinterpret_cast<unsigned char*>(interfaces));
  for(auto input:inputs)if(env->IsAssignableFrom(classes[i],input))stored.affected=true;
  if(env->ExceptionCheck())return false;
 }
 // ToReflectedField resolves declared field types, and interface/root
 // staging may also load Classes. Only newly observed identities are staged;
 // finish after a complete fresh enumeration adds none. There is no SDK,
 // descriptor exemption, repeated mirror replay or arbitrary pass limit.
 if(known!=before)return Fail(error,"Loaded caller graph lost a rooted Class while closing staging");
 if(state_->nodes.size()==before)break;
 }
 if(state_->nodes.empty())return Fail(error,"Empty public Class graph");state_->staged=true;return true;
}
bool Verifier::MatchesAffected(JNIEnv* env,const std::vector<jclass>& actual,std::string* error)const {
 const auto& s=*state_;if(!env||!s.staged||env->ExceptionCheck())return Fail(error,"Closed caller graph is unavailable");
 size_t affected=0;for(const auto& node:s.nodes)if(node.affected){++affected;size_t matches=0;for(auto klass:actual)matches+=s.roots.Same(env,node.root,klass);if(matches!=1)return Fail(error,"Actual Holder graph differs from public staged identities");}
 if(affected!=actual.size())return Fail(error,"Duplicate or unstaged Holder Class");
 return !env->ExceptionCheck();
}
bool Verifier::CheckWindow(JNIEnv* env,jvmtiEnv* ti,const std::vector<jclass>& actual,Window window,std::string* error){
 auto& s=*state_;if(!s.staged||s.collected||(window==Window::Verified&&s.verified)||!redefinition_gate::Owned(s.bindings.vm)||!env||!ti||env->ExceptionCheck()||(window==Window::Collected&&!s.verified))return Fail(error,"Schema window order or context is invalid");
 if(!MatchesAffected(env,actual,error))return false;
 jint count=0;jclass* loaded=nullptr;if(ti->GetLoadedClasses(&count,&loaded)!=JVMTI_ERROR_NONE||count<0)return false;
 struct Loaded {JNIEnv* env;jvmtiEnv* ti;jint count;jclass* values;~Loaded(){for(jint i=0;i<count;++i)env->DeleteLocalRef(values[i]);if(values)ti->Deallocate(reinterpret_cast<unsigned char*>(values));}} release{env,ti,count,loaded};
 size_t live=0;for(jint i=0;i<count;++i){jint status=0;if(ti->GetClassStatus(loaded[i],&status)!=JVMTI_ERROR_NONE)return false;if(!CallerClass(status))continue;
  if(!PreparedCaller(status)){
   Strings text{ti};ti->GetClassSignature(loaded[i],&text.signature,nullptr);
   return ClassWindowFailure(error,"Unprepared Class appeared before native schema sealing",text.signature,status);
  }
  Node current;if(!PublicMembers(env,ti,loaded[i],&current,error))return false;
  auto node=std::find_if(s.nodes.begin(),s.nodes.end(),[&](const auto& n){return n.schema.descriptor==current.schema.descriptor&&s.roots.Same(env,n.root,loaded[i]);});
  if(node==s.nodes.end())return ClassWindowFailure(error,"Class was defined after caller staging; retry complete schema operation",current.schema.descriptor.c_str(),status);
  if(!SamePublic(*node,current))return Fail(error,"Public member flags/identities changed inside the source window");
  jobject loader=nullptr;if(ti->GetClassLoader(loaded[i],&loader)!=JVMTI_ERROR_NONE)return Fail(error,"Paused ClassLoader identity unavailable");const bool same_loader=s.roots.Same(env,node->loader,loader);if(loader)env->DeleteLocalRef(loader);if(!same_loader)return Fail(error,"Public ClassLoader identity changed inside source window");
  auto parent=env->GetSuperclass(loaded[i]);const bool same_parent=s.roots.Same(env,node->parent,parent);if(parent)env->DeleteLocalRef(parent);if(!same_parent)return Fail(error,"Public superclass identity changed inside the source window");
  bool affects=false;for(auto input:s.inputs){auto ref=static_cast<jclass>(s.roots.LocalRef(env,input));if(!ref||env->ExceptionCheck())return false;affects|=env->IsAssignableFrom(loaded[i],ref);env->DeleteLocalRef(ref);}if(affects!=node->affected)return Fail(error,"Loaded Class assignability changed after staging");++live;
 }
 if(live!=s.nodes.size()||env->ExceptionCheck())return Fail(error,"Loaded caller graph lost staged Class identity");
 if(window==Window::Verified)s.verified=true;else s.collected=true;return true;
}
bool Verifier::Seal(const source_commit::Plan& transaction,std::string* error){
 auto& s=*state_;if(!s.collected||s.frozen||!redefinition_gate::Owned(s.bindings.vm)||transaction.old_classes.empty()||transaction.old_classes.size()!=transaction.new_classes.size())return Fail(error,"Schema graph cannot seal before its real collected window");
 s.sealed=false;s.access_proven=false;s.final_fields_proven=false;s.aliases_proven=false;s.access_slots.clear();s.readonly_access_slots.clear();s.grants.clear();
 Snapshot roots;if(!s.Roots(&roots,error))return false;
 const auto Mapped=[&](const void* address,size_t bytes){return address&&s.bindings.readable(s.bindings.owner,address,bytes);};
 size_t count=0;for(size_t n=0;n<s.nodes.size();++n)if(s.nodes[n].affected){++count;if(std::count(transaction.old_classes.begin(),transaction.old_classes.end(),roots.classes[n])!=1)return Fail(error,"Native Holder Class is outside public affected graph");}
 if(count!=transaction.old_classes.size())return Fail(error,"Native Holder repeats or omits a staged Class");
 // Calibrate the common IfTable source prefix from a real positive and empty
 // public transitive graph, never from a successful zero-interface read alone.
 bool positive=false,negative=false;
 for(size_t n=0;n<s.nodes.size();++n){if(s.nodes[n].schema.descriptor.empty()||s.nodes[n].schema.descriptor.front()!='L')continue;
  std::set<void*> expected;std::set<size_t> traversed;
  const auto walk=[&](const auto& self,size_t k)->bool{if(!traversed.insert(k).second)return true;if(roots.parents[k]!=absent&&!self(self,roots.parents[k]))return false;for(auto iface:s.nodes[k].interfaces){void* native=nullptr;if(!s.roots.Read(iface,&native))return false;auto it=std::find(roots.classes.begin(),roots.classes.end(),native);if(it==roots.classes.end())return false;expected.insert(native);if(!self(self,size_t(it-roots.classes.begin())))return false;}return true;};
  if(!walk(walk,n))return Fail(error,"Public interface calibration roots are incomplete");void* table=s.bindings.metadata->Reference(roots.classes[n],24);uint32_t length=0;if(table){if(!Mapped(table,12))return false;length=Read<uint32_t>(table,8);}
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
 if(s.frozen){
  if(!s.refreshed||(!s.batch_snapshot&&!s.FrozenProof()))return false;
  if(!std::any_of(s.grants.begin(),s.grants.end(),[&](const auto& grant){return grant.pair==pair&&grant.loader==loader&&grant.requirement.kind==requirement.kind&&grant.requirement.member_name==requirement.member_name&&grant.requirement.descriptor==requirement.descriptor;}))return false;
  if(requirement.kind==native_schema_policy::GuardKind::ExistingInstances||requirement.kind==native_schema_policy::GuardKind::AbstractMethodDependents)
   return s.bindings.abstract_instances&&s.bindings.abstract_instances->Validate(transaction,pair,*s.bindings.metadata,nullptr);
  return true;
 }
 const auto prove=[&]()->bool {
 Snapshot local;const Snapshot* episode=s.batch_snapshot;if(!episode){if(!s.Roots(&local,error))return false;episode=&local;}const auto& roots=*episode;auto it=std::find(roots.classes.begin(),roots.classes.end(),transaction.old_classes[pair]);if(it==roots.classes.end())return false;const size_t target=size_t(it-roots.classes.begin());
 if(!s.nodes[target].affected||roots.loaders[target]!=loader)return Fail(error,"Schema guard owner/ClassLoader identity differs");
 field_copy::CopyClassView old_identity,new_identity;if(!s.bindings.metadata->ClassIdentity(transaction.old_classes[pair],&old_identity,error)||!s.bindings.metadata->ClassIdentity(transaction.new_classes[pair],&new_identity,error)||old_identity.loader!=loader||new_identity.loader!=loader)return false;
 const auto mapped=[&](void* value){for(size_t i=0;i<transaction.old_classes.size();++i)if(transaction.old_classes[i]==value)return transaction.new_classes[i];return value;};
 if(mapped(old_identity.parent)!=new_identity.parent)return Fail(error,"Schema changed superclass outside the proven rooted transaction");
 evolution::ClassSchema before,after;if(!native_schema_policy::ReadNativeDeclarations(*s.bindings.metadata,*s.bindings.methods,transaction.old_classes[pair],&before,error)||!native_schema_policy::ReadNativeDeclarations(*s.bindings.metadata,*s.bindings.methods,transaction.new_classes[pair],&after,error)||!MembersEqual(before.methods,s.nodes[target].schema.methods)||!MembersEqual(before.fields,s.nodes[target].schema.fields))return Fail(error,"Schema guard native declarations differ from independent public stage");
 const auto parent=[&](size_t node){return node==absent?absent:roots.parents[node];};
 const auto subtype=[&](size_t node){
  std::set<size_t> visited;
  const auto walk=[&](const auto& self,size_t current)->bool {
   if(current==target)return true;if(current==absent||!visited.insert(current).second)return false;
   if(self(self,parent(current)))return true;
   for(auto iface:s.nodes[current].interfaces){void* value=nullptr;if(!s.roots.Read(iface,&value))continue;auto found=std::find(roots.classes.begin(),roots.classes.end(),value);if(found!=roots.classes.end()&&self(self,size_t(found-roots.classes.begin())))return true;}
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
   bool child_targets=true;for(void* klass:transaction.old_classes){auto node=std::find(roots.classes.begin(),roots.classes.end(),klass);if(node==roots.classes.end()||!roots.loaders[size_t(node-roots.classes.begin())])child_targets=false;}
   std::vector<uint32_t*> slots,readonly;
   const auto add=[&](void* method)->bool {
    void* declaring=s.bindings.method_class(s.bindings.owner,method);auto native_owner=std::find(roots.classes.begin(),roots.classes.end(),declaring);
    if(native_owner!=roots.classes.end()){if(child_targets&&!roots.loaders[size_t(native_owner-roots.classes.begin())])return true;}
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
bool Verifier::VerifyBatch(const source_commit::Plan& transaction,const std::vector<native_schema_policy::GuardRequest>& requests,std::string* error)const {
 auto& s=*state_;if(requests.empty())return true;
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

bool Verifier::CollectManagedRoots(const prepared_roots::Registrar& registrar,std::string* error){
 auto& s=*state_;if(!s.sealed||s.frozen||!redefinition_gate::Owned(s.bindings.vm)||!s.Roots(&s.frozen_snapshot,error))return false;
 const bool split=s.bindings.metadata->StorageModel()==modern_field_metadata::Model::kSplit;
 const auto& layout=s.bindings.methods->model==method_identity::Model::kMethod11?method_metadata::kAndroid11:method_metadata::kAndroid12Onward;
 s.class_proofs.resize(s.nodes.size());
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
  for(void* method:s.frozen_snapshot.all_methods[n]){void* owner=s.bindings.method_class(s.bindings.owner,method);auto it=std::find(s.frozen_snapshot.classes.begin(),s.frozen_snapshot.classes.end(),owner);method_metadata::Identity identity;
   if(it==s.frozen_snapshot.classes.end()||!method_metadata::ReadIdentity(layout,method,layout.size,&identity))return false;
   s.member_proofs.push_back({method,size_t(it-s.frozen_snapshot.classes.begin()),jvmti_method_flags::Public(identity.access_flags),identity.dex_method_index,0,false});}
 }
 for(void* klass:s.expected.new_classes){State::InterfaceProof proof;proof.klass=klass;proof.table=s.bindings.metadata->Reference(klass,24);
  if(proof.table){if(!s.bindings.readable(s.bindings.owner,proof.table,12))return false;const uint32_t length=Read<uint32_t>(proof.table,8);
   if(length%2||!s.bindings.readable(s.bindings.owner,proof.table,12+size_t(length)*4))return false;
   for(uint32_t n=0;n<length/2;++n)proof.values.push_back(s.bindings.metadata->Reference(proof.table,12+size_t(n)*8));}
  s.replacement_interfaces.push_back(std::move(proof));}
 if(!rooted_schema_plan::CollectBindings(&s.expected,registrar,&s.expected_roots))return false;
 for(auto* values:{&s.frozen_snapshot.classes,&s.frozen_snapshot.loaders})for(auto& value:*values)if(!s.frozen_roots.Add(&value,registrar))return false;
 for(auto& proof:s.class_proofs){if(!s.frozen_roots.Add(&proof.dex_cache,registrar)||!s.frozen_roots.Add(&proof.interfaces,registrar))return false;
  for(auto& value:proof.interface_classes)if(!s.frozen_roots.Add(&value,registrar))return false;}
 for(auto& proof:s.replacement_interfaces){if(!s.frozen_roots.Add(&proof.klass,registrar)||!s.frozen_roots.Add(&proof.table,registrar))return false;for(auto& value:proof.values)if(!s.frozen_roots.Add(&value,registrar))return false;}
 for(auto& grant:s.grants)if(!s.frozen_roots.Add(&grant.loader,registrar))return false;
 s.frozen_roots.Seal();s.frozen=true;return s.FrozenProof();
}

bool Verifier::CanRefreshManagedRoots(const prepared_roots::Resolver& resolver)const noexcept{
 const auto& s=*state_;return s.frozen&&s.sealed&&redefinition_gate::Owned(s.bindings.vm)&&s.expected_roots.CanRefresh(resolver)&&s.frozen_roots.CanRefresh(resolver);
}
bool Verifier::RefreshManagedRoots(const source_commit::Plan& plan,const prepared_roots::Resolver& resolver) noexcept{
 auto& s=*state_;s.refreshed=false;if(!CanRefreshManagedRoots(resolver))return false;
 s.expected_roots.Refresh(resolver);s.frozen_roots.Refresh(resolver);
 if(!SamePlan(plan,s.expected)||!s.FrozenProof())return false;
 if((s.final_fields_proven||s.aliases_proven)&&(!s.bindings.final_fields||!s.bindings.final_fields->VerifyAliases(plan,nullptr)))return false;
 if(s.access_proven&&(!s.bindings.access_permissions||!s.bindings.access_permissions->Verify(plan,nullptr)))return false;
 s.refreshed=true;return true;
}

void Verifier::BeginCommit(const source_commit::Plan& transaction)const{
 const auto& s=*state_;if(!s.sealed||(s.frozen&&!s.refreshed)||!redefinition_gate::Owned(s.bindings.vm)||!SamePlan(transaction,s.expected))std::abort();
 if(s.access_proven){s.bindings.access_permissions->BeginCommit(transaction);for(auto slot:s.access_slots)__atomic_fetch_and(slot,~skip_access_checks,__ATOMIC_RELAXED);}
 if(s.final_fields_proven||s.aliases_proven)s.bindings.final_fields->BeginCommit(transaction);
}
}
