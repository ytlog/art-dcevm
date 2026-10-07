#include "device_profile_fixture.h"
#include "platform/android13/instrumentation_listener.h"
#include "platform/android13/instrumentation_listener_abi.h"
#include "platform/android13/art_method_abi.h"
#include "platform/android13/field_copy_metadata_abi.h"
#include "platform/android13/member_access_guards.h"
#include "platform/android13/thread_suspend_scope.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <dlfcn.h>
#include <map>
#include <vector>
#include <sys/mman.h>
namespace g=artdcevm::android13; namespace a=g::listener_abi;
using Class=g::validation_abi::ClassPrefix;
using Method=g::art_method_abi::ArtMethod;
namespace {
a::Listener* registered; a::Listener* listener;
unsigned char thread[512]{};unsigned char* arena;size_t used;
template<class T> T* New(){used=(used+alignof(T)-1)&~(alignof(T)-1);auto* p=new(arena+used) T{};used+=sizeof(T);return p;}
uint32_t Ref(void* p){assert(uintptr_t(p)<=UINT32_MAX);return uint32_t(uintptr_t(p));}
void* Root(void* p){return reinterpret_cast<void*>(uintptr_t(*static_cast<uint32_t*>(p)));}
void* Current(){return thread;}
bool enabled,required,suspended;size_t adds,removes,deopts,undeopts,resolves,throws;
void* instrumentation=reinterpret_cast<void*>(42);void* declared;void* moved;
unsigned char frame[80]{};std::string text;const char* name="value";
void* Resolve(void* linker,uint16_t type,void*){assert(linker==reinterpret_cast<void*>(43)&&type==0);++resolves;uint32_t p=Ref(moved);std::memcpy(frame+72,&p,4);return declared;}
void* Obsolete(void*){return nullptr;}
const char* Name(void*){return name;}
bool Proxy(void*,const char*){return false;}
void Throw(void* self,const char* type,const char* msg){assert(self==thread&&std::strcmp(type,"Ljava/lang/IncompatibleClassChangeError;")==0);++throws;text=msg;}
void Add(void* i,a::Listener* l,uint32_t e){assert(suspended&&i==instrumentation&&e==0x38);registered=listener=l;++adds;}
void Remove(void* i,a::Listener* l,uint32_t e){assert(suspended&&i==instrumentation&&l==registered&&e==0x38);registered=nullptr;++removes;}
void Deopt(void* i,const char* k){assert(suspended&&i==instrumentation&&std::strcmp(k,"artdcevm-field-read-probe")==0);++deopts;}
void Undeopt(void* i,const char* k){assert(suspended&&i==instrumentation&&std::strcmp(k,"artdcevm-field-read-probe")==0);++undeopts;}
class Hooks:public artdcevm::HookEngine {
 public:
 bool Initialize(std::string*) override{return true;}void* Install(void*,void*,void**,std::string*)override{return nullptr;}bool Remove(void*,std::string*)override{return true;}
 void* FindSymbol(const char*,const char* symbol) override {
  std::string s=symbol;
  if(s=="artReadBarrierForRootSlow")return reinterpret_cast<void*>(Root);
  if(s.find("CurrentFromGdb")!=s.npos)return reinterpret_cast<void*>(Current);
  if(s.find("GetObsoleteDexCache")!=s.npos)return reinterpret_cast<void*>(Obsolete);
  if(s.find("DoResolveType")!=s.npos)return reinterpret_cast<void*>(Resolve);
  if(s.find("GetName")!=s.npos)return reinterpret_cast<void*>(Name);
  if(s.find("ProxyDescriptorEquals")!=s.npos)return reinterpret_cast<void*>(Proxy);
  if(s.find("ThrowNewException")!=s.npos)return reinterpret_cast<void*>(Throw);
  if(s.find("AddListener")!=s.npos)return reinterpret_cast<void*>(Add);
  if(s.find("RemoveListener")!=s.npos)return reinterpret_cast<void*>(::Remove);
  if(s.find("UndeoptimizeEverything")!=s.npos)return reinterpret_cast<void*>(Undeopt);
  if(s.find("DeoptimizeEverything")!=s.npos)return reinterpret_cast<void*>(Deopt);
  return nullptr;
 }
};
}
namespace artdcevm::android13 {
bool ReceiverGuardEnabled(){return enabled;}bool ReceiverGuardRequired(){return required;}
void SetReceiverGuardEnabled(bool v){assert(suspended);enabled=v;}void SetReceiverGuardRequired(bool v){required=v;}
void* RuntimeInstrumentation(){return instrumentation;}void* RuntimeClassLinker(){return reinterpret_cast<void*>(43);}
void* ReadHeapReference(void* p,size_t off,bool){return reinterpret_cast<void*>(uintptr_t(*reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(p)+off)));}
bool IsValidationAssignableFrom(void* target,void* actual){return target==actual;}
NativeSuspendScope::NativeSuspendScope(void* self):self_(self),supported_(true),transitioned_(true){assert(self==thread);}
NativeSuspendScope::~NativeSuspendScope(){assert(!suspended&&self_==thread&&transitioned_);}
OwnedSuspendAll::OwnedSuspendAll(const char*,bool){assert(!suspended);storage_={};suspended=true;}
OwnedSuspendAll::~OwnedSuspendAll(){assert(suspended);suspended=false;}
}
int main(){ auto profile=MatchedProfileForTest();
 arena=static_cast<unsigned char*>(mmap(reinterpret_cast<void*>(0x22000000),65536,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0));assert(arena!=MAP_FAILED&&uintptr_t(arena)+65536<=UINT32_MAX);
 Hooks hooks;std::string error;assert(!g::ConfigureInstrumentationListener(hooks,g::DeviceProfile{},&error));if (!g::ConfigureInstrumentationListener(hooks,profile,&error)) {std::cerr<<error<<"\n";std::abort();}
 assert(g::SetReceiverGuardMode(true));assert(enabled&&adds==1&&deopts==1);assert(g::SetReceiverGuardMode(true,true)&&adds==1);
 required=true;assert(!g::SetReceiverGuardMode(false));assert(enabled&&removes==0);required=false;
 auto* owner=New<Class>();auto* unrelated=New<Class>();auto* object=New<Class>();object->object.klass=Ref(owner);auto* changed=New<Class>();changed->object.klass=Ref(unrelated);
 auto* field=New<g::field_copy_abi::ArtField>();field->declaring_class=Ref(owner);uint32_t object_root=Ref(object);a::Handle handle{&object_root};uint64_t value=55;
 listener->FieldRead(thread,handle,nullptr,0,field);assert(throws==0);object_root=Ref(changed);
 listener->FieldWritten(thread,handle,nullptr,0,field,value);listener->FieldWritten(thread,handle,nullptr,0,field,a::Handle{});assert(throws==2&&value==55);
 auto* cache=New<a::DexCache>();owner->dex_cache=Ref(cache);auto* method=New<Method>();method->declaring_class=Ref(owner);uint16_t code[12]{};code[8]=0x6e;code[9]=0;code[10]=0;uint32_t count=3;std::memcpy(reinterpret_cast<unsigned char*>(code)+12,&count,4);method->data=uintptr_t(code);
 uint32_t header[28]{};header[22]=1;a::MethodId id{};a::DexFile dex{};dex.prefix.header=header;dex.prefix.method_ids=&id;cache->prefix.dex_file=uintptr_t(&dex);
 g::member_access_abi::ShadowFrame sf{};sf.method=method;sf.registers=2;std::memcpy(frame,&sf,sizeof(sf));uint32_t receiver=Ref(object);std::memcpy(frame+72,&receiver,4);void* f=frame;std::memcpy(thread+168+16,&f,8);
 alignas(8) uint64_t pairs[1024]{};a::TypePair pair{Ref(owner),0};std::memcpy(&pairs[0],&pair,8);cache->types=uintptr_t(pairs);
 listener->DexPcMoved(thread,{},method,0);assert(resolves==0&&g::InvokeGuardDiagnostics(0)==1&&g::InvokeGuardDiagnostics(1)==0);
 pair.index=1;std::memcpy(&pairs[0],&pair,8);declared=owner;moved=changed;
 listener->DexPcMoved(thread,{},method,0);assert(resolves==1&&throws==3&&g::InvokeGuardDiagnostics(1)==1);
 f=nullptr;std::memcpy(thread+184,&f,8);listener->DexPcMoved(thread,{},method,0);assert(g::InvokeGuardDiagnostics(2)==1);
 code[8]=0x71;listener->DexPcMoved(thread,{},method,0);assert(g::InvokeGuardDiagnostics(2)==1);
 // A large CompactDex preheader count is checked through the real callback.
 std::vector<uint16_t> compact(70007);compact[0]=1;compact[1]=4464;compact[3]=uint16_t((3<<5)|0x10);
 compact[4+70000]=0x78;dex.compact=true;method->data=uintptr_t(compact.data()+2)|1;
 f=frame;std::memcpy(thread+184,&f,8);pair={Ref(owner),0};std::memcpy(&pairs[0],&pair,8);receiver=Ref(object);std::memcpy(frame+72,&receiver,4);
 uint32_t compact_pc=70000;std::memcpy(frame+offsetof(g::member_access_abi::ShadowFrame,pc),&compact_pc,4);
 auto checks=g::InvokeGuardDiagnostics(0);listener->DexPcMoved(thread,{},method,70000);assert(g::InvokeGuardDiagnostics(0)==checks+1);
 listener->DexPcMoved(thread,{},method,70003);assert(g::InvokeGuardDiagnostics(0)==checks+1);
 method->access_flags=0x100;listener->DexPcMoved(thread,{},method,70000);assert(g::InvokeGuardDiagnostics(0)==checks+1);method->access_flags=0;
 assert(g::SetReceiverGuardMode(false));assert(!enabled&&removes==1&&undeopts==1);
 assert(g::SetReceiverGuardMode(true));assert(g::InvokeGuardDiagnostics(0)==0&&g::InvokeGuardDiagnostics(1)==0&&g::InvokeGuardDiagnostics(2)==0&&g::FieldReadProbeHits()==0);
 assert(g::InvokeGuardDiagnostics(-1)==0&&g::InvokeGuardDiagnostics(3)==0);
 assert(g::SetReceiverGuardMode(false));assert(g::SetReceiverGuardMode(true,true));
 const char fixture[]="\x30" "Lcom/github/ytlog/artdcevm/fixture/NarrowParent;";uint32_t string_id=0,type_id=0;header[14]=1;header[16]=1;dex.prefix.mapping.data_begin=reinterpret_cast<const unsigned char*>(fixture);dex.prefix.mapping.data_size=sizeof(fixture);dex.prefix.string_ids=&string_id;dex.prefix.type_ids=&type_id;name="ancestor";object_root=Ref(object);owner->primitive_type=a::kNonPrimitiveEncoding;
 listener->FieldRead(thread,handle,nullptr,0,field);assert(g::FieldReadProbeHits()==1&&text=="Field access probe rejected access before storage mutation");
 assert(g::SetReceiverGuardMode(true,false));listener->FieldWritten(thread,handle,nullptr,0,field,value);assert(g::FieldReadProbeHits()==2&&value==55);
 assert(g::SetReceiverGuardMode(false));munmap(arena,65536);
 std::cout<<"listener slots, receiver guards, registration lifecycle, cache fast/slow and GC reload, CompactDex count: passed\n";
}
