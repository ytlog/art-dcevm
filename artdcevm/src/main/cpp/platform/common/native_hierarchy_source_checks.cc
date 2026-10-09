#include "platform/common/native_hierarchy_source_checks.h"
#include "platform/common/source_check_episode.h"
#include "platform/android13/platform_string_abi.h"
#include "platform/common/dex_mapping_abi.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/abi/arm64/android-common/class_redefinition_prefix.h"
#include "platform/abi/arm64/android-common/redefiner_prefix.h"
#include "platform/abi/arm64/android-common/suspend_all.h"
#include <cstring>
#include <mutex>
#include <atomic>
#include <new>
#include <cstdlib>
namespace artdcevm::native_hierarchy_source_checks {
namespace {
using Definition=abi::arm64::android_common::ClassRedefinitionPrefix;
// Fixed first five Redefiner members in all seven ti_redefine.h declarations.
// No platform vector/string is constructed, copied or inspected here.
using DriverPrefix=abi::arm64::android_common::RedefinerPrefix;
using Mirror=abi::arm64::android11::ReleaseObjectPointer(*)(void*);
Mirror mirror=nullptr;void(*marker)(void*)=nullptr;
bool(*check_original)(void*)=nullptr;detail::Failure failure_original=nullptr;
thread_local Scope* scope=nullptr;
std::atomic<FailureObserver> failure_observer{nullptr};
std::mutex installation;std::atomic<bool> installed{false};
bool Mapped(const void* p,size_t n) {size_t extent=0;return p&&method_identity::ReadableMappingExtent(p,&extent)&&extent>=n;}
template<class T>T Read(const void* p,size_t n=0){T v;std::memcpy(&v,static_cast<const char*>(p)+n,sizeof(v));return v;}
bool Check(void* owner) {
 if(scope)return scope->Check(owner,check_original);
 // An unrelated/reentrant CheckClass may execute while another original is
 // inside its episode. Mask that episode even on the unselected proxy path.
 return detail::Invoke(owner,nullptr,nullptr,check_original,[]{return false;},[]{return false;},[](void*){});
}
void ForwardFailure(void*driver,jvmtiError code,const void*signature,const void*message){
 if(auto observe=failure_observer.load(std::memory_order_acquire))observe(driver,code);
 failure_original(driver,code,signature,message);
}
void Failure(void* driver,jvmtiError code,const void* signature,const void* message) {
 detail::Record(ForwardFailure,driver,code,signature,message);
}
}
void SetFailureObserver(FailureObserver observer)noexcept{failure_observer.store(observer,std::memory_order_release);}
bool Ready(FailureObserver expected)noexcept{return expected&&installed.load(std::memory_order_acquire)&&failure_observer.load(std::memory_order_acquire)==expected;}
void RefuseActualClass(void* owner,int code,const char* reason) noexcept {
 // An invalid source-owner invocation is an integration invariant, not an
 // invitation to synthesize a JVMTI success or directly modify driver.result.
 if(!installed.load(std::memory_order_acquire)||!failure_original||!reason||!scope||!scope->OwnsClassOwner(owner))std::abort();
 const auto d=Read<Definition>(owner);
 if(!d.driver||!Mapped(d.driver,sizeof(DriverPrefix)))std::abort();
 const void* signature=static_cast<const unsigned char*>(owner)+offsetof(Definition,class_signature);
 android13::BorrowedPlatformString message(reason);
 // Call the sole forwarded original directly: a new owner refusal must never
 // be suppressed as the original single hierarchy-comparison episode.
 ForwardFailure(d.driver,static_cast<jvmtiError>(code),signature,&message);
}
Scope::Scope(void* driver,const runnable_jni::Context& context,source_handle_roots::Service& roots,
 native_hierarchy_graph::Authority& authority,Refuse refuse):driver_(driver),context_(context),roots_(roots),authority_(authority),refuse_(refuse),previous_(scope){scope=this;}
Scope::~Scope(){if(scope!=this)std::abort();scope=previous_;}
bool Scope::OwnsClassOwner(const void* owner)const noexcept{
 if(scope!=this||!context_.env||!context_.self||!Mapped(owner,sizeof(Definition))||!Mapped(driver_,sizeof(DriverPrefix)))return false;
 const auto d=Read<Definition>(owner);const auto p=Read<DriverPrefix>(driver_);
 if(d.driver!=driver_||p.self!=context_.self||!p.environment||p.type!=0)return false;
 const auto*g=authority_.Staged();if(!g||g->inputs.empty()||g->inputs.front()>=g->nodes.size())return false;
 void*live=nullptr;return roots_.BorrowOwnedStrongGlobal(context_,g->nodes[g->inputs.front()].strong_class,&live)&&live;
}
bool Scope::Proof(void* owner,jvmtiError* expected_failure) {
 if(!installed||!refuse_||!context_.env||!context_.self||!roots_.Calibrated()||
    !Mapped(owner,sizeof(Definition))||!Mapped(driver_,sizeof(DriverPrefix)))return false;
 auto d=Read<Definition>(owner);auto driver=Read<DriverPrefix>(driver_);
 if(d.driver!=driver_||driver.self!=context_.self||driver.type!=0||driver.result!=JVMTI_ERROR_INTERNAL)return false;
 // Re-read actual native mirror and independently rooted public identities.
 const auto* graph=authority_.Staged();if(!graph||graph->inputs.empty()||!Mapped(d.dex_file,sizeof(dex_mapping_abi::Prefix)))return false;
 if(graph->inputs.front()>=graph->nodes.size())return false;
 void* runnable_root=nullptr;
 // The calibrated decoder checks current Thread/state/JNIEnv before any source
 // leaf. Merely supplying the source driver self does not establish Runnable.
 if(!roots_.BorrowOwnedStrongGlobal(context_,graph->nodes[graph->inputs.front()].strong_class,&runnable_root))return false;
 const auto prefix=Read<dex_mapping_abi::Prefix>(d.dex_file);
 if(!Mapped(prefix.begin,0x70)||std::memcmp(prefix.begin,"dex\n",4)||prefix.begin[7]||Read<uint32_t>(prefix.begin,96)!=1)return false;
 const uint32_t size=Read<uint32_t>(prefix.begin,32);
 if(size<0x70||!Mapped(prefix.begin,size)||!prefix.data_begin||!prefix.data_size||!Mapped(prefix.data_begin,prefix.data_size))return false;
 evolution::ClassSchema schema;std::string parse_error;
 if(!evolution::ReadMappedDexDeclarations({prefix.begin,size,prefix.data_begin,prefix.data_size,0},&schema,&parse_error))return false;
 void* actual=reinterpret_cast<void*>(mirror(owner).reference);
 if(!actual)return false;
 for(size_t input:graph->inputs){
  if(input>=graph->nodes.size())return false;
  auto original=graph->nodes[input].strong_class;void* rooted=nullptr;
  if(!roots_.BorrowOwnedStrongGlobal(context_,original,&rooted))return false;
  if(rooted==actual)return authority_.CheckClass(context_,original,schema,expected_failure);
 }
 return false;
}
bool Scope::Mark(void* owner) {
 // The exact original named marker must actually set the source structural
 // byte. This byte is shared in 11--17; the differing trailing policy is unused.
 marker(owner);
 return Mapped(owner,sizeof(Definition))&&Read<unsigned char>(owner,offsetof(Definition,added_methods))==1;
}
bool Scope::Check(void* owner,bool(*original)(void*)) {
 if(!original)return false;
 const void* signature=reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(owner)+offsetof(Definition,class_signature));
 try {
  jvmtiError expected=JVMTI_ERROR_NONE;
  if(!Proof(owner,&expected))return detail::Invoke(owner,nullptr,nullptr,original,[]{return false;},[]{return false;},[](void*){});
  const auto prove=[&]{jvmtiError actual=JVMTI_ERROR_NONE;return Proof(owner,&actual)&&actual==expected;};
  return detail::Invoke(owner,driver_,signature,original,prove,[&]{return Mark(owner);},
    [&](void* failed){refuse_(failed,expected,"Staged Class declarations/graph proof changed during original CheckClass");},expected);
 }catch(const std::bad_alloc&){
  // No exception crosses the original ART source frames. Ordinary permission
  // parsing is before allocation; source keeps its Holder/DEX cleanup ownership.
  if(refuse_)refuse_(owner,JVMTI_ERROR_OUT_OF_MEMORY,"Hierarchy source check exhausted native memory");
  return false;
 }
}
bool Install(HookEngine& hooks,std::string* error) {
 std::lock_guard<std::mutex> lock(installation);if(installed)return true;
 if(!hooks.Initialize(error))return false;
 auto* check=hooks.FindSymbol("libopenjdkjvmti.so","_ZN12openjdkjvmti9Redefiner17ClassRedefinition10CheckClassEv");
 auto* failure=hooks.FindSymbol("libopenjdkjvmti.so","_ZN12openjdkjvmti9Redefiner13RecordFailureE10jvmtiErrorRKNSt3__112basic_stringIcNS2_11char_traitsIcEENS2_9allocatorIcEEEESA_");
 marker=reinterpret_cast<decltype(marker)>(hooks.FindSymbol("libopenjdkjvmti.so","_ZN12openjdkjvmti9Redefiner17ClassRedefinition20RecordNewMethodAddedEv"));
 mirror=reinterpret_cast<Mirror>(hooks.FindSymbol("libopenjdkjvmti.so","_ZN12openjdkjvmti9Redefiner17ClassRedefinition14GetMirrorClassEv"));
 using namespace abi::arm64::android_common;
 auto construct=reinterpret_cast<SuspendConstruct>(hooks.FindSymbol("libart.so",kSuspendConstruct));
 auto destroy=reinterpret_cast<SuspendDestroy>(hooks.FindSymbol("libart.so",kSuspendDestroy));
 if(!check||!failure||!marker||!mirror||!construct||!destroy){if(error)*error="Missing named hierarchy source check boundary";return false;}
 SuspendAll stopped{};construct(&stopped,"artdcevm hierarchy source checks",false);
 void* first=hooks.Install(failure,reinterpret_cast<void*>(Failure),reinterpret_cast<void**>(&failure_original),error);
 if(first&&!failure_original)std::abort();
 void* second=first?hooks.Install(check,reinterpret_cast<void*>(Check),reinterpret_cast<void**>(&check_original),error):nullptr;
 if(second&&!check_original)std::abort();
 if(first&&!second){std::string rollback;if(!hooks.Remove(first,&rollback))std::abort();failure_original=nullptr;}
 destroy(&stopped);installed=first&&second;return installed;
}
}
