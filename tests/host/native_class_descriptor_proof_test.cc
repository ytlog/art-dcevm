#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "native_commit_fixture.inc"
#pragma clang diagnostic pop
#include <map>
std::map<void*,const char*> terminal_names;
size_t comparisons=0;bool bogus_equals=false;
bool StringEquals(void* string,const char* name){++comparisons;return bogus_equals||std::strcmp(static_cast<const char*>(string),name)==0;}
const char* TerminalDescriptor(void* klass,void* storage){assert(!storage);auto* c=static_cast<class_metadata::Class17*>(klass);assert(!c->component_type&&!(c->access_flags&0x40000));auto i=terminal_names.find(klass);return i==terminal_names.end()?Descriptor(klass,storage):i->second;}
struct Snapshot {
 const void* deny=nullptr;const void* truncate=nullptr;size_t calls=0;
 static bool Extent(void* owner,const void* address,size_t* bytes){auto& s=*static_cast<Snapshot*>(owner);++s.calls;
  if(!address||address==s.deny||uintptr_t(address)<uintptr_t(heap.base)||uintptr_t(address)-uintptr_t(heap.base)>=(1u<<20))return false;
  *bytes=address==s.truncate?1:(1u<<20)-(uintptr_t(address)-uintptr_t(heap.base));return true;}
};
class TypedHooks:public Hooks {public:void* FindSymbol(const char* lib,const char* symbol)override{if(std::strstr(symbol,"String6EqualsEPKc"))return reinterpret_cast<void*>(StringEquals);if(std::strstr(symbol,"Class13GetDescriptor"))return reinterpret_cast<void*>(TerminalDescriptor);return Hooks::FindSymbol(lib,symbol);}};
int main(){
 InitJni();InitializeHeap();TypedHooks hooks;std::string error;
 std::array<modern_field_metadata::FieldOracle,4> oracle{};for(size_t i=0;i<4;++i)oracle[i]={reinterpret_cast<jobject>(&mirrors[i]),names[i],types[i],i==0?8u:1u};
 modern_field_metadata::FieldMetadata metadata;assert(metadata.Bind(hooks,&error));
 struct Request{modern_field_metadata::FieldMetadata* service;const modern_field_metadata::FieldOracle* oracle;std::string* error;} request{&metadata,oracle.data(),&error};
 const auto calibrate=[](const runnable_jni::Context& context,void* p){auto& r=*static_cast<Request*>(p);return r.service->Calibrate(context,reinterpret_cast<jobject>(old_class),"LFixture;",r.oracle,4,r.error);};
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),calibrate,&request)==runnable_jni::Result::kSuccess);
 const auto make=[](){auto* c=static_cast<class_metadata::Class17*>(heap.New(160));InitializeClass(c);return c;};
 auto* primitive=make();primitive->super_class=0;primitive->primitive_type=5;terminal_names[primitive]="I";
 auto* array=make();array->component_type=uint32_t(uintptr_t(primitive));auto* nested=make();nested->component_type=uint32_t(uintptr_t(array));
 auto* proxy=make();proxy->access_flags=0x40011;
 auto* name=static_cast<char*>(heap.New(128));std::strcpy(name,"custom.\xe4\xb8\xadProxy");proxy->name=uint32_t(uintptr_t(name));
 auto* proxy_array=make();proxy_array->component_type=uint32_t(uintptr_t(proxy));
 Snapshot snapshot;modern_field_metadata::ReadContext context{&snapshot,Snapshot::Extent};
 field_copy::CopyClassView view;const int before=jni_calls;const uint64_t queries=method_identity::MappingOsQueries();
 assert(metadata.ClassIdentityVerified(old_class,"LFixture;",nullptr,nullptr,nullptr,&view,&error,&context));
 assert(metadata.ClassIdentityVerified(primitive,"I",nullptr,nullptr,nullptr,&view,&error,&context));
 assert(metadata.ClassIdentityVerified(array,"[I",nullptr,nullptr,nullptr,&view,&error,&context));
 assert(metadata.ClassIdentityVerified(nested,"[[I",nullptr,nullptr,nullptr,&view,&error,&context));
 assert(!metadata.ClassIdentityVerified(nested,"[I",nullptr,nullptr,nullptr,&view,&error,&context));
 assert(!metadata.ClassIdentityVerified(array,"[[I",nullptr,nullptr,nullptr,&view,&error,&context));
 assert(!metadata.ClassIdentityVerified(array,"[J",nullptr,nullptr,nullptr,&view,&error,&context));
 const char* descriptor="Lcustom/\xe4\xb8\xadProxy;";
 assert(metadata.ClassIdentityVerified(proxy,descriptor,name,name,"wrong",&view,&error,&context));assert(comparisons==2);
 assert(metadata.ClassIdentityVerified(proxy_array,"[Lcustom/\xe4\xb8\xadProxy;",name,name,"wrong",&view,&error,&context));
 assert(!metadata.ClassIdentityVerified(proxy,descriptor,nullptr,name,"wrong",&view,&error,&context));
 assert(!metadata.ClassIdentityVerified(proxy,descriptor,name,"custom.Other","wrong",&view,&error,&context));
 bogus_equals=true;assert(!metadata.ClassIdentityVerified(proxy,descriptor,name,name,"wrong",&view,&error,&context));bogus_equals=false;
 assert(!metadata.ClassIdentityVerified(proxy,"Lwrong/Name;",name,name,"wrong",&view,&error,&context));
 // Re-read the independent public String root after moving GC; stale addresses
 // cannot grant proxy identity, while the refreshed root does.
 auto* moved=static_cast<char*>(heap.New(128));std::strcpy(moved,name);proxy->name=uint32_t(uintptr_t(moved));
 assert(!metadata.ClassIdentityVerified(proxy,descriptor,name,name,"wrong",&view,&error,&context));
 assert(metadata.ClassIdentityVerified(proxy,descriptor,moved,name,"wrong",&view,&error,&context));
 assert(jni_calls==before&&method_identity::MappingOsQueries()==queries&&snapshot.calls>20);
 // Every nested component prefix is checked before reading it, even when
 // another snapshot still maps the address. No fallback on denial/truncation.
 for(const void* value:{static_cast<void*>(nested),static_cast<void*>(array),static_cast<void*>(primitive)}){
  snapshot.deny=value;assert(!metadata.ClassIdentityVerified(nested,"[[I",nullptr,nullptr,nullptr,&view,&error,&context));snapshot.deny=nullptr;
  snapshot.truncate=value;assert(!metadata.ClassIdentityVerified(nested,"[[I",nullptr,nullptr,nullptr,&view,&error,&context));snapshot.truncate=nullptr;
 }
 modern_field_metadata::ReadContext missing{&snapshot,nullptr};assert(!metadata.ClassIdentityVerified(old_class,"LFixture;",nullptr,nullptr,nullptr,&view,&error,&missing));
 assert(method_identity::MappingOsQueries()==queries);
 assert(metadata.ClassIdentityVerified(old_class,"LFixture;",nullptr,nullptr,nullptr,&view,&error));assert(method_identity::MappingOsQueries()>queries);
 std::cout<<"Actual calibrated Class identity: ordinary/primitive/nested array/proxy/proxy-array, exact dimensions/names/public String roots, positive-negative leaf and moved-root rejection PASS\n";
}
