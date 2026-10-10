#include "platform/common/native_hierarchy_typed_storage.h"
#include "platform/common/runnable_executor.h"
#include "platform/abi/arm64/art_field.h"
#include <algorithm>
#include "platform/common/typed_storage_assignability.h"
#include <cstring>
#include <unordered_map>
namespace artdcevm::native_hierarchy_typed_storage {
namespace {bool Fail(std::string* e,const char* s){if(e)*e=s;return false;}}
struct Proof::State final:native_typed_heap_walk::Sink {
 const native_typed_heap_walk::Service& walk;const source_handle_roots::Service& source;
 const modern_field_metadata::FieldMetadata& metadata;runnable_jni::Context context{};
 using Slot=bounded_managed_roots::Slot;
 struct Field {void* identity=nullptr;void* declared_type=nullptr;Slot type_root{};uint32_t offset=0;bool statik=false;};
 struct Type {void* klass=nullptr;void* component=nullptr;Slot root{},component_root{};bool array=false,primitive=false,object=false,cloneable=false,serializable=false;std::vector<Field> fields;};
 std::vector<Type> types;bounded_managed_roots::Store public_roots;prepared_roots::Bindings roots;
 const source_commit::Plan* plan=nullptr;source_commit::Plan expected;
 using Assign=bool(*)(void*,void*);using FieldLeaf=void*(*)(void*);Assign assign=nullptr;FieldLeaf field_leaf=nullptr;
 std::vector<std::pair<uintptr_t,size_t>> type_index,class_index,instance_index;
 mutable const modern_field_metadata::ReadContext* maps=nullptr;
 mutable const native_source_commit::Services* copies=nullptr;
 mutable Refusal refusal=Refusal::None;
 bool staged=false,captured=false,refreshed=false;
 State(const native_typed_heap_walk::Service&w,const source_handle_roots::Service&s,const modern_field_metadata::FieldMetadata&m):walk(w),source(s),metadata(m),public_roots({this,Scope,Oracle,Decode,Element}){}
 static bool Scope(void* raw,JNIEnv* env,jobject,runnable_jni::Operation op,void* data){auto&s=*static_cast<State*>(raw);return s.context.env==env&&s.source.ReadyForSourceAccess()&&op(s.context,data);}
 static bool Oracle(void*,const runnable_jni::Context& c,jobject root,void** out){return runnable_executor::DecodeStrongGlobal(c,root,out);}
 static bool Decode(void* raw,jobject root,void** out){auto&s=*static_cast<State*>(raw);return s.source.BorrowOwnedStrongGlobal(s.context,root,out);}
 static void* Element(void* raw,void* array,size_t index){return static_cast<State*>(raw)->metadata.Reference(array,12+index*4);}
 bool Readable(const void* p,size_t n)const noexcept{size_t extent=0;return maps&&maps->owner&&maps->extent&&maps->extent(maps->owner,p,&extent)&&extent>=n;}
 const Type* Find(void* klass)const noexcept{
  auto at=std::lower_bound(type_index.begin(),type_index.end(),std::pair<uintptr_t,size_t>{uintptr_t(klass),0});
  return at!=type_index.end()&&at->first==uintptr_t(klass)?&types[at->second]:nullptr;
 }
 static size_t Index(const std::vector<std::pair<uintptr_t,size_t>>& index,void* value)noexcept{
  auto at=std::lower_bound(index.begin(),index.end(),std::pair<uintptr_t,size_t>{uintptr_t(value),0});
  return at!=index.end()&&at->first==uintptr_t(value)?at->second:SIZE_MAX;
 }
 void* MappedClass(void* old)const noexcept{const auto i=Index(class_index,old);return i==SIZE_MAX?old:expected.new_classes[i];}
 void* Component(void* klass)const noexcept{
  if(auto*t=Find(klass))return t->component;
  return Readable(klass,offsetof(abi::arm64::android11::Class11,component_type)+4)?metadata.Reference(klass,offsetof(abi::arm64::android11::Class11,component_type)):nullptr;
 }
 bool ArraySupertype(void* klass)const noexcept{auto*t=Find(klass);return t&&(t->object||t->cloneable||t->serializable);}
 bool Primitive(void* klass)const noexcept{auto*t=Find(klass);return t&&t->primitive;}
 bool AssignNamed(void* declared,void* actual)const noexcept{return Readable(declared,8)&&Readable(actual,8)&&assign(declared,actual);}
 bool ChangedArray(void* klass)const noexcept{return ArrayRelationChanged(*this,klass);}
 bool Assignable(void* declared,void* actual)const noexcept{return AssignableAfter(*this,declared,actual);}
 bool Reference(void* object,void* value,uint32_t offset)noexcept override{
  if(!object||!value)return true; // Java/root-frame legality belongs to its separate actual owner.
  if(!Readable(value,8)||!Readable(object,8)){refusal=Refusal::Inventory;return false;}
  void* after_class=nullptr;bool affected=false;
  const auto i=Index(instance_index,value);if(i!=SIZE_MAX){
   if(!Readable(expected.new_instances[i],8)){refusal=Refusal::Inventory;return false;}
   after_class=metadata.Reference(expected.new_instances[i],0);affected=true;
  }
  if(!affected){after_class=metadata.Reference(value,0);affected=after_class&&ChangedArray(after_class);}
  if(!affected)return true;
  void* owner_class=metadata.Reference(object,0);const auto* type=Find(owner_class);
  // An object-array value changes virtual assignability when its component
  // narrows, even though the array object itself is not in old_instances.
  void* owner_component=Component(owner_class);
  if(owner_component){
   if(offset==0)return true;
   if(!Readable(object,12)||offset<12||(offset-12)%4){refusal=Refusal::UnknownCell;return false;}
   int32_t length=0;std::memcpy(&length,static_cast<char*>(object)+8,4);
   if(length<0||size_t((offset-12)/4)>=size_t(length)||!owner_component){refusal=Refusal::UnknownCell;return false;}
   const bool pass=Assignable(owner_component,after_class);if(!pass)refusal=Refusal::IncompatibleArray;return pass;
  }
  const auto check_fields=[&](const Type* declared_owner,bool statik,bool*found)noexcept{
   for(size_t depth=0;declared_owner&&depth<65536;++depth){
    for(const auto& field:declared_owner->fields)if(field.statik==statik&&field.offset==offset){
     *found=true;
     // A compatible declaration is safe whether the source copies or retires
     // this cell. The exact retained-copy table is necessary only to prove that
     // an otherwise incompatible old-owner cell is genuinely cleared.
     if(Assignable(field.declared_type,after_class))return true;
     void* source_class=nullptr;
     if(statik){if(Index(class_index,object)!=SIZE_MAX)source_class=object;}
     else if(Index(instance_index,object)!=SIZE_MAX)source_class=owner_class;
     if(source_class){bool retained=false;if(!copies->RetainsSourceField(*plan,statik,source_class,field.identity,&retained)){refusal=Refusal::SourceCopy;return false;}if(!retained)return true;}
     refusal=Refusal::IncompatibleField;return false;
    }
    if(statik)break;
    if(!Readable(declared_owner->klass,offsetof(abi::arm64::android11::Class11,super_class)+4))break;
    auto* parent=metadata.Reference(declared_owner->klass,offsetof(abi::arm64::android11::Class11,super_class));declared_owner=Find(parent);
   }
   return true;
  };
  // A Class object contains both the represented class's static fields and
  // java.lang.Class instance fields. Source copying clears only the former;
  // Class metadata cells must retain their own declared assignment relation.
  bool found=false;
  if(const auto* represented=Find(object)){const bool pass=check_fields(represented,true,&found);if(found)return pass;}
  const bool pass=check_fields(type,false,&found);if(found)return pass;
  refusal=Refusal::UnknownCell;return false;
 }
};
Proof::Proof(const native_typed_heap_walk::Service&w,const source_handle_roots::Service&s,const modern_field_metadata::FieldMetadata&m):state_(new State(w,s,m)){}
Proof::~Proof()=default;
void Proof::Clear(JNIEnv* env){auto&s=*state_;s.public_roots.Clear(env);s.types.clear();s.type_index.clear();s.class_index.clear();s.instance_index.clear();s.roots={};s.expected={};s.plan=nullptr;s.captured=s.staged=s.refreshed=false;}
bool Proof::Staged()const noexcept{return state_->staged;}
Proof::Refusal Proof::LastRefusal()const noexcept{return state_->refusal;}
bool Proof::Stage(HookEngine& hooks,const runnable_jni::Context& c,jvmtiEnv* ti,std::string* error){
 auto&s=*state_;if(s.staged||!c.env||!c.self||!ti||!s.walk.Calibrated()||!s.metadata.Calibrated()||!s.source.ReadyForSourceAccess())return Fail(error,"Typed storage needs complete independently calibrated source owners");
 s.context=c;s.assign=reinterpret_cast<State::Assign>(hooks.FindSymbol("libart.so","_ZN3art6mirror5Class16IsAssignableFromENS_6ObjPtrIS1_EE"));
 s.field_leaf=reinterpret_cast<State::FieldLeaf>(hooks.FindSymbol("libart.so","_ZN3art6mirror5Field11GetArtFieldEv"));
 if(!s.assign||!s.field_leaf)return Fail(error,"Named typed Class/Field declarations unavailable");
 auto* env=c.env;jclass class_type=env->FindClass("java/lang/Class"),field_type=env->FindClass("java/lang/reflect/Field");
 auto component=class_type?env->GetMethodID(class_type,"getComponentType","()Ljava/lang/Class;"):nullptr;
 auto primitive=class_type?env->GetMethodID(class_type,"isPrimitive","()Z"):nullptr;
 auto declared=field_type?env->GetMethodID(field_type,"getType","()Ljava/lang/Class;"):nullptr;
 jclass system=env->FindClass("java/lang/System");auto hash=system?env->GetStaticMethodID(system,"identityHashCode","(Ljava/lang/Object;)I"):nullptr;
 if(!component||!primitive||!declared||!hash||env->ExceptionCheck())return Fail(error,"Public typed declaration oracle unavailable");
 bool pass=true;std::unordered_multimap<jint,size_t> identities;
 // Resolving public Field.getType may load new Classes. Enumerate until a
 // complete pass adds none; native identities are borrowed only after this
 // fixed point, and the graph owner must snapshot loaded Classes afterwards.
 for(;;){jint count=0;jclass* loaded=nullptr;
  if(ti->GetLoadedClasses(&count,&loaded)!=JVMTI_ERROR_NONE||count<=0||!loaded){pass=false;break;}
  size_t added=0;
  for(jint i=0;i<count&&pass;++i){
   const jint identity=env->CallStaticIntMethod(system,hash,loaded[i]);bool seen=false;
   const auto range=identities.equal_range(identity);
   for(auto at=range.first;at!=range.second;++at)if(s.public_roots.Same(env,s.types[at->second].root,loaded[i])){seen=true;break;}
   if(env->ExceptionCheck()){pass=false;break;}if(seen)continue;
   State::Type type;char* signature=nullptr;pass=ti->GetClassSignature(loaded[i],&signature,nullptr)==JVMTI_ERROR_NONE&&signature;
  if(!pass)break;const std::string name(signature);ti->Deallocate(reinterpret_cast<unsigned char*>(signature));
  // JNI Class.getClassLoader may return a BootClassLoader facade. Only the
  // independent JVMTI defining-loader identity supplies the boot oracle.
  jobject cl=nullptr;const auto loader_error=ti->GetClassLoader(loaded[i],&cl);
  const bool boot=loader_error==JVMTI_ERROR_NONE&&!cl;if(cl)env->DeleteLocalRef(cl);
  if(loader_error!=JVMTI_ERROR_NONE||env->ExceptionCheck()){pass=false;break;}
  type.object=boot&&name=="Ljava/lang/Object;";type.cloneable=boot&&name=="Ljava/lang/Cloneable;";type.serializable=boot&&name=="Ljava/io/Serializable;";
  type.primitive=env->CallBooleanMethod(loaded[i],primitive);type.array=!name.empty()&&name.front()=='[';
  pass=!env->ExceptionCheck()&&s.public_roots.Stage(c,loaded[i],&type.root,error);
  jobject part=pass?env->CallObjectMethod(loaded[i],component):nullptr;
  if(pass&&part)pass=s.public_roots.Stage(c,part,&type.component_root,error);if(part)env->DeleteLocalRef(part);
  if(pass&&!type.array&&!type.primitive){jint fields_count=0;jfieldID* fields=nullptr;
   const auto fields_error=ti->GetClassFields(loaded[i],&fields_count,&fields);
   // JVMTI does not expose declarations of an unprepared class. Keep its
   // identity in the inventory: an affected actual cell without an independently
   // resolved field remains UnknownCell, while unrelated/no-reference classes
   // do not prevent a legal narrowing.
   pass=(fields_error==JVMTI_ERROR_NONE||fields_error==JVMTI_ERROR_CLASS_NOT_PREPARED)&&fields_count>=0;
   if(fields_error==JVMTI_ERROR_CLASS_NOT_PREPARED)fields_count=0;
   for(jint j=0;j<fields_count&&pass;++j){char* name_bytes=nullptr;char* descriptor=nullptr;jint modifiers=0;
    pass=ti->GetFieldName(loaded[i],fields[j],&name_bytes,&descriptor,nullptr)==JVMTI_ERROR_NONE&&name_bytes&&descriptor&&ti->GetFieldModifiers(loaded[i],fields[j],&modifiers)==JVMTI_ERROR_NONE;
    if(pass&&(descriptor[0]=='L'||descriptor[0]=='[')){
     jobject reflected=env->ToReflectedField(loaded[i],fields[j],(modifiers&8)!=0);jobject declared_type=reflected?env->CallObjectMethod(reflected,declared):nullptr;
     State::Field field;field.statik=(modifiers&8)!=0;State::Slot mirror;
     pass=reflected&&declared_type&&!env->ExceptionCheck()&&s.public_roots.Stage(c,declared_type,&field.type_root,error)&&s.public_roots.Stage(c,reflected,&mirror,error);
     void* native_mirror=nullptr;if(pass)pass=s.public_roots.Read(mirror,&native_mirror);
     field.identity=pass?s.field_leaf(native_mirror):nullptr;field_copy::CopyFieldView view;
     if(pass)pass=field.identity&&s.metadata.FieldIdentity(field.identity,&view,error)&&view.name&&view.descriptor&&!std::strcmp(view.name,name_bytes)&&!std::strcmp(view.descriptor,descriptor)&&
        (static_cast<abi::arm64::Field*>(field.identity)->access_flags&0xffffu)==(uint32_t(modifiers)&0xffffu);
     if(pass){field.offset=static_cast<abi::arm64::Field*>(field.identity)->offset;type.fields.push_back(field);}
     if(reflected)env->DeleteLocalRef(reflected);if(declared_type)env->DeleteLocalRef(declared_type);
    }
    if(name_bytes)ti->Deallocate(reinterpret_cast<unsigned char*>(name_bytes));if(descriptor)ti->Deallocate(reinterpret_cast<unsigned char*>(descriptor));
   }if(fields)ti->Deallocate(reinterpret_cast<unsigned char*>(fields));
  }
  if(pass){identities.emplace(identity,s.types.size());s.types.push_back(std::move(type));++added;}
  }
  for(jint i=0;i<count;++i)env->DeleteLocalRef(loaded[i]);ti->Deallocate(reinterpret_cast<unsigned char*>(loaded));
  if(!pass||!added)break;
 }
 env->DeleteLocalRef(system);env->DeleteLocalRef(class_type);env->DeleteLocalRef(field_type);
 if(!pass||env->ExceptionCheck())return Fail(error,"Complete public/native typed field inventory refused");
 // The fixed named ObjPtr<Class> leaf needs an independent public positive
 // and negative class relation, including its real receiver/argument order.
 size_t object_index=SIZE_MAX,sample_index=SIZE_MAX;
 for(size_t i=0;i<s.types.size();++i){if(s.types[i].object)object_index=i;else if(sample_index==SIZE_MAX&&!s.types[i].array&&!s.types[i].primitive)sample_index=i;}
 if(object_index==SIZE_MAX||sample_index==SIZE_MAX)return Fail(error,"Typed storage lacks independent ordinary Class relation oracles");
 jobject object_ref=s.public_roots.LocalRef(env,s.types[object_index].root),sample_ref=s.public_roots.LocalRef(env,s.types[sample_index].root);
 const bool public_forward=object_ref&&sample_ref&&env->IsAssignableFrom(static_cast<jclass>(sample_ref),static_cast<jclass>(object_ref));
 const bool public_reverse=object_ref&&sample_ref&&env->IsAssignableFrom(static_cast<jclass>(object_ref),static_cast<jclass>(sample_ref));
 if(object_ref)env->DeleteLocalRef(object_ref);if(sample_ref)env->DeleteLocalRef(sample_ref);
 if(!public_forward||public_reverse||env->ExceptionCheck())return Fail(error,"Public ordinary Class relation oracle is ambiguous");
 // Every JNI allocation and type resolution has finished. Borrow all native
 // identities in the same no-JNI interval, checking calibrated field membership.
 for(auto& type:s.types){if(!s.public_roots.Read(type.root,&type.klass)||!type.klass)return false;
  if(type.component_root&&!s.public_roots.Read(type.component_root,&type.component))return false;
  if(s.metadata.Reference(type.klass,offsetof(abi::arm64::android11::Class11,component_type))!=type.component)return Fail(error,"Native array component differs from public Class oracle");
  for(auto& field:type.fields){if(!s.public_roots.Read(field.type_root,&field.declared_type)||!field.declared_type||s.metadata.FieldDeclaringClass(field.identity)!=type.klass)return Fail(error,"Native field declaration owner/type differs");
   field_array::View array;if(!s.metadata.Fields(type.klass,field.statik,&array,error))return false;bool found=false;for(uint32_t i=0;i<array.selected_count;++i)found|=array.AtSelected(i)==field.identity;if(!found)return Fail(error,"Reflected typed field is outside complete native declaration array");
  }
 }
 if(!s.assign(s.types[object_index].klass,s.types[sample_index].klass)||s.assign(s.types[sample_index].klass,s.types[object_index].klass))return Fail(error,"Named Class assignment leaf differs from independent JNI relation");
 s.type_index.resize(s.types.size());s.staged=true;return true;
}
bool Proof::Capture(const source_commit::Plan& plan,const prepared_roots::Registrar& registrar,std::string* error){auto&s=*state_;
 if(!s.staged||s.captured||plan.old_classes.size()!=plan.new_classes.size()||plan.old_instances.size()!=plan.new_instances.size())return Fail(error,"Typed storage capture lacks exact complete source Plan");
 // Other ordinary owners may perform JNI/type resolution after Stage. Reload
 // every independently rooted public identity in this actual original source
 // Runnable/GC-stable collection interval immediately before registration.
 if(!s.source.ReadyForSourceAccess())return Fail(error,"Typed storage capture is outside its original source Runnable root interval");
 for(auto& type:s.types){if(!s.public_roots.Read(type.root,&type.klass)||!type.klass||
    (type.component_root&&!s.public_roots.Read(type.component_root,&type.component)))return false;
  if(s.metadata.Reference(type.klass,offsetof(abi::arm64::android11::Class11,component_type))!=type.component)return false;
  for(auto& field:type.fields)if(!s.public_roots.Read(field.type_root,&field.declared_type)||!field.declared_type||s.metadata.FieldDeclaringClass(field.identity)!=type.klass)return false;
 }
 s.expected=plan;s.plan=&plan;s.class_index.resize(plan.old_classes.size());s.instance_index.resize(plan.old_instances.size());
 if(!s.roots.Add(&s.expected.mirror,registrar)||!s.roots.Add(&s.expected.new_classes_root,registrar))return false;
 for(auto& origin:s.expected.class_root_origins)if(!s.roots.Add(&origin.array,registrar))return false;
 for(auto* group:{&s.expected.old_classes,&s.expected.new_classes,&s.expected.old_instances,&s.expected.new_instances})for(auto& value:*group)if(!s.roots.Add(&value,registrar))return false;
 for(auto& type:s.types){if(!s.roots.Add(&type.klass,registrar)||!s.roots.Add(&type.component,registrar))return false;for(auto& field:type.fields)if(!s.roots.Add(&field.declared_type,registrar))return false;}
 s.roots.Seal();s.captured=true;return true;
}
bool Proof::CanRefresh(const prepared_roots::Resolver&r)const noexcept{return state_->captured&&state_->roots.CanRefresh(r);}
void Proof::Refresh(const prepared_roots::Resolver&r)noexcept{auto&s=*state_;if(!CanRefresh(r))std::abort();s.roots.Refresh(r);s.refreshed=true;for(size_t i=0;i<s.types.size();++i)s.type_index[i]={uintptr_t(s.types[i].klass),i};std::sort(s.type_index.begin(),s.type_index.end());
 for(size_t i=0;i<s.expected.old_classes.size();++i)s.class_index[i]={uintptr_t(s.expected.old_classes[i]),i};
 for(size_t i=0;i<s.expected.old_instances.size();++i)s.instance_index[i]={uintptr_t(s.expected.old_instances[i]),i};
 std::sort(s.class_index.begin(),s.class_index.end());std::sort(s.instance_index.begin(),s.instance_index.end());}
bool Proof::ValidatePrepared(const source_commit::Plan& plan,const modern_field_metadata::ReadContext& maps,const native_required_cleanup::Service& cleanup,void* driver,const transaction_root::Holder* holder,const native_source_commit::Services& copies)const noexcept{
 state_->refusal=Refusal::Owner;
 return cleanup.VerifyPreparedCopies(plan,maps,driver,holder,copies)&&ValidateHeap(plan,maps,copies);
}
bool Proof::Validate(const source_commit::Plan& plan,const modern_field_metadata::ReadContext& maps,const native_required_cleanup::Service& cleanup,void* driver,const transaction_root::Holder* holder,const native_source_commit::Services& copies)const noexcept{
 state_->refusal=Refusal::Owner;
 return cleanup.VerifyFinalCopies(plan,maps,driver,holder,copies)&&ValidateHeap(plan,maps,copies);
}
bool Proof::ValidateHeap(const source_commit::Plan& plan,const modern_field_metadata::ReadContext& maps,const native_source_commit::Services& copies)const noexcept{auto&s=*state_;s.refusal=Refusal::Owner;
 if(!s.captured||!s.refreshed||s.plan!=&plan||!maps.owner||!maps.extent||
    plan.mirror!=s.expected.mirror||plan.new_classes_root!=s.expected.new_classes_root||plan.abstract_instances!=s.expected.abstract_instances||plan.class_root_origins.size()!=s.expected.class_root_origins.size()||plan.old_classes!=s.expected.old_classes||plan.new_classes!=s.expected.new_classes||plan.old_instances!=s.expected.old_instances||plan.new_instances!=s.expected.new_instances)return false;
 for(size_t i=0;i<plan.class_root_origins.size();++i)if(plan.class_root_origins[i].array!=s.expected.class_root_origins[i].array||plan.class_root_origins[i].index!=s.expected.class_root_origins[i].index)return false;
 s.maps=&maps;s.copies=&copies;s.refusal=Refusal::Inventory;
 for(const auto& type:s.types){
  if(!s.Readable(type.klass,offsetof(abi::arm64::android11::Class11,component_type)+4)||
     s.metadata.Reference(type.klass,offsetof(abi::arm64::android11::Class11,component_type))!=type.component){s.maps=nullptr;s.copies=nullptr;return false;}
  for(const auto& field:type.fields){if(!s.Readable(field.identity,sizeof(abi::arm64::Field))||!field.declared_type){s.maps=nullptr;s.copies=nullptr;return false;}
   const auto* native=static_cast<const abi::arm64::Field*>(field.identity);
   if(native->offset!=field.offset||bool(native->access_flags&8)!=field.statik||s.metadata.FieldDeclaringClass(field.identity)!=type.klass){s.maps=nullptr;s.copies=nullptr;return false;}
  }
 }
 s.refusal=Refusal::None;
 const bool pass=s.walk.Walk(s.context.self,s);s.maps=nullptr;s.copies=nullptr;if(!pass&&s.refusal==Refusal::None)s.refusal=Refusal::Inventory;return pass;
}
}
