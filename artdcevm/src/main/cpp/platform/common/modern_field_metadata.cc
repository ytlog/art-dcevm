#include "platform/common/modern_field_metadata.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/common/runnable_executor.h"
#include "platform/common/dex_file_metadata.h"
#include "platform/abi/arm64/android16/class.h"
#include "platform/abi/arm64/android17/class.h"
#include <cstdlib>
#include <cstring>
#include <set>
#include <vector>
#include <algorithm>
#include "platform/common/native_interface_graph.h"
namespace artdcevm::modern_field_metadata {
namespace {
using SplitClass=abi::arm64::android11::Class11;
using MergedClass=abi::arm64::android17::Class17;
static_assert(offsetof(abi::arm64::android16::Class16,status)==offsetof(MergedClass,status));
static_assert(offsetof(abi::arm64::android16::Class16,fields)==offsetof(MergedClass,fields));
static_assert(sizeof(MergedClass)==108);
struct Layout {field_array::Layout fields;size_t parent,loader,status,access_flags;};
constexpr Layout kSplit{field_array::kSplit,offsetof(SplitClass,super_class),
    offsetof(SplitClass,class_loader),offsetof(SplitClass,status),offsetof(SplitClass,access_flags)};
// Class16/17 share every field through status; the differing method-array tail
// is not read here. A class's initialized status and all field identities are
// checked independently before this declaration is retained.
constexpr Layout kMerged{{sizeof(MergedClass),offsetof(MergedClass,fields),offsetof(MergedClass,fields),true},
    offsetof(MergedClass,super_class),offsetof(MergedClass,class_loader),offsetof(MergedClass,status),
    offsetof(MergedClass,access_flags)};
const Layout& Source(Model model){return model==Model::kSplit?kSplit:kMerged;}
bool Fail(std::string* error, const char* message) { if(error)*error=message;return false; }
void Require(bool value) {if(!value)std::abort();}
}
bool FieldMetadata::Bind(HookEngine& hooks, std::string* error) {
  if (bound_) return true;
  auto leaf=reinterpret_cast<FieldLeaf>(hooks.FindSymbol("libart.so","_ZN3art6mirror5Field11GetArtFieldEv"));
  auto name=reinterpret_cast<FieldText>(hooks.FindSymbol("libart.so","_ZN3art8ArtField7GetNameEv"));
  auto descriptor=reinterpret_cast<FieldText>(hooks.FindSymbol("libart.so","_ZN3art8ArtField17GetTypeDescriptorEv"));
  auto class_descriptor=reinterpret_cast<ClassText>(hooks.FindSymbol("libart.so",
      "_ZN3art6mirror5Class13GetDescriptorEPNSt3__112basic_stringIcNS2_11char_traitsIcEENS2_9allocatorIcEEEE"));
  auto mark=reinterpret_cast<Mark>(hooks.FindSymbol("libart.so","artReadBarrierMark"));
  auto root=reinterpret_cast<Mark>(hooks.FindSymbol("libart.so","artReadBarrierForRootSlow"));
  if(!leaf||!class_descriptor||!mark||!root)
    return Fail(error,"Missing named reflected Field, Class descriptor or read barrier capability");
  named_interface_relation_=hooks.FindSymbol("libart.so","_ZN3art6mirror5Class10ImplementsENS_6ObjPtrIS1_EE")||
      hooks.FindSymbol("libart.so","_ZN3art6mirror5Class16IsAssignableFromENS_6ObjPtrIS1_EE");
  field_leaf_=leaf;name_=name;descriptor_=descriptor;class_descriptor_=class_descriptor;
  string_equals_=reinterpret_cast<StringEquals>(hooks.FindSymbol("libart.so","_ZN3art6mirror6String6EqualsEPKc"));
  mark_=mark;root_=root;bound_=true;
  if(error)error->clear();return true;
}
bool FieldMetadata::Text(void* field,Model model,const char** name,const char** descriptor,std::string* error)const {
  // Fixed legacy Class/DexCache/DexFile declarations, used only after the
  // reflected field's native identity and declaring Class have been established.
  // The ordinary split candidate must match every independent fixture field.
  void* klass=root_(field);size_t extent=0;
  const auto& source=Source(model);
  if(!klass||!method_identity::ReadableMappingExtent(klass,&extent)||extent<source.fields.class_size)
    return Fail(error,"Field declaring Class source prefix is not readable");
  uint32_t flags=0;std::memcpy(&flags,static_cast<const unsigned char*>(klass)+source.access_flags,4);
  // Fixed ArtField::IsProxyField/GetName/GetTypeDescriptor: only the rooted
  // declaring Class proxy flag selects these two synthetic static fields.
  // Their index is not an ordinary DEX FieldId. Public declared-field identity
  // remains independently compared by Candidate and the member graph verifier.
  if(flags&0x40000u){
    uint32_t index=0;std::memcpy(&index,static_cast<const unsigned char*>(field)+offsetof(field_array::Field,dex_field_index),4);
    if(!(field_array::Flags(field)&8u)||index>=2)
      return Fail(error,"Proxy field differs from fixed static index declaration");
    const char* source_name=index==0?"interfaces":"throws";
    const char* source_descriptor=index==0?"[Ljava/lang/Class;":"[[Ljava/lang/Class;";
    if(name_&&descriptor_){
      *name=name_(field);*descriptor=descriptor_(field);
      if(!*name||!*descriptor||std::strcmp(*name,source_name)||std::strcmp(*descriptor,source_descriptor))
        return Fail(error,"Named proxy field text differs from fixed source declaration");
    }else{*name=source_name;*descriptor=source_descriptor;}
    return true;
  }
  if(name_&&descriptor_) {
    *name=name_(field);*descriptor=descriptor_(field);
    return (*name&&*descriptor)||Fail(error,"Named field text is absent");
  }
  if(model!=Model::kSplit)return Fail(error,"Merged fields require named text services");
  void* cache=Reference(klass,offsetof(SplitClass,dex_cache));
  constexpr size_t offset=offsetof(abi::arm64::android11::LegacyCache,dex_file);
  if(!cache||!method_identity::ReadableMappingExtent(cache,&extent)||extent<offset+8)
    return Fail(error,"Legacy DexCache prefix is not readable");
  uint64_t raw_file=0;std::memcpy(&raw_file,static_cast<const unsigned char*>(cache)+offset,8);
  auto* file=reinterpret_cast<void*>(uintptr_t(raw_file));
  if(!file||!method_identity::ReadableMappingExtent(file,&extent)||extent<sizeof(dex_mapping_abi::Prefix))
    return Fail(error,"Legacy DexFile prefix is not readable");
  evolution::MappedDexMetadata mapping;
  if(!dex_file_metadata::ReadFile(30,file,extent,0,&mapping,error))return false;
  size_t header_extent=0,data_extent=0;
  if(!method_identity::ReadableMappingExtent(mapping.begin,&header_extent)||mapping.size>header_extent||
     !method_identity::ReadableMappingExtent(mapping.data_begin,&data_extent)||mapping.data_size>data_extent)
    return Fail(error,"Legacy DEX ranges exceed live readable mappings");
  uint32_t index=0;std::memcpy(&index,static_cast<const unsigned char*>(field)+offsetof(field_array::Field,dex_field_index),4);
  evolution::MappedDexFieldReference reference;
  if(!evolution::ReadMappedDexFieldReference(mapping,index,&reference,error))return false;
  const char* owner=nullptr;
  if(!Descriptor(klass,model,&owner,error)||std::strcmp(owner,reference.declaring_class))
    return Fail(error,"Legacy FieldId owner differs from named Class identity");
  *name=reference.name;*descriptor=reference.descriptor;
  return true;
}
void* FieldMetadata::FieldDeclaringClass(void* field)const {
  Require(calibrated_&&field&&root_);return root_(field);
}
void* FieldMetadata::Reference(void* object,size_t offset,bool is_volatile) const {
  Require(bound_&&object);
  const auto* bytes=static_cast<const unsigned char*>(object);
  const uint32_t monitor=__atomic_load_n(reinterpret_cast<const uint32_t*>(bytes+4),__ATOMIC_ACQUIRE);
  const uint32_t reference=__atomic_load_n(reinterpret_cast<const uint32_t*>(bytes+offset),is_volatile?__ATOMIC_SEQ_CST:__ATOMIC_RELAXED);
  void* raw=reinterpret_cast<void*>(static_cast<uintptr_t>(reference));
  return ((monitor>>28)&1u)?mark_(raw):raw;
}
bool FieldMetadata::FieldsCandidate(void* klass,Model model,bool is_static,field_array::View* output,std::string* error)const{
  const auto& layout=Source(model).fields;
  if(!bound_||!klass||!output)return Fail(error,"Invalid merged field metadata context");
  size_t class_extent=0;
  if(!method_identity::ReadableMappingExtent(klass,&class_extent))return Fail(error,"Class prefix has no readable mapping");
  void* array=nullptr;
  if(!field_array::ReadPointer(layout,klass,class_extent,is_static,&array))
    return Fail(error,"Class merged-field source candidate has an invalid prefix");
  size_t array_extent=0;
  if(array&&!method_identity::ReadableMappingExtent(array,&array_extent))
    return Fail(error,"Merged field allocation has no readable mapping");
  if(!field_array::Read(layout,array,array_extent,is_static,output))
    return Fail(error,"Merged field allocation exceeds its readable mapping");
  if(error)error->clear();return true;
}
bool FieldMetadata::Fields(void* klass,bool is_static,field_array::View* output,std::string* error)const{
  if(!calibrated_)return Fail(error,"Field source declarations are not behaviorally calibrated");
  return FieldsCandidate(klass,model_,is_static,output,error);
}
bool FieldMetadata::Candidate(const runnable_jni::Context& context,void* klass,Model model,
    const char* expected_class_descriptor,const FieldOracle* oracles,size_t count,std::string* error)const{
  const auto& layout=Source(model);
  size_t extent=0;
  if(!method_identity::ReadableMappingExtent(klass,&extent)||extent<layout.fields.class_size)
    return Fail(error,"Owned fixture class prefix has no readable mapping");
  const uint32_t status=__atomic_load_n(reinterpret_cast<const uint32_t*>(
      static_cast<const unsigned char*>(klass)+layout.status),__ATOMIC_SEQ_CST)>>28;
  if(status<14||status>15)return Fail(error,"Initialized fixture does not match Class status declaration");
  // Establish array ownership from the complete reflection oracle before
  // traversing candidate field storage. Mapping readability alone is not proof
  // that an arbitrary length header belongs to an ArtField allocation.
  std::vector<void*> native_fields;std::set<void*> seen;
  size_t static_count=0;
  uintptr_t first_all=UINTPTR_MAX,first_static=UINTPTR_MAX,first_instance=UINTPTR_MAX;
  for(size_t i=0;i<count;++i) {
    const auto& oracle=oracles[i];
    if(!oracle.strong_global_field||!oracle.name||!oracle.descriptor)
      return Fail(error,"Missing independent reflected field identity");
    void* reflected=nullptr;
    if(!runnable_executor::DecodeStrongGlobal(context,oracle.strong_global_field,&reflected)||!reflected)
      return Fail(error,"Cannot decode owned reflected Field");
    void* native=field_leaf_(reflected);
    size_t field_extent=0;
    if(context.env->ExceptionCheck()||!native||!seen.insert(native).second||
        !method_identity::ReadableMappingExtent(native,&field_extent)||field_extent<sizeof(field_array::Field))
      return Fail(error,"Named reflected Field accessor rejected independent identity");
    const char* name=nullptr;const char* descriptor=nullptr;
    if(root_(native)!=klass)return Fail(error,"Native field declaring root differs from owned fixture class");
    if(!Text(native,model,&name,&descriptor,error)||std::strcmp(name,oracle.name)||std::strcmp(descriptor,oracle.descriptor)||
        (field_array::Flags(native)&0xdfu)!=(oracle.modifiers&0xdfu))
      return Fail(error,"Native field identity differs from independent reflection");
    if(root_(native)!=klass)return Fail(error,"Native field declaring root differs from owned fixture class");
    native_fields.push_back(native);
    const uintptr_t address=reinterpret_cast<uintptr_t>(native);
    first_all=std::min(first_all,address);
    if(oracle.modifiers&8u){++static_count;first_static=std::min(first_static,address);}
    else first_instance=std::min(first_instance,address);
  }
  if(!static_count||static_count==count)return Fail(error,"Fixture must have static and instance fields");
  for(bool is_static:{false,true}) {
    void* array=nullptr;
    const uintptr_t first=layout.fields.merged?first_all:(is_static?first_static:first_instance);
    if(first<field_array::kDataOffset||!field_array::ReadPointer(layout.fields,klass,extent,is_static,&array)||
        reinterpret_cast<uintptr_t>(array)!=first-field_array::kDataOffset)
      return Fail(error,"Class field-array pointer differs from independent complete reflection");
    size_t mapped=0;
    if(!method_identity::ReadableMappingExtent(array,&mapped)||mapped<4)
      return Fail(error,"Field length has no readable mapping");
    uint32_t length=0;std::memcpy(&length,array,4);
    const size_t expected=layout.fields.merged?count:(is_static?static_count:count-static_count);
    if(length!=expected)return Fail(error,"Field length differs from independent reflection");
  }
  field_array::View instances,statics;
  if(!FieldsCandidate(klass,model,false,&instances,error)||!FieldsCandidate(klass,model,true,&statics,error))return false;
  if(instances.selected_count+statics.selected_count!=count||!instances.selected_count||!statics.selected_count||
      (layout.fields.merged&&(instances.array!=statics.array||instances.count!=count)))
    return Fail(error,"Independent reflected field count does not match native storage");
  for(size_t i=0;i<count;++i) {
    uint32_t physical=0;
    const auto& view=(oracles[i].modifiers&8u)?statics:instances;
    if(!view.IndexOf(native_fields[i],&physical))return Fail(error,"Reflected field is outside selected native array");
  }
  const char* actual_descriptor=nullptr;
  if(!Descriptor(klass,model,&actual_descriptor,error))return false;
  if(std::strcmp(actual_descriptor,expected_class_descriptor)) {
    if(error)*error="Fixture descriptor differs: actual="+std::string(actual_descriptor)+"; expected="+expected_class_descriptor;
    return false;
  }
  return true;
}
bool FieldMetadata::Calibrate(const runnable_jni::Context& context,jobject global_class,
    const char* expected_class_descriptor,const FieldOracle* oracles,size_t count,std::string* error){
  if(!bound_||!context.self||!context.env||!context.vm||!global_class||!expected_class_descriptor||
      !oracles||count<2||count>1024||context.env->ExceptionCheck())
    return Fail(error,"Invalid owned field calibration request");
  void* klass=nullptr;
  if(!runnable_executor::DecodeStrongGlobal(context,global_class,&klass)||!klass)return Fail(error,"Cannot decode owned fixture class");
  std::string split_error,merged_error;
  const bool split=Candidate(context,klass,Model::kSplit,expected_class_descriptor,oracles,count,&split_error);
  if(context.env->ExceptionCheck())return Fail(error,"Named field calibration raised JNI exception");
  const bool merged=Candidate(context,klass,Model::kMerged,expected_class_descriptor,oracles,count,&merged_error);
  if(context.env->ExceptionCheck())return Fail(error,"Named field calibration raised JNI exception");
  if(split==merged){
    if(error)*error=split?"Ambiguous Class/field storage source declarations":
        "No Class/field source declaration matched: split="+split_error+"; merged="+merged_error;
    return false;
  }
  model_=split?Model::kSplit:Model::kMerged;calibrated_=true;
  if(!named_interface_relation_&&!interface_declaration_calibrated_){
    if(!native_interface_graph::Calibrate(context,global_class,*this,error)){calibrated_=false;return false;}
    interface_declaration_calibrated_=true;
  }
  if(error)error->clear();return true;
}
bool FieldMetadata::Descriptor(void* klass,Model model,const char** output,std::string* error)const {
  if(!bound_||!klass||!output)return Fail(error,"Missing live Class for named descriptor identity");
  const auto& layout=Source(model);
  size_t extent=0;
  if(!method_identity::ReadableMappingExtent(klass,&extent)||extent<layout.fields.class_size)
    return Fail(error,"Class source prefix has no readable mapping");
  const auto* bytes=static_cast<const unsigned char*>(klass);
  uint32_t component=0,flags=0;
  std::memcpy(&component,bytes+offsetof(MergedClass,component_type),4);
  std::memcpy(&flags,bytes+layout.access_flags,4);
  // Fixed Class::GetDescriptor only writes platform std::string storage for
  // arrays/proxies. Reject those classes and pass nullptr on the source's
  // dim==0 non-proxy return path; NDK strings never cross the platform ABI.
  if(component||(flags&0x40000u))return Fail(error,"Array/proxy descriptor needs unavailable platform string storage");
  const char* descriptor=class_descriptor_(klass,nullptr);
  if(!descriptor||!*descriptor)return Fail(error,"Named Class descriptor is absent");
  *output=descriptor;
  if(error)error->clear();return true;
}

bool FieldMetadata::ClassIdentityVerified(void* klass,const char* expected,void* public_name,
    const char* name,const char* negative,field_copy::CopyClassView* out,std::string* error)const {
  if(!calibrated_||!klass||!expected||!*expected||!out)return Fail(error,"Verified Class identity lacks independently staged descriptor");
  const auto& layout=Source(model_);void* terminal=klass;size_t dim=0;
  while(expected[dim]=='['){
    size_t extent=0;if(!method_identity::ReadableMappingExtent(terminal,&extent)||extent<layout.fields.class_size)return Fail(error,"Array component source prefix is unreadable");
    terminal=Reference(terminal,offsetof(MergedClass,component_type));
    if(!terminal)return Fail(error,"Native array dimensions differ from public descriptor");++dim;
  }
  size_t extent=0;if(!method_identity::ReadableMappingExtent(terminal,&extent)||extent<layout.fields.class_size)return Fail(error,"Terminal Class source prefix is unreadable");
  if(Reference(terminal,offsetof(MergedClass,component_type)))return Fail(error,"Native array has additional unstaged dimensions");
  uint32_t flags=0;std::memcpy(&flags,static_cast<const char*>(terminal)+layout.access_flags,4);
  if(flags&0x40000u){
    if(!public_name||!name||!negative||!string_equals_||Reference(terminal,offsetof(MergedClass,name))!=public_name)return Fail(error,"Proxy name lacks independent public String root");
    // The native String is independently paired with the actual public
    // Class.getName return. Exact positive/negative MUTF-8 oracles attest the
    // named leaf; its declaring-Class loader is checked separately by caller.
    if(!string_equals_(public_name,name)||string_equals_(public_name,negative))return Fail(error,"Named proxy String equality differs from public positive/negative names");
    const char* descriptor=expected+dim;const size_t length=std::strlen(descriptor);
    if(length<2||descriptor[0]!='L'||descriptor[length-1]!=';'||std::strlen(name)!=length-2)return Fail(error,"Proxy public name/descriptor length differs");
    for(size_t i=0;i<length-2;++i)if((name[i]=='.'?'/':name[i])!=descriptor[i+1])return Fail(error,"Proxy public name/descriptor spelling differs");
  }else{
    const char* actual=nullptr;if(!Descriptor(terminal,model_,&actual,error))return false;
    if(std::strcmp(actual,expected+dim))return Fail(error,"Native terminal descriptor differs from public Class signature");
  }
  field_array::View fields;if(!Fields(klass,false,&fields,error))return false;
  auto* parent=Reference(klass,layout.parent);
  *out={parent,Reference(klass,layout.loader),expected,fields.selected_count,!parent&&!std::strcmp(expected,"Ljava/lang/Object;")};
  if(error)error->clear();return true;
}

bool FieldMetadata::ClassIdentity(void* klass,field_copy::CopyClassView* out,std::string* error)const {
  if(!calibrated_||!klass||!out)return Fail(error,"Class identity lacks calibrated rooted source");
  const auto& layout=Source(model_);field_array::View fields;
  if(!Fields(klass,false,&fields,error))return false;
  const char* descriptor=nullptr;if(!Descriptor(klass,model_,&descriptor,error))return false;
  auto* parent=Reference(klass,layout.parent);
  *out={parent,Reference(klass,layout.loader),descriptor,fields.selected_count,
      !parent&&!std::strcmp(descriptor,"Ljava/lang/Object;")};
  return true;
}
bool FieldMetadata::FieldIdentity(void* field,field_copy::CopyFieldView* out,std::string* error)const {
  if(!calibrated_||!field||!out)return Fail(error,"Field identity lacks calibrated rooted source");
  const char *name=nullptr,*descriptor=nullptr;
  if(!Text(field,model_,&name,&descriptor,error))return false;
  *out={field,name,descriptor};return true;
}
field_copy::CopyClassView FieldMetadata::Class(void* klass,bool is_static)const{
  Require(calibrated_&&klass);
  const auto& layout=Source(model_);
  auto* parent=Reference(klass,layout.parent);
  auto* loader=Reference(klass,layout.loader);
  field_array::View fields;std::string error;Require(Fields(klass,is_static,&fields,&error));
  const char* descriptor="Ljava/lang/Object;";
  if(parent)Require(Descriptor(klass,model_,&descriptor,&error));
  return {parent,loader,descriptor,fields.selected_count,parent==nullptr};
}
field_copy::CopyFieldView FieldMetadata::Field(void* klass,bool is_static,size_t ordinal)const{
  Require(calibrated_&&klass);
  field_array::View fields;std::string error;Require(Fields(klass,is_static,&fields,&error));
  Require(ordinal<fields.selected_count);
  void* field=fields.AtSelected(static_cast<uint32_t>(ordinal));
  const char* name=nullptr;const char* descriptor=nullptr;
  Require(Text(field,model_,&name,&descriptor,&error));return {field,name,descriptor};
}
}
