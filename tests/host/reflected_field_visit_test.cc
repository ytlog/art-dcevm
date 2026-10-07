#include "platform/android13/reflected_field_visit.h"
#include "platform/android13/member_decode_abi.h"
#include "platform/android13/structural_reflective_abi.h"
#include "platform/android13/field_copy_metadata_abi.h"
#include "platform/android13/validation_metadata_abi.h"
#include "platform/android13/retired_field_registry.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <unordered_map>
#include <sstream>
#include <sys/mman.h>
using namespace artdcevm::android13;
using Field=member_decode_abi::ReflectedField;
using Native=field_copy_abi::ArtField;
using Class=validation_abi::ClassPrefix;
using Source=structural_reflective_abi::SourceInfo;
struct Array{uint32_t size;Native fields[3];};
RetiredFieldRegistry registry;std::unordered_map<void*,void*> roots;
size_t barriers=0,describes=0,visits=0,reference_stores=0;
Field* active=nullptr;void* expected_original=nullptr;void* result=nullptr;
// Real virtual declaration matching ART's ReflectionSourceInfo/HeapReflectiveSourceInfo.
// Its named vtable is supplied to production configuration as the device ELF symbol.
struct Info {virtual ~Info()=default;virtual void Describe(std::ostream&)const{++describes;assert(type==2&&object==active);} uint32_t type=2;void* object=nullptr;};
const void* info_vptr=nullptr;
namespace artdcevm::android13 {
uint32_t RetiredFieldTokenFor(void* field){return registry.TokenFor(field);}
void* DecodeReflectedField(void* raw){auto* field=static_cast<Field*>(raw);if(field->art_field_index&0x80000000)return registry.Resolve(field->art_field_index);
 auto* klass=reinterpret_cast<Class*>(uintptr_t(field->declaring_class));auto* array=reinterpret_cast<Array*>((field->access_flags&8)?klass->sfields:klass->ifields);assert(field->art_field_index<array->size);return &array->fields[field->art_field_index];}
void WriteCopyValue(void* raw,void* metadata,char descriptor,uint64_t value){assert(descriptor=='L');auto* native=static_cast<Native*>(metadata);assert(native->offset==offsetof(Field,declaring_class));
 assert(value<=UINT32_MAX);std::memcpy(static_cast<unsigned char*>(raw)+native->offset,&value,4);++reference_stores;++barriers;}
void CopyWriteBarrier(void* field){assert(field==active);++barriers;}
}
void* ReadRoot(void* member){return roots.at(member);}
extern "C" bool ArtDcevmSourceProbeReflectedFieldVisit(const void* vptr){assert(vptr==info_vptr);return true;}
class Hooks:public artdcevm::HookEngine {public:bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char*,const char* name)override{if(std::strcmp(name,"_ZTVN3art24HeapReflectiveSourceInfoE")==0)return const_cast<unsigned char*>(static_cast<const unsigned char*>(info_vptr))-2*sizeof(void*);if(std::strcmp(name,"artReadBarrierForRootSlow")==0)return reinterpret_cast<void*>(ReadRoot);return nullptr;}
 void* Install(void*,void*,void**,std::string*)override{return nullptr;}bool Remove(void*,std::string*)override{return true;}};
class Visitor final:public structural_reflective_abi::Visitor {public:
 void* VisitMethod(void*,const Source&)override{assert(false);return nullptr;}
 void* VisitField(void* original,const Source& source)override{++visits;assert(original==expected_original);auto& info=*reinterpret_cast<const Info*>(&source);assert(info.type==2&&info.object==active);
 std::ostringstream output;info.Describe(output);return result;}};
int main(){Info source;std::memcpy(&info_vptr,static_cast<const void*>(&source),sizeof(info_vptr));Hooks hooks;std::string error;assert(ConfigureReflectedFieldVisit(hooks,dlopen(nullptr,RTLD_NOW),&error));Visitor visitor;
 void* low=mmap(reinterpret_cast<void*>(0x20000000),4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON|MAP_FIXED,-1,0);assert(low==reinterpret_cast<void*>(0x20000000));
 auto* old_class=new(low)Class{};auto* new_class=new(static_cast<unsigned char*>(low)+256)Class{};
 Array old_array{},new_instance{},new_static{};old_array.size=1;new_instance.size=3;new_static.size=3;
 old_class->ifields=reinterpret_cast<uint64_t>(&old_array);new_class->ifields=reinterpret_cast<uint64_t>(&new_instance);new_class->sfields=reinterpret_cast<uint64_t>(&new_static);
 Native* old=&old_array.fields[0];old->offset=12;Native* instance=&new_instance.fields[2];instance->access_flags=0x41;instance->offset=52;
 Native* statik=&new_static.fields[1];statik->access_flags=0x18;statik->offset=76;roots[instance]=roots[statik]=new_class;
 Field field{};field.declaring_class=uint32_t(reinterpret_cast<uintptr_t>(old_class));field.type=0x1122;field.access_flags=1;field.offset=12;field.accessible=1;active=&field;expected_original=old;
 result=old;Field before=field;VisitReflectedFieldTarget(&field,&visitor);assert(!std::memcmp(&before,&field,sizeof(field))&&barriers==0);
 registry.Retire(old);result=old;VisitReflectedFieldTarget(&field,&visitor);assert(field.art_field_index==registry.TokenFor(old));assert(field.offset==before.offset&&field.access_flags==before.access_flags&&field.declaring_class==before.declaring_class&&barriers==0);
 // A retired token decodes its original native field without consulting replacement class layouts.
 result=instance;VisitReflectedFieldTarget(&field,&visitor);assert(field.declaring_class==uint32_t(reinterpret_cast<uintptr_t>(new_class))&&field.offset==52&&field.access_flags==0x41&&field.art_field_index==2);assert(field.type==0x1122&&field.accessible==1&&barriers==2&&reference_stores==1);
 expected_original=instance;result=statik;VisitReflectedFieldTarget(&field,&visitor);assert(field.offset==76&&field.access_flags==0x18&&field.art_field_index==1&&barriers==4&&reference_stores==2);
 // A removed replacement keeps all cache metadata and changes only its opaque token.
 Native retired{};registry.Retire(&retired);expected_original=statik;result=&retired;before=field;VisitReflectedFieldTarget(&field,&visitor);assert(field.art_field_index==registry.TokenFor(&retired));before.art_field_index=field.art_field_index;assert(!std::memcmp(&before,&field,sizeof(field))&&barriers==4);
 assert(visits==5&&describes==5);munmap(low,4096);
 puts("reflected Field visitor: real device info vtable/Describe, identity no-op, retired tokens, replacement metadata/index, static transition and card barriers passed");}
