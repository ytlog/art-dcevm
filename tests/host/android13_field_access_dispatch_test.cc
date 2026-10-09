#include "device_profile_fixture.h"
#include "platform/android13/retired_field_hooks.cc"
#include <cassert>
#include <iostream>
#include <map>
namespace g=artdcevm::android13;
namespace {
int thread,declaring,foreign,bootstrap;int field;
uint32_t field_flags=1;bool pending=false,initialized=false;
bool target_static=false,target_primitive=true;size_t target_size=4;
size_t checked_calls=0,unchecked_calls=0,nterp_calls=0;int expected_type;
uint32_t expected_index;void* expected_caller;size_t expected_size;
void Parameters(int type,uint32_t index,void* caller,void* self,size_t size){
 assert(type==expected_type&&index==expected_index&&caller==expected_caller&&self==&thread&&size==expected_size);
}
template<int type> void* Checked(uint32_t index,void* caller,void* self,size_t size){
 Parameters(type,index,caller,self,size);++checked_calls;
 // Independent fixed-source boundary: check current member authority even
 // when the caller's historical verified/readonly code skipped checks.
 if(pending)return nullptr;
 if(bool(type&2)!=target_static||bool(type&8)!=target_primitive||size!=target_size){pending=true;return nullptr;}
 if(field_flags==2&&caller!=&declaring){pending=true;return nullptr;}
 initialized=true;return &field;
}
template<int type> void* Unchecked(uint32_t index,void* caller,void* self,size_t size){Parameters(type,index,caller,self,size);++unchecked_calls;return &field;}
uint32_t Instance(void* self,void* caller,uint16_t* pc,size_t resolve){assert(self==&thread&&caller==expected_caller&&pc[1]==expected_index&&resolve==17);++nterp_calls;return 24;}
size_t Static(void* self,void* caller,uint16_t* pc,size_t resolve){assert(self==&thread&&caller==expected_caller&&pc[1]==expected_index&&resolve==17);++nterp_calls;return reinterpret_cast<size_t>(&field);}
void Uncalled(){std::abort();}
struct Hooks:artdcevm::HookEngine{
 std::map<void*,std::string> names;std::map<std::string,void*> proxies;size_t next=1;int missing=0;
 bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char*,const char* name)override{
  const std::string symbol=name;
#define LEAF(TYPE) if(symbol==g::FieldResolverSymbol(TYPE,true))return missing==TYPE?nullptr:reinterpret_cast<void*>(Checked<TYPE>);
  LEAF(21)LEAF(37)LEAF(25)LEAF(41)LEAF(22)LEAF(38)LEAF(26)LEAF(42)
#undef LEAF
  void* token=reinterpret_cast<void*>(next++);names[token]=symbol;return token;
 }
 void* Install(void* target,void* proxy,void** original,std::string*)override{
  auto symbol=names.at(target);proxies[symbol]=proxy;*original=reinterpret_cast<void*>(Uncalled);
#define LEAF(TYPE) if(symbol==g::FieldResolverSymbol(TYPE,false))*original=reinterpret_cast<void*>(Unchecked<TYPE>);
  LEAF(21)LEAF(37)LEAF(25)LEAF(41)LEAF(22)LEAF(38)LEAF(26)LEAF(42)
#undef LEAF
  if(symbol==artdcevm::android13::cas::kRetiredFieldHooks[5])*original=reinterpret_cast<void*>(Instance);
  if(symbol==artdcevm::android13::cas::kRetiredFieldHooks[6])*original=reinterpret_cast<void*>(Static);
  return target;
 }
 bool Remove(void*,std::string*)override{return true;}
};
}
// Non-bytecode hooks are deliberately unreachable in this typed dispatch test.
namespace artdcevm::android13 {
bool ConfigureReflectedFieldVisit(HookEngine&,const DeviceProfile& p,std::string*){return p.Matched();}
void* DecodeReflectedField(void*){std::abort();}void VisitReflectedFieldTarget(void*,void*){std::abort();}
bool GuardFieldVarHandle(void*,int,void*,const void*,void*){std::abort();}
bool GuardMethodHandleFieldPut(void*){std::abort();}bool GuardRunnableFieldId(jfieldID){std::abort();}
bool GuardRunnableFieldReceiver(const void*,jobject,jfieldID){std::abort();}
bool GuardNativeFieldId(JNIEnv*,jfieldID){std::abort();}bool GuardNativeFieldReceiver(JNIEnv*,jobject,jfieldID){std::abort();}
bool GuardNativeReflectedField(JNIEnv*,jobject){std::abort();}
bool GuardNterpFinalFieldPut(void*,void*,uint16_t*){return false;}
}
int main(){
 Hooks hooks;std::string error;hooks.missing=41;
 assert(!g::InstallRetiredFieldHooks(hooks,MatchedProfileForTest(),&error)&&hooks.proxies.empty());
 hooks.missing=0;assert(g::InstallRetiredFieldHooks(hooks,MatchedProfileForTest(),&error));
 for(int type:g::kFieldTypes)assert(!hooks.proxies.count(g::FieldResolverSymbol(type,true)));
 expected_index=0x9876;expected_caller=&foreign;expected_size=4;expected_type=25;
 auto resolve=reinterpret_cast<g::CheckedField>(hooks.proxies.at(g::FieldResolverSymbol(25,false)));
 assert(resolve(expected_index,expected_caller,&thread,expected_size)==&field&&unchecked_calls==1&&checked_calls==0);
 g::RequireFieldAccessChecks();assert(g::FieldAccessChecksRequired());
 for(int type:g::kFieldTypes){expected_type=type;expected_size=(type&8)?8:4;target_static=bool(type&2);target_primitive=bool(type&8);target_size=expected_size;auto fn=reinterpret_cast<g::CheckedField>(hooks.proxies.at(g::FieldResolverSymbol(type,false)));
  field_flags=2;pending=false;assert(fn(expected_index,&foreign,&thread,expected_size)==nullptr&&pending);
  expected_caller=&declaring;pending=false;assert(fn(expected_index,&declaring,&thread,expected_size)==&field&&initialized);
  expected_caller=&bootstrap;pending=false;assert(fn(expected_index,&bootstrap,&thread,expected_size)==nullptr&&pending);
  field_flags=1;pending=false;assert(fn(expected_index,&bootstrap,&thread,expected_size)==&field);
  pending=false;target_size=expected_size+1;assert(fn(expected_index,&bootstrap,&thread,expected_size)==nullptr&&pending);target_size=expected_size;
  pending=false;target_static=!target_static;assert(fn(expected_index,&bootstrap,&thread,expected_size)==nullptr&&pending);target_static=!target_static;
  pending=false;target_primitive=!target_primitive;assert(fn(expected_index,&bootstrap,&thread,expected_size)==nullptr&&pending);target_primitive=!target_primitive;expected_caller=&foreign;
 }
 using InstanceFn=uint32_t(*)(void*,void*,uint16_t*,size_t);using StaticFn=size_t(*)(void*,void*,uint16_t*,size_t);
 auto instance=reinterpret_cast<InstanceFn>(hooks.proxies.at(g::cas::kRetiredFieldHooks[5]));
 auto statik=reinterpret_cast<StaticFn>(hooks.proxies.at(g::cas::kRetiredFieldHooks[6]));
 constexpr size_t sizes[]={4,8,4,1,1,2,2};
 for(unsigned op=0x52;op<=0x6d;++op){unsigned group=(op-0x52)/7,kind=(op-0x52)%7;bool is_static=group>=2,write=group&1,object=kind==2;
  expected_type=g::kFieldTypes[(is_static?4:0)+(object?0:2)+(write?1:0)];expected_size=sizes[kind];target_static=is_static;target_primitive=!object;target_size=expected_size;uint16_t pc[]={uint16_t(op|0xa000),uint16_t(expected_index)};
  field_flags=2;pending=false;auto before=nterp_calls;
  if(is_static)assert(statik(&thread,&foreign,pc,17)==0);else assert(instance(&thread,&foreign,pc,17)==0);
  assert(pending&&nterp_calls==before); // no denied offset/cache publication
  expected_caller=&declaring;pending=false;
  if(is_static)assert(statik(&thread,&declaring,pc,17)==reinterpret_cast<size_t>(&field));else assert(instance(&thread,&declaring,pc,17)==24);
  expected_caller=&foreign;field_flags=1;pending=false;
  if(is_static)assert(statik(&thread,&foreign,pc,17)==reinterpret_cast<size_t>(&field));else assert(instance(&thread,&foreign,pc,17)==24);
  // Existing pending exception must not be replaced by a success/cache entry.
  before=nterp_calls;pending=true;
  if(is_static)assert(statik(&thread,&foreign,pc,17)==0);else assert(instance(&thread,&foreign,pc,17)==0);
  assert(pending&&nterp_calls==before);
 }
 assert(unchecked_calls==1);std::cout<<"Android13 actual field hooks: eight checked/unchecked typed families, all 28 opcode sizes, cached foreign/boot read/write refusal, authorized same-Class/public restore, pending exception and zero denied offset publication PASS\n";
}
