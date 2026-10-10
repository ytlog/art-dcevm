#include "transaction_metadata_profile.h"
#include "platform/abi/arm64/android13/redefinition_art_abi.h"
#include "platform/android13/structural_method_updates.h"
#include "platform/android13/jni_method_targets.h"
#include "platform/abi/arm64/android13/art_method_abi.h"
#include "platform/abi/arm64/android13/validation_metadata_abi.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <unordered_map>
#include <thread>
#include <atomic>
using Method=artdcevm::android13::art_method_abi::ArtMethod;
using Class=artdcevm::android13::validation_abi::ClassPrefix;
struct Array { uint32_t size=0,pad=0; Method methods[8]{}; };
std::unordered_map<void*,void*> roots;
void* retired=nullptr; size_t calls=0,canon_calls=0;
bool Same(void* a,void* b) { return static_cast<Method*>(a)->dex_method_index==static_cast<Method*>(b)->dex_method_index; }
void* Root(void* m) { return roots.at(m); }
void Obsolete(void* linker,void* m) { assert(linker==reinterpret_cast<void*>(123)); assert(__atomic_load_n(&static_cast<Method*>(m)->access_flags,__ATOMIC_RELAXED)&0x40000); ++calls; static_cast<Method*>(m)->entry_point=999; }
void* Canonical(void* method,size_t size) { assert(size==8); ++canon_calls; return static_cast<unsigned char*>(method)+32; }
namespace artdcevm::android13 { void* DispatchClassLinker(){return reinterpret_cast<void*>(123);} bool IsRetiredMethodPointer(void* m){return m==retired;} }
class Hooks:public artdcevm::HookEngine { public:
 bool canonical_available=true;const char* missing=nullptr;
 bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char*,const char* symbol)override {
 if(missing&&std::strstr(symbol,missing))return nullptr;
 if (std::strstr(symbol,"HasSameNameAndSignature"))return reinterpret_cast<void*>(Same);
 if (std::strstr(symbol,"SetEntryPointsForObsoleteMethod"))return reinterpret_cast<void*>(Obsolete);
 if (std::strstr(symbol,"GetCanonicalMethod"))return canonical_available?reinterpret_cast<void*>(Canonical):nullptr;
 if (std::strcmp(symbol,"artReadBarrierForRootSlow")==0)return reinterpret_cast<void*>(Root);
 return nullptr; }
 void* Install(void*,void*,void**,std::string*)override{return nullptr;} bool Remove(void*,std::string*)override{return true;}
};
namespace artdcevm::android13 {void* ReadHeapReference(void*,size_t,bool){return nullptr;}}
int main(){ Hooks hooks; ConfigureTransactionReaderForTest(hooks); std::string error;
 for(const char* name:{"HasSameNameAndSignature","artReadBarrierForRootSlow","SetEntryPointsForObsoleteMethod"}){hooks.missing=name;assert(!artdcevm::android13::ConfigureStructuralMethodUpdates(hooks,&error)&&!error.empty());}
 hooks.missing=nullptr;
 assert(artdcevm::android13::ConfigureStructuralMethodUpdates(hooks,&error));
 artdcevm::android13::DeviceProfile profile;
 assert(!artdcevm::android13::ConfigureJniMethodTargets(hooks,profile,&error));
 assert(artdcevm::android13::MatchDeviceProfile(&profile,&error));
 hooks.canonical_available=false;
 assert(!artdcevm::android13::ConfigureJniMethodTargets(hooks,profile,&error));
 hooks.canonical_available=true;
 assert(artdcevm::android13::ConfigureJniMethodTargets(hooks,profile,&error));
 Class old{},replacement{},iface{}; Array a,b; a.size=8;b.size=6;old.methods=reinterpret_cast<uint64_t>(&a);replacement.methods=reinterpret_cast<uint64_t>(&b);
 for(unsigned i=0;i<a.size;++i){a.methods[i].dex_method_index=i;roots[&a.methods[i]]=&old;}
 a.methods[0].access_flags=0x100;a.methods[0].data=0x123456789abcdefULL;
 a.methods[1].access_flags=0x100;a.methods[1].data=77;retired=&a.methods[1];
 a.methods[2].access_flags=0x400;
 iface.access_flags=0x200;a.methods[3].access_flags=0x1000000;roots[&a.methods[3]]=&iface;
 a.methods[4].access_flags=0x100;a.methods[4].data=88;
 a.methods[5].access_flags=0x108;a.methods[5].data=99;
 a.methods[6].access_flags=1;a.methods[6].data=0xface; // Java -> native retains new JNI lookup binding
 a.methods[7].access_flags=0x101;a.methods[7].data=0xabcd; // native -> abstract retains new abstract metadata
 for(unsigned i=0;i<b.size;++i)roots[&b.methods[i]]=&replacement;
 b.methods[0].dex_method_index=0;b.methods[0].access_flags=0x100;
 b.methods[1].dex_method_index=4;b.methods[1].access_flags=0; // native becomes Java
 b.methods[2].dex_method_index=5;b.methods[2].access_flags=0x100;b.methods[2].data=41; // wrong staticness
 b.methods[3].dex_method_index=5;b.methods[3].access_flags=0x108;
 b.methods[4].dex_method_index=6;b.methods[4].access_flags=0x101;b.methods[4].data=0xbeef;
 b.methods[5].dex_method_index=7;b.methods[5].access_flags=0x401;b.methods[5].data=0;
 artdcevm::android13::UpdateStructuralMethods(&old,&replacement);assert(calls==7);
 assert(b.methods[0].data==a.methods[0].data && b.methods[3].data==99 && b.methods[2].data==41);
 assert(b.methods[4].data==0xbeef&&b.methods[5].data==0);
 assert(a.methods[6].data==0xface&&a.methods[7].data==0xabcd);
 assert(a.methods[1].data==77 && a.methods[1].entry_point==999); // retired native still walks obsolete frames
 assert((a.methods[2].access_flags&0x2040000)==0x40000);
 assert(a.methods[3].access_flags==0x1000000 && a.methods[3].entry_point==0);
 assert((a.methods[0].access_flags&0x2040000)==0x2040000);
 Method m{};assert(artdcevm::android13::CanonicalizeJniMethod(&m)==&m);m.access_flags=0x1000000;assert(artdcevm::android13::CanonicalizeJniMethod(&m)==reinterpret_cast<unsigned char*>(&m)+32);m.access_flags|=0x80000000;assert(artdcevm::android13::CanonicalizeJniMethod(&m)==&m);assert(canon_calls==1);
 Array empty;old.methods=reinterpret_cast<uint64_t>(&empty); artdcevm::android13::UpdateStructuralMethods(&old,&replacement);old.methods=0;artdcevm::android13::UpdateStructuralMethods(&old,&replacement);assert(calls==7);
 // ART flag readers may race with relaxed atomic updates even while Java is suspended.
 old.methods=reinterpret_cast<uint64_t>(&empty); empty.size=1; roots[&empty.methods[0]]=&old;
 std::atomic<bool> done{false};
 std::thread reader([&]{while(!done.load(std::memory_order_acquire)) (void)__atomic_load_n(&empty.methods[0].access_flags,__ATOMIC_RELAXED);});
 std::thread writer([&]{for(unsigned i=0;i<10000;++i)__atomic_fetch_or(&empty.methods[0].access_flags,0x1000u,__ATOMIC_RELAXED);});
 for(unsigned i=0;i<10000;++i)artdcevm::android13::UpdateStructuralMethods(&old,&replacement);
 writer.join();done.store(true,std::memory_order_release);reader.join();
 assert((empty.methods[0].access_flags&0x2041000)==0x2041000);
 puts("structural method updates: native retention/static identity/retired native/abstract/copied default/intrinsic canonicalization passed"); }
