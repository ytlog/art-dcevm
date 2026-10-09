#include "platform/common/native_schema_guard_verifier.h"
#include "platform/common/loaded_member_guards.h"
#include "platform/common/method_array_view.h"
#include "platform/common/redefinition_gate.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <limits>
#include <cstdlib>
namespace artdcevm::native_schema_guard_verifier {
namespace {
bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
constexpr size_t absent=SIZE_MAX;
constexpr uint32_t skip_access_checks=0x00080000u,native_flag=0x100u;
struct Node {
 jclass root=nullptr;jobject loader=nullptr;
 evolution::ClassSchema schema;
 std::vector<jmethodID> method_ids;
 std::vector<jfieldID> field_ids;
 std::vector<jclass> interfaces;
 jclass parent=nullptr;
 bool affected=false;
};
struct Snapshot {
 std::vector<void*> classes,loaders;
 std::vector<size_t> parents;
 std::vector<std::vector<void*>> declared,all_methods;
};
bool SamePlan(const source_commit::Plan& a,const source_commit::Plan& b){
 if(a.mirror!=b.mirror||a.new_classes_root!=b.new_classes_root||a.abstract_instances!=b.abstract_instances||a.old_classes!=b.old_classes||a.new_classes!=b.new_classes||a.old_instances!=b.old_instances||a.new_instances!=b.new_instances||a.class_root_origins.size()!=b.class_root_origins.size())return false;
 for(size_t i=0;i<a.class_root_origins.size();++i)if(a.class_root_origins[i].array!=b.class_root_origins[i].array||a.class_root_origins[i].index!=b.class_root_origins[i].index)return false;
 return true;
}
bool Ordinary(jint status){return !(status&(JVMTI_CLASS_STATUS_ERROR|JVMTI_CLASS_STATUS_ARRAY|JVMTI_CLASS_STATUS_PRIMITIVE));}
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
bool PublicMembers(JNIEnv* env,jvmtiEnv* ti,jclass klass,Node* out,std::string* error){
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
  out->field_ids.push_back(fields[i]);out->schema.fields.push_back({text.name,text.signature,native_schema_policy::JavaFlags(uint32_t(flags)),UINT32_MAX});
 }
 if(env->ExceptionCheck())return Fail(error,"Public schema stage left a pending exception");
 if(std::set<jmethodID>(out->method_ids.begin(),out->method_ids.end()).size()!=out->method_ids.size()||std::set<jfieldID>(out->field_ids.begin(),out->field_ids.end()).size()!=out->field_ids.size())return Fail(error,"Duplicate public member IDs");
 return true;
}
bool SamePublic(const Node& a,const Node& b){return a.schema.descriptor==b.schema.descriptor&&a.schema.access==b.schema.access&&MembersEqual(a.schema.fields,b.schema.fields)&&MembersEqual(a.schema.methods,b.schema.methods)&&a.method_ids==b.method_ids&&a.field_ids==b.field_ids;}
}
struct Verifier::State {
 Bindings bindings{};std::vector<Node> nodes;std::vector<jclass> inputs;
 bool staged=false,verified=false,collected=false,sealed=false,interfaces_proven=false;
 source_commit::Plan expected{};
 mutable std::vector<uint32_t*> access_slots;
 mutable bool access_proven=false,final_fields_proven=false,aliases_proven=false;
 bool Roots(Snapshot* out,std::string* error)const{
  const auto Mapped=[&](const void* address,size_t bytes){return address&&bindings.readable&&bindings.readable(bindings.owner,address,bytes);};
  if(!redefinition_gate::Owned(bindings.vm)||!bindings.metadata||!bindings.metadata->Calibrated()||!bindings.methods||bindings.methods->identities.size()<3||bindings.methods->readable_extents.size()!=bindings.methods->identities.size()||!bindings.decode_global||!bindings.decode_method||!bindings.decode_field||!bindings.method_class||!bindings.same_method||!bindings.readable||!bindings.writable||!bindings.metadata||!bindings.methods)return Fail(error,"Schema verifier lacks the calibrated native process owner");
  Snapshot result;result.classes.resize(nodes.size());result.loaders.resize(nodes.size());result.parents.resize(nodes.size(),absent);result.declared.resize(nodes.size());result.all_methods.resize(nodes.size());
  for(size_t n=0;n<nodes.size();++n){
   if(!bindings.decode_global(bindings.owner,nodes[n].root,&result.classes[n])||!result.classes[n])return Fail(error,"Staged Class strong root could not be decoded");
   if(nodes[n].loader&&!bindings.decode_global(bindings.owner,nodes[n].loader,&result.loaders[n]))return Fail(error,"Staged ClassLoader strong root could not be decoded");

  }
  if(std::set<void*>(result.classes.begin(),result.classes.end()).size()!=nodes.size())return Fail(error,"Duplicate staged native Class identity");
  const auto& layout=bindings.methods->model==method_identity::Model::kMethod11?method_metadata::kAndroid11:method_metadata::kAndroid12Onward;
  for(size_t n=0;n<nodes.size();++n){const auto& node=nodes[n];void* klass=result.classes[n];
   if(node.parent){void* parent=nullptr;if(!bindings.decode_global(bindings.owner,node.parent,&parent))return false;auto it=std::find(result.classes.begin(),result.classes.end(),parent);if(it==result.classes.end())return Fail(error,"Staged ancestor is outside complete loaded roots");result.parents[n]=size_t(it-result.classes.begin());}
   field_copy::CopyClassView identity;
   if(!bindings.metadata->ClassIdentity(klass,&identity,error)||identity.loader!=result.loaders[n]||!identity.descriptor||node.schema.descriptor!=identity.descriptor)return Fail(error,"Public/native Class descriptor or loader changed");
   void* expected_parent=result.parents[n]==absent?nullptr:result.classes[result.parents[n]];
   if(identity.parent!=expected_parent)return Fail(error,"Native superclass differs from public rooted identity");
   for(size_t i=0;i<node.field_ids.size();++i){
    void* field=bindings.decode_field(bindings.owner,node.root,node.field_ids[i]);field_copy::CopyFieldView identity;
    if(!Mapped(field,sizeof(field_array::Field))||bindings.metadata->FieldDeclaringClass(field)!=klass||!bindings.metadata->FieldIdentity(field,&identity,error)||!identity.name||!identity.descriptor||node.schema.fields[i].name!=identity.name||node.schema.fields[i].descriptor!=identity.descriptor||native_schema_policy::JavaFlags(field_array::Flags(field))!=node.schema.fields[i].access)return Fail(error,"Contextual public/native field identity or flags changed after staging");
   }
   for(size_t i=0;i<node.method_ids.size();++i){void* method=bindings.decode_method(bindings.owner,node.method_ids[i]);
    if(!Mapped(method,layout.size)||bindings.method_class(bindings.owner,method)!=klass||native_schema_policy::JavaFlags(method_metadata::ReadAccessFlags(method))!=native_schema_policy::JavaFlags(node.schema.methods[i].access))return Fail(error,"Public/native method owner or flags changed after staging");
    result.declared[n].push_back(method);
   }
  }
  bool distinct_signature=false,shared_signature=false;
  std::map<std::pair<std::string,std::string>,void*> signatures;
  for(size_t n=0;n<nodes.size();++n)for(size_t i=0;i<result.declared[n].size();++i){void* method=result.declared[n][i];const auto& member=nodes[n].schema.methods[i];
   if(!bindings.same_method(bindings.owner,method,method))return Fail(error,"Named signature comparator rejected a public self identity");
   auto key=std::make_pair(member.name,member.descriptor);auto previous=signatures.find(key);
   if(previous!=signatures.end()&&previous->second!=method){if(!bindings.same_method(bindings.owner,method,previous->second))return Fail(error,"Named signature comparator differs from independent matching public declarations");shared_signature=true;}
   if(!signatures.empty()&&signatures.begin()->first!=key){if(bindings.same_method(bindings.owner,method,signatures.begin()->second))return Fail(error,"Named signature comparator accepted independently different public declarations");distinct_signature=true;}
   signatures.emplace(std::move(key),method);
  }
  if(!distinct_signature||!shared_signature)return Fail(error,"Whole loaded graph lacks positive and negative signature comparator oracles");
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
    bool match=false;for(void* declared:result.declared[size_t(cls-result.classes.begin())])if(bindings.same_method(bindings.owner,method,declared)){match=true;break;}
    if(!match)return Fail(error,"Copied native method has no independent public declaration");
   }
   if(found.size()!=result.declared[n].size())return Fail(error,"Public method ID is outside its source-declared native arena");
  }
  *out=std::move(result);return true;
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
    for(auto iface:nodes[index].interfaces){void* native=nullptr;if(!bindings.decode_global(bindings.owner,iface,&native))return false;auto it=std::find(roots.classes.begin(),roots.classes.end(),native);if(it==roots.classes.end())return Fail(error,"Interface identity is outside staged Class roots");
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
Verifier::~Verifier(){if(!state_->nodes.empty()||!state_->inputs.empty())std::abort();}
void Verifier::Clear(JNIEnv* env){if(!env&&(!state_->nodes.empty()||!state_->inputs.empty()))std::abort();for(auto& n:state_->nodes){for(auto ref:n.interfaces)env->DeleteGlobalRef(ref);if(n.parent)env->DeleteGlobalRef(n.parent);if(n.loader)env->DeleteGlobalRef(n.loader);if(n.root)env->DeleteGlobalRef(n.root);}for(auto ref:state_->inputs)env->DeleteGlobalRef(ref);*state_=State{};}
bool Verifier::Stage(JNIEnv* env,jvmtiEnv* ti,const std::vector<jclass>& inputs,const Bindings& bindings,std::string* error){
 if(!env||!ti||inputs.empty()||!redefinition_gate::Owned(bindings.vm)||env->ExceptionCheck()||!state_->nodes.empty())return Fail(error,"Invalid or repeated schema graph stage");state_->bindings=bindings;
 for(auto klass:inputs){auto global=static_cast<jclass>(env->NewGlobalRef(klass));if(!global||env->ExceptionCheck())return false;state_->inputs.push_back(global);}
 jint count=0;jclass* classes=nullptr;if(ti->GetLoadedClasses(&count,&classes)!=JVMTI_ERROR_NONE||count<0)return Fail(error,"Complete loaded caller graph unavailable");
 struct Classes {JNIEnv* env;jvmtiEnv* ti;jint count;jclass* values;~Classes(){for(jint i=0;i<count;++i)env->DeleteLocalRef(values[i]);if(values)ti->Deallocate(reinterpret_cast<unsigned char*>(values));}} release{env,ti,count,classes};
 for(jint i=0;i<count;++i){jint status=0;if(ti->GetClassStatus(classes[i],&status)!=JVMTI_ERROR_NONE)return false;if(!Ordinary(status))continue;
  if(!(status&JVMTI_CLASS_STATUS_PREPARED))return Fail(error,"Unprepared loaded Class prevents complete caller provenance");
  Node node;if(!PublicMembers(env,ti,classes[i],&node,error))return false;node.root=static_cast<jclass>(env->NewGlobalRef(classes[i]));if(!node.root)return false;
  state_->nodes.push_back(std::move(node));auto& stored=state_->nodes.back();
  jobject loader=nullptr;if(ti->GetClassLoader(classes[i],&loader)!=JVMTI_ERROR_NONE)return false;if(loader){stored.loader=env->NewGlobalRef(loader);env->DeleteLocalRef(loader);if(!stored.loader)return false;}
  auto parent=env->GetSuperclass(classes[i]);if(parent){stored.parent=static_cast<jclass>(env->NewGlobalRef(parent));env->DeleteLocalRef(parent);if(!stored.parent)return false;}
  jint interfaces_count=0;jclass* interfaces=nullptr;if(ti->GetImplementedInterfaces(classes[i],&interfaces_count,&interfaces)!=JVMTI_ERROR_NONE||interfaces_count<0)return false;
  for(jint j=0;j<interfaces_count;++j){auto global=static_cast<jclass>(env->NewGlobalRef(interfaces[j]));env->DeleteLocalRef(interfaces[j]);if(!global){if(interfaces)ti->Deallocate(reinterpret_cast<unsigned char*>(interfaces));return false;}stored.interfaces.push_back(global);}
  if(interfaces)ti->Deallocate(reinterpret_cast<unsigned char*>(interfaces));
  for(auto input:state_->inputs)if(env->IsAssignableFrom(classes[i],input))stored.affected=true;
  if(env->ExceptionCheck())return false;
 }
 if(state_->nodes.empty())return Fail(error,"Empty public Class graph");state_->staged=true;return true;
}
bool Verifier::CheckWindow(JNIEnv* env,jvmtiEnv* ti,const std::vector<jclass>& actual,Window window,std::string* error){
 auto& s=*state_;if(!s.staged||s.collected||(window==Window::Verified&&s.verified)||!redefinition_gate::Owned(s.bindings.vm)||!env||!ti||env->ExceptionCheck()||(window==Window::Collected&&!s.verified))return Fail(error,"Schema window order or context is invalid");
 size_t affected=0;for(const auto& node:s.nodes)if(node.affected){++affected;size_t matches=0;for(auto klass:actual)matches+=env->IsSameObject(klass,node.root);if(matches!=1)return Fail(error,"Actual Holder graph differs from public staged identities");}
 if(affected!=actual.size())return Fail(error,"Duplicate or unstaged Holder Class");
 jint count=0;jclass* loaded=nullptr;if(ti->GetLoadedClasses(&count,&loaded)!=JVMTI_ERROR_NONE||count<0)return false;
 struct Loaded {JNIEnv* env;jvmtiEnv* ti;jint count;jclass* values;~Loaded(){for(jint i=0;i<count;++i)env->DeleteLocalRef(values[i]);if(values)ti->Deallocate(reinterpret_cast<unsigned char*>(values));}} release{env,ti,count,loaded};
 size_t live=0;for(jint i=0;i<count;++i){jint status=0;if(ti->GetClassStatus(loaded[i],&status)!=JVMTI_ERROR_NONE)return false;if(!Ordinary(status))continue;
  if(!(status&JVMTI_CLASS_STATUS_PREPARED))return Fail(error,"Unprepared Class appeared before native schema sealing");
  auto node=std::find_if(s.nodes.begin(),s.nodes.end(),[&](const auto& n){return env->IsSameObject(n.root,loaded[i]);});
  if(node==s.nodes.end())return Fail(error,"Class was defined after caller staging; retry complete schema operation");
  Node current;if(!PublicMembers(env,ti,loaded[i],&current,error)||!SamePublic(*node,current))return Fail(error,"Public member flags/identities changed inside the source window");
  jobject loader=nullptr;if(ti->GetClassLoader(loaded[i],&loader)!=JVMTI_ERROR_NONE)return Fail(error,"Paused ClassLoader identity unavailable");const bool same_loader=env->IsSameObject(loader,node->loader);if(loader)env->DeleteLocalRef(loader);if(!same_loader)return Fail(error,"Public ClassLoader identity changed inside source window");
  auto parent=env->GetSuperclass(loaded[i]);const bool same_parent=env->IsSameObject(parent,node->parent);if(parent)env->DeleteLocalRef(parent);if(!same_parent)return Fail(error,"Public superclass identity changed inside the source window");
  bool affects=false;for(auto input:s.inputs)affects|=env->IsAssignableFrom(loaded[i],input);if(affects!=node->affected)return Fail(error,"Loaded Class assignability changed after staging");++live;
 }
 if(live!=s.nodes.size()||env->ExceptionCheck())return Fail(error,"Loaded caller graph lost staged Class identity");
 if(window==Window::Verified)s.verified=true;else s.collected=true;return true;
}
bool Verifier::Seal(const source_commit::Plan& transaction,std::string* error){
 auto& s=*state_;if(!s.collected||!redefinition_gate::Owned(s.bindings.vm)||transaction.old_classes.empty()||transaction.old_classes.size()!=transaction.new_classes.size())return Fail(error,"Schema graph cannot seal before its real collected window");
 s.sealed=false;s.access_proven=false;s.final_fields_proven=false;s.aliases_proven=false;s.access_slots.clear();
 Snapshot roots;if(!s.Roots(&roots,error))return false;
 const auto Mapped=[&](const void* address,size_t bytes){return address&&s.bindings.readable(s.bindings.owner,address,bytes);};
 size_t count=0;for(size_t n=0;n<s.nodes.size();++n)if(s.nodes[n].affected){++count;if(std::count(transaction.old_classes.begin(),transaction.old_classes.end(),roots.classes[n])!=1)return Fail(error,"Native Holder Class is outside public affected graph");}
 if(count!=transaction.old_classes.size())return Fail(error,"Native Holder repeats or omits a staged Class");
 // Calibrate the common IfTable source prefix from a real positive and empty
 // public transitive graph, never from a successful zero-interface read alone.
 bool positive=false,negative=false;
 for(size_t n=0;n<s.nodes.size();++n){if(s.nodes[n].schema.descriptor.empty()||s.nodes[n].schema.descriptor.front()!='L')continue;
  std::set<void*> expected;std::set<size_t> traversed;
  const auto walk=[&](const auto& self,size_t k)->bool{if(!traversed.insert(k).second)return true;if(roots.parents[k]!=absent&&!self(self,roots.parents[k]))return false;for(auto iface:s.nodes[k].interfaces){void* native=nullptr;if(!s.bindings.decode_global(s.bindings.owner,iface,&native))return false;auto it=std::find(roots.classes.begin(),roots.classes.end(),native);if(it==roots.classes.end())return false;expected.insert(native);if(!self(self,size_t(it-roots.classes.begin())))return false;}return true;};
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
 Snapshot roots;if(!s.Roots(&roots,error))return false;auto it=std::find(roots.classes.begin(),roots.classes.end(),transaction.old_classes[pair]);if(it==roots.classes.end())return false;const size_t target=size_t(it-roots.classes.begin());
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
   for(auto iface:s.nodes[current].interfaces){void* value=nullptr;if(!s.bindings.decode_global(s.bindings.owner,iface,&value))continue;auto found=std::find(roots.classes.begin(),roots.classes.end(),value);if(found!=roots.classes.end()&&self(self,size_t(found-roots.classes.begin())))return true;}
   return false;
  };return walk(walk,node);
 };
 const auto parent_token=[&](size_t token){if(!token)return size_t{0};const auto value=parent(token-1);return value==absent?size_t{0}:value+1;};
 using Kind=native_schema_policy::GuardKind;
 switch(requirement.kind){
  case Kind::HierarchyGraph:
   return Fail(error,"Hierarchy changes require the calibrated rooted graph and source allocator owner");
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
  case Kind::AccessPermissions:{
   if(!s.bindings.access_permissions||!s.bindings.access_permissions->Verify(transaction,error))return Fail(error,"Access permission changes require the proven process interpreter/deopt owner");
   std::vector<uint32_t*> slots;for(const auto& methods:roots.all_methods)for(void* method:methods){uint32_t flags=method_metadata::ReadAccessFlags(method);if(!(flags&native_flag)&&(flags&skip_access_checks))slots.push_back(reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(method)+4));}
   // Include newly allocated tables: their verifier may have enabled shortcuts
   // using the old public flags. Complete native schema validation supplies the
   // exact source arena even when added members have no old public JNI ID.
   for(void* klass:transaction.new_classes){evolution::ClassSchema schema;native_method_table::View table;if(!native_schema_policy::ReadNativeDeclarations(*s.bindings.metadata,*s.bindings.methods,klass,&schema,error,&table))return false;for(uint32_t i=0;i<table.methods.count;++i){void* method=table.methods.At(i);const uint32_t flags=method_metadata::ReadAccessFlags(method);if(!(flags&native_flag)&&(flags&skip_access_checks))slots.push_back(reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(method)+4));}}
   std::sort(slots.begin(),slots.end());slots.erase(std::unique(slots.begin(),slots.end()),slots.end());
   for(auto slot:slots)if(reinterpret_cast<uintptr_t>(slot)%alignof(uint32_t)||!s.bindings.writable(s.bindings.owner,slot,sizeof(uint32_t)))return Fail(error,"A loaded caller access flag cannot be atomically updated in its rooted arena");
   s.access_slots=std::move(slots);s.access_proven=true;return true;
  }
 }
 return Fail(error,"Unknown native schema guard requirement");
}
void Verifier::BeginCommit(const source_commit::Plan& transaction)const{
 const auto& s=*state_;if(!s.sealed||!redefinition_gate::Owned(s.bindings.vm)||!SamePlan(transaction,s.expected))std::abort();
 if(s.access_proven){s.bindings.access_permissions->BeginCommit(transaction);for(auto slot:s.access_slots)__atomic_fetch_and(slot,~skip_access_checks,__ATOMIC_RELAXED);}
 if(s.final_fields_proven||s.aliases_proven)s.bindings.final_fields->BeginCommit(transaction);
}
}
