#include "transaction_metadata_profile.h"
#include "platform/android13/redefinition_art_abi.h"
#include "platform/android13/structural_reflective.h"
#include "platform/android13/art_method_abi.h"
#include "platform/android13/field_copy_metadata.h"
#include "platform/android13/validation_metadata_abi.h"
#include "platform/android13/retired_method_registry.h"
#include "platform/android13/retired_field_registry.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <unordered_map>
using namespace artdcevm::android13;
using namespace structural_reflective_abi;
using Method=art_method_abi::ArtMethod;
using Class=validation_abi::ClassPrefix;
struct Methods {uint32_t size=0,pad=0;Method value[4]{};};
struct Field {uint32_t declaring_class=0,flags=0;const char* name;const char* type;};
struct Fields {std::vector<Field*> statik,instance;};
std::unordered_map<void*,Fields> fields;
std::unordered_map<void*,void*> roots;
RetiredMethodRegistry methods;RetiredFieldRegistry retired_fields;
size_t dispatch=0;void* mirror=nullptr;void* assignable=nullptr;
Visitor* active=nullptr;void (*scenario)(Visitor*)=nullptr;
namespace artdcevm::android13 {
void RetireMethodPointer(void* p)noexcept{methods.Retire(p);}void RetireFieldPointer(void* p)noexcept{retired_fields.Retire(p);}
bool IsRetiredMethodPointer(void* p){return methods.Contains(p);}bool IsRetiredFieldPointer(void* p){return retired_fields.TokenFor(p)!=0;}
void RequireDispatchChecks(){++dispatch;}
CopyClassView ReadCopyClass(void* p,bool statik){return {nullptr,nullptr,nullptr,statik?fields[p].statik.size():fields[p].instance.size(),false};}
CopyFieldView ReadCopyField(void* p,bool statik,size_t i){auto* f=(statik?fields[p].statik:fields[p].instance).at(i);return {f,f->name,f->type};}
bool IsValidationAssignableFrom(void* a,void* b){assert(b==mirror);return a==assignable;}
void* CanonicalizeJniMethod(void* m){return static_cast<unsigned char*>(m)+32;}
}
bool Same(void* a,void* b){return static_cast<Method*>(a)->dex_method_index==static_cast<Method*>(b)->dex_method_index;}
void* Root(void* p){return roots.at(p);}
const char* Name(void* f){return static_cast<Field*>(f)->name;}const char* Type(void* f){return static_cast<Field*>(f)->type;}
void Visit(void*,Visitor* v){scenario(v);}
class Hooks:public artdcevm::HookEngine {public:
 bool missing_identity=false;
 bool Initialize(std::string*)override{return true;}void* FindSymbol(const char*,const char* name)override{
 if(std::strstr(name,"HasSameName"))return missing_identity?nullptr:reinterpret_cast<void*>(Same);
 if(std::strstr(name,"VisitReflectiveTargets"))return reinterpret_cast<void*>(Visit);
 if(std::strcmp(name,"artReadBarrierForRootSlow")==0)return reinterpret_cast<void*>(Root);
 if(std::strstr(name,"7GetName"))return reinterpret_cast<void*>(Name);
 if(std::strstr(name,"17GetTypeDescriptor"))return reinterpret_cast<void*>(Type);return nullptr;}
 void* Install(void*,void*,void**,std::string*)override{return nullptr;}bool Remove(void*,std::string*)override{return true;}};
Class old_class{},new_class{},parent{},unrelated{};Methods old_array,new_array;StructuralMemberMappings mappings;
Field retained{0,0,"same","I"},replacement{0,0,"same","I"},removed{0,0,"gone","I"},wrong_type{0,0,"gone","J"};
Field shadow{0,0,"shadow","I"},new_shadow{0,0,"shadow","I"},private_shadow{0,2,"shadow","I"},static_shadow{0,8,"shadow","I"};
struct Executable {unsigned char prefix[32]{};uint32_t flags=999;};
void Basic(Visitor* v){SourceInfo generic{nullptr,10},dexm{nullptr,kDexMethod},dexf{nullptr,kDexField},jni{nullptr,kJniMethod};
 assert(v->VisitMethod(&old_array.value[0],generic)==&new_array.value[0]);
 assert(v->VisitMethod(&old_array.value[1],generic)==&old_array.value[1]);
 assert(v->VisitMethod(&old_array.value[1],dexm)==nullptr);
 assert(v->VisitMethod(&old_array.value[0],jni)==reinterpret_cast<unsigned char*>(&new_array.value[0])+32);
 assert(v->VisitField(&retained,generic)==&replacement);assert(v->VisitField(&removed,generic)==&removed);assert(v->VisitField(&removed,dexf)==nullptr);
 Executable object;HeapSourceInfo heap{{nullptr,kExecutable},&object};assert(v->VisitMethod(&old_array.value[0],heap.base)==&new_array.value[0]);assert(object.flags==new_array.value[0].access_flags);
 object.flags=123;v->VisitMethod(&old_array.value[1],heap.base);assert(object.flags==123);
 Method unknown{};assert(v->VisitMethod(&unknown,generic)==&unknown);assert(v->VisitField(&wrong_type,generic)==&wrong_type);
 assert(v->VisitMethod(nullptr,generic)==nullptr&&v->VisitField(nullptr,generic)==nullptr);
}
Method inherited{},constructor{},virtual_method{},unrelated_method{};
void Shadowing(Visitor* v){SourceInfo dexm{nullptr,kDexMethod},dexf{nullptr,kDexField},generic{nullptr,10};
 assert(v->VisitMethod(&inherited,dexm)==nullptr);assert(v->VisitMethod(&inherited,generic)==&inherited);
 assert(v->VisitMethod(&constructor,dexm)==&constructor);assert(v->VisitMethod(&virtual_method,dexm)==&virtual_method);assert(v->VisitMethod(&unrelated_method,dexm)==&unrelated_method);
 assert(v->VisitField(&shadow,dexf)==nullptr);assert(v->VisitField(&shadow,generic)==&shadow);
 assert(v->VisitField(&private_shadow,dexf)==&private_shadow);assert(v->VisitField(&static_shadow,dexf)==&static_shadow);
}
void Hierarchy(Visitor* v){SourceInfo dexm{nullptr,kDexMethod},dexf{nullptr,kDexField},generic{nullptr,10};
 assert(v->VisitMethod(&unrelated_method,dexm)==nullptr);assert(v->VisitField(&static_shadow,dexf)==nullptr);assert(v->VisitMethod(&unrelated_method,generic)==&unrelated_method);}
namespace artdcevm::android13 {void* ReadHeapReference(void*,size_t,bool){return nullptr;}}
int main(){ auto profile=MatchedProfileForTest();Hooks hooks;ConfigureTransactionReaderForTest(hooks);std::string error;hooks.missing_identity=true;assert(!ConfigureStructuralMemberMappings(hooks,&error)&&!error.empty());hooks.missing_identity=false;assert(ConfigureStructuralMemberMappings(hooks,&error));assert(!ConfigureStructuralReflectiveTargets(hooks,artdcevm::android13::DeviceProfile{},&error));assert(ConfigureStructuralReflectiveTargets(hooks,profile,&error));
 old_array.size=3;new_array.size=2;old_class.methods=reinterpret_cast<uint64_t>(&old_array);new_class.methods=reinterpret_cast<uint64_t>(&new_array);
 old_array.value[0].dex_method_index=1;old_array.value[0].access_flags=1;new_array.value[0].dex_method_index=1;new_array.value[0].access_flags=2;
 old_array.value[1].dex_method_index=2;old_array.value[1].access_flags=8;new_array.value[1].dex_method_index=2;new_array.value[1].access_flags=0;
 old_array.value[2].dex_method_index=3;fields[&old_class].instance={&retained,&removed};fields[&new_class].instance={&replacement,&wrong_type};
 for(unsigned i=0;i<old_array.size;++i)roots[&old_array.value[i]]=&old_class;
 for(unsigned i=0;i<new_array.size;++i)roots[&new_array.value[i]]=&new_class;
 mappings=BuildStructuralMemberMappings({&old_class},{&new_class});assert(dispatch==2&&methods.Count()==2);assert(mappings.fields.at(&removed)==&removed&&mappings.methods.at(&old_array.value[1])==&old_array.value[1]);
 scenario=Basic;VisitStructuralReflectiveTargets(nullptr,&old_class,{&old_class},{&new_class},mappings,false);
 mirror=&old_class;assignable=&parent;inherited.dex_method_index=7;inherited.access_flags=8;constructor=inherited;constructor.access_flags=0x10000|8;virtual_method=inherited;virtual_method.access_flags=1;unrelated_method=inherited;
 roots[&inherited]=roots[&constructor]=roots[&virtual_method]=&parent;roots[&unrelated_method]=&unrelated;
 roots[&shadow]=roots[&private_shadow]=roots[&static_shadow]=&parent;fields[&new_class].instance.push_back(&new_shadow);
 new_array.size=3;new_array.value[2].dex_method_index=7;new_array.value[2].access_flags=8;new_class.virtual_methods_offset=3;
 scenario=Shadowing;VisitStructuralReflectiveTargets(nullptr,&old_class,{&old_class},{&new_class},mappings,false);
 // Old participating declarations also shadow even when assignability is false.
 assignable=nullptr;scenario=Shadowing;VisitStructuralReflectiveTargets(nullptr,&old_class,{&old_class,&parent},{&new_class},mappings,false);
 scenario=Hierarchy;VisitStructuralReflectiveTargets(nullptr,&old_class,{&old_class},{&new_class},mappings,true);
 puts("member mapping/reflection: deletion, type/static/visibility changes, DexCache shadowing, hierarchy clearing, cached modifiers, JNI targets passed");}
