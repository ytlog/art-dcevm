#include "device_profile_fixture.h"
#include "handle_scope_fixture.h"
#include "platform/android13/member_access_guards.h"
#include "platform/abi/arm64/android13/member_access_guards_abi.h"
#include "platform/android13/native_object_access.h"
#include "platform/abi/arm64/android13/native_object_access_abi.h"
#include "platform/android13/handle_root_scope.h"
#include "platform/abi/arm64/android13/handle_root_scope_abi.h"
#include "platform/abi/arm64/android13/validation_metadata_abi.h"
#include "platform/abi/arm64/android13/art_method_abi.h"
#include "platform/abi/arm64/android13/field_copy_metadata_abi.h"
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <dlfcn.h>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <sys/mman.h>
namespace g=artdcevm::android13;
using Class=g::validation_abi::ClassPrefix;
using Method=g::art_method_abi::ArtMethod;
using Field=g::field_copy_abi::ArtField;
using Handle=g::member_access_abi::MethodHandle;
using Type=g::member_access_abi::MethodType;
using VarHandle=g::member_access_abi::FieldVarHandle;
namespace {
alignas(8) unsigned char thread[512]{};
unsigned char* arena; size_t used=0;
template<class T> T* New() { used=(used+alignof(T)-1)&~(alignof(T)-1); auto* result=new(arena+used) T{};used+=sizeof(T);return result; }
std::map<std::pair<void*,size_t>,void*> heap;
std::map<void*,void*> roots,old_classes;
std::map<void*,void*> direct_interfaces;
std::set<std::pair<void*,void*>> assignable;
bool final_enabled=false;
void *retired_method=nullptr,*retired_field=nullptr,*resolved_field=nullptr,*cache=nullptr,*loader=nullptr;
size_t throws=0,final_throws=0,resolves=0,root_reads=0,template_calls=0;
std::string exception_type,exception_text;
uint32_t interface_count=1;
void* Root(void* m) { ++root_reads;return roots.at(m); }
void* Current() { return thread; }
void* peer=nullptr;void* Peer(void*){return peer;}
void* OldCache(void*) { return cache; }
void* OldClass(void* k) { auto i=old_classes.find(k);return i==old_classes.end()?nullptr:i->second; }
void* Types(void*) { return &interface_count; }
void* Direct(void* k,uint32_t i) { assert(i<2);auto it=direct_interfaces.find(k);return it==direct_interfaces.end()?nullptr:it->second; }
uint32_t Template(uint32_t mode) { ++template_calls;return mode==0||mode==2||mode==4||mode==6?0:1; }
void Throw(void* self,const char* type,const char* text) { assert(self==thread);++throws;exception_type=type;exception_text=text; }
void ThrowFinal(void*,void*) { ++final_throws; }
void* Resolve(void* linker,uint32_t index,void* cache_slot,void* loader_slot) {
 assert(linker==reinterpret_cast<void*>(123) && index==0x4567);
 assert(*static_cast<uint32_t*>(cache_slot)==reinterpret_cast<uintptr_t>(cache));
 assert(*static_cast<uint32_t*>(loader_slot)==reinterpret_cast<uintptr_t>(loader));
 void* scope;std::memcpy(&scope,thread+g::handle_root_abi::kTopHandleScopeOffset,8);assert(scope);
 ++resolves;return resolved_field;
}
void Enter(void* self) { assert(self==thread);*reinterpret_cast<uint32_t*>(thread)&=0xffffff; }
void Leave(void* self) { assert(self==thread);*reinterpret_cast<uint32_t*>(thread)|=g::native_access_abi::kNative<<24; }
artdcevm::abi::arm64::android11::ReleaseObjectPointer Decode(const void* self,jobject object) { assert(self==thread);return {reinterpret_cast<uintptr_t>(object)}; }
artdcevm::abi::arm64::android11::NativeOffset SelfOffset(size_t size) { assert(size==8);return {8}; }
class Hooks:public artdcevm::HookEngine { public:
 bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char*,const char* s)override {
 if(!std::strcmp(s,"artReadBarrierForRootSlow"))return reinterpret_cast<void*>(Root);
 if(std::strstr(s,"GetObsoleteDexCache"))return reinterpret_cast<void*>(OldCache);
 if(std::strstr(s,"GetObsoleteClass"))return reinterpret_cast<void*>(OldClass);
 if(std::strstr(s,"GetInterfaceTypeList"))return reinterpret_cast<void*>(Types);
 if(std::strstr(s,"GetDirectInterface"))return reinterpret_cast<void*>(Direct);
 if(std::strstr(s,"GetAccessModeTemplate"))return reinterpret_cast<void*>(Template);
 if(std::strstr(s,"ResolveFieldJLS"))return reinterpret_cast<void*>(Resolve);
 if(std::strstr(s,"ThrowIllegalAccessErrorFinalField"))return reinterpret_cast<void*>(ThrowFinal);
 if(std::strstr(s,"ThrowNewException"))return reinterpret_cast<void*>(Throw);
 if(std::strstr(s,"HandleScopeVisitRoots"))return reinterpret_cast<void*>(HostHandleScopeVisitRoots);
 if(std::strstr(s,"GetPeerFromOtherThread"))return reinterpret_cast<void*>(Peer);
 if(std::strstr(s,"CurrentFromGdb"))return reinterpret_cast<void*>(Current);
 if(!std::strcmp(s,"artJniMethodEnd"))return reinterpret_cast<void*>(Enter);
 if(!std::strcmp(s,"artJniMethodStart"))return reinterpret_cast<void*>(Leave);
 if(std::strstr(s,"DecodeJObject"))return reinterpret_cast<void*>(Decode);
 if(std::strstr(s,"SelfOffset"))return reinterpret_cast<void*>(SelfOffset);
 return nullptr;
 }
 void* Install(void*,void*,void**,std::string*)override{return nullptr;}
 bool Remove(void*,std::string*)override{return true;}
};
jboolean JNICALL NoException(JNIEnv*){return JNI_FALSE;}
struct Operands:art::InstructionOperands {
 explicit Operands(uint32_t value):art::InstructionOperands(1),index(value){}
 uint32_t index;uint32_t GetOperand(size_t i)const override{assert(i==0);return index;}
};
struct Parameters { uint32_t klass,monitor,length,ref; };
struct Frame { g::member_access_abi::ShadowFrame header;uint32_t registers[4]; };
}
namespace artdcevm::android13 {
void* ReadHeapReference(void* object,size_t offset,bool is_volatile) { assert(!is_volatile);return heap[{object,offset}]; }
bool IsValidationAssignableFrom(void* target,void* source){return target==source||assignable.count({target,source});}
void* RuntimeClassLinker(){return reinterpret_cast<void*>(123);}
void* DecodeMethodId(jmethodID id){return id;}
void* DecodeFieldId(jfieldID id){return id;}

bool GuardRetiredMethod(void* m,void*,void* result){if(m!=retired_method)return false;if(result)std::memset(result,0,8);return true;}
bool GuardRetiredField(void* f,void*,void* result){if(f!=retired_field)return false;if(result)std::memset(result,0,8);return true;}
}
int main(){
 arena=static_cast<unsigned char*>(mmap(reinterpret_cast<void*>(0x20000000),65536,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0));
 assert(arena!=MAP_FAILED && reinterpret_cast<uintptr_t>(arena)+65536<=UINT32_MAX);
 Class *declaring=New<Class>(),*caller=New<Class>(),*unrelated=New<Class>(),*direct=New<Class>();
 auto* object=New<Class>();auto* method=New<Method>();auto* field=New<Field>();
 auto* handle=New<Handle>();auto* type=New<Type>();auto* parameters=New<Parameters>();auto* vh=New<VarHandle>();
 roots[method]=declaring;roots[field]=declaring;heap[{object,0}]=unrelated;
 JNINativeInterface_ table{};table.ExceptionCheck=NoException;
 struct Env{JNIEnv jni;void* self;void* vm;} env{{&table},thread,nullptr};
 Hooks hooks;std::string error;peer=object;
 assert(g::ConfigureNativeObjectAccess(hooks,MatchedProfileForTest(),&error));
 assert(g::ConfigureHandleRootScopes(hooks,MatchedProfileForTest(),&error));
 g::DeviceProfile unmatched;assert(!g::ConfigureMemberAccessGuards(hooks,unmatched,&error));
 assert(g::ConfigureMemberAccessGuards(hooks,MatchedProfileForTest(),&error));
 g::member_access_abi::RunnableAccess access{thread,&env,nullptr};
 assert(!g::GuardRunnableFieldReceiver(&access,reinterpret_cast<jobject>(object),reinterpret_cast<jfieldID>(field)));
 g::SetReceiverGuardEnabled(true);g::SetReceiverGuardRequired(true);assert(g::ReceiverGuardEnabled()&&g::ReceiverGuardRequired());
 assert(g::GuardRunnableFieldReceiver(&access,reinterpret_cast<jobject>(object),reinterpret_cast<jfieldID>(field)));
 assert(exception_type=="Ljava/lang/IncompatibleClassChangeError;");
 assert(g::GuardRunnableMethodReceiver(&access,reinterpret_cast<jobject>(object),method));
 assert(g::GuardRunnableMethodIdReceiver(&access,reinterpret_cast<jobject>(object),reinterpret_cast<jmethodID>(method)));
 *reinterpret_cast<uint32_t*>(thread)=g::native_access_abi::kNative<<24;
 assert(g::GuardNativeFieldReceiver(&env.jni,reinterpret_cast<jobject>(object),reinterpret_cast<jfieldID>(field)));
 assert(*reinterpret_cast<uint32_t*>(thread)==g::native_access_abi::kNative<<24);*reinterpret_cast<uint32_t*>(thread)=0;
 assert(!g::GuardRunnableFieldReceiver(&access,nullptr,reinterpret_cast<jfieldID>(field)));
 field->access_flags=8;assert(!g::GuardRunnableFieldReceiver(&access,reinterpret_cast<jobject>(object),reinterpret_cast<jfieldID>(field)));field->access_flags=0;
 assignable.insert({declaring,unrelated});assert(!g::GuardRunnableMethodReceiver(&access,reinterpret_cast<jobject>(object),method));assignable.clear();
 uint32_t slot=reinterpret_cast<uintptr_t>(handle);uint64_t result=99;
 handle->target=reinterpret_cast<uintptr_t>(method);handle->kind=g::member_access_abi::kVirtual;
 retired_method=method;assert(g::GuardMethodHandleTarget(thread,&slot,&result)&&result==0);retired_method=nullptr;
 handle->target=reinterpret_cast<uintptr_t>(field);handle->kind=g::member_access_abi::kInstanceGet;
 retired_field=field;assert(g::GuardMethodHandleTarget(thread,&slot,nullptr));retired_field=nullptr;
 handle->target=reinterpret_cast<uintptr_t>(method);handle->kind=g::member_access_abi::kSuper;
 heap[{handle,offsetof(Handle,method_type)}]=type;heap[{type,offsetof(Type,parameters)}]=parameters;
 parameters->length=1;heap[{parameters,12}]=caller;
 result=123;assert(g::GuardMethodHandleTarget(thread,&slot,&result)&&result==0);
 heap[{caller,offsetof(Class,super_class)}]=declaring;assert(!g::GuardMethodHandleTarget(thread,&slot,nullptr));
 declaring->access_flags=0x200;heap[{caller,offsetof(Class,super_class)}]=nullptr;
 direct_interfaces[caller]=direct;assignable.insert({declaring,direct});assert(!g::GuardMethodHandleTarget(thread,&slot,nullptr));
 // An interface reachable only through a superclass does not pass invokespecial.
 direct_interfaces.clear();assert(g::GuardMethodHandleTarget(thread,&slot,nullptr));
 parameters->length=0;assert(g::GuardMethodHandleTarget(thread,&slot,nullptr));parameters->length=1;
 vh->field=reinterpret_cast<uintptr_t>(field);field->access_flags=16;
 Frame frame{};frame.header.registers=2;frame.registers[2]=reinterpret_cast<uintptr_t>(object);Operands ops(0);
 for(int mode=0;mode<=30;++mode){result=77;bool write=mode!=0&&mode!=2&&mode!=4&&mode!=6;
   bool rejected=g::GuardFieldVarHandle(vh,mode,&frame,&ops,&result);
   assert(rejected);assert(write?result==0:result==77);
   assert(exception_type==(write?"Ljava/lang/UnsupportedOperationException;":"Ljava/lang/IncompatibleClassChangeError;"));
 }
 auto templates_before=template_calls;result=88;
 assert(g::GuardFieldVarHandle(vh,-1,&frame,&ops,&result)&&result==88);
 assert(g::GuardFieldVarHandle(vh,31,&frame,&ops,&result)&&result==88&&template_calls==templates_before);
 retired_field=field;result=19;assert(g::GuardFieldVarHandle(vh,1,&frame,&ops,&result)&&result==0&&template_calls==templates_before);retired_field=nullptr;
 g::SetReceiverGuardEnabled(false);assert(!g::GuardFieldVarHandle(vh,0,&frame,&ops,nullptr));g::SetReceiverGuardEnabled(true);
 field->access_flags=0;result=91;assert(g::GuardFieldVarHandle(vh,1,&frame,&ops,&result)&&result==91);
 assignable.insert({declaring,unrelated});assert(!g::GuardFieldVarHandle(vh,1,&frame,&ops,&result));assignable.clear();
 field->access_flags=16;assert(!g::GuardMethodHandleFieldPut(field));
 assert(!g::GuardNterpFinalFieldPut(nullptr,nullptr,nullptr));final_enabled=true;
 assert(g::GuardMethodHandleFieldPut(field));retired_field=field;assert(g::GuardMethodHandleFieldPut(field));retired_field=nullptr;
 cache=New<Class>();loader=New<Class>();heap[{declaring,offsetof(Class,dex_cache)}]=cache;heap[{declaring,offsetof(Class,class_loader)}]=loader;
 resolved_field=field;uint16_t iput[]={0x59,0x4567},sput[]={0x67,0x4567},iget[]={0x52,0x4567};
 size_t before=resolves;assert(!g::GuardNterpFinalFieldPut(thread,method,iget)&&resolves==before);
 assert(!g::GuardNterpFinalFieldPut(thread,method,iput)); // same declaring class can write final
 roots[method]=caller;heap[{caller,offsetof(Class,dex_cache)}]=cache;heap[{caller,offsetof(Class,class_loader)}]=loader;
 assert(g::GuardNterpFinalFieldPut(thread,method,sput)&&final_throws==1);
 caller->access_flags=0x200000;old_classes[declaring]=caller;
 method->access_flags=0x40000;assert(!g::GuardNterpFinalFieldPut(thread,method,iput)); // obsolete clinit remains allowed
 resolved_field=nullptr;assert(!g::GuardNterpFinalFieldPut(thread,method,iput));
 void* pending=reinterpret_cast<void*>(42);std::memcpy(thread+g::handle_root_abi::kExceptionOffset,&pending,8);
 assert(g::GuardNterpFinalFieldPut(thread,method,sput));
 void* top;std::memcpy(&top,thread+g::handle_root_abi::kTopHandleScopeOffset,8);assert(!top);
 std::cout<<"member access guards: JNI receiver states, retirement dispatch, direct-interface/super guards, all 31 VarHandle modes, result semantics, final writes, obsolete clinit and rooted resolution passed\n";
 munmap(arena,65536);
}

namespace artdcevm::final_field_guard { bool FinalFieldGuardEnabled() noexcept{return final_enabled;} }
