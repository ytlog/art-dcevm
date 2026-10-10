#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "allocation_owner_fixture.inc"
#pragma clang diagnostic pop
#include "platform/common/native_production_schema_capabilities.h"
#include "platform/common/native_schema_run_state.h"
#include "platform/common/dex_mapping_abi.h"
#include "platform/abi/arm64/common/redefiner_prefix.h"
#include "platform/abi/arm64/common/class_redefinition_prefix.h"
// Only the inactive production RunState dispatch seam is doubled; all owner,
// root, allocation, lookup, registry, monitor and scope code below is linked.
namespace artdcevm::native_schema_run_state {
RunState* RunState::Current()noexcept{return nullptr;}
abi::arm64::android11::ReleaseObjectPointer RunState::FindOriginal(void*,void*,const char*,void*){std::abort();}
}
namespace {
using namespace native_production_schema_capabilities;
abi::arm64::android_common::HierarchyAllocator allocator_proxy=nullptr;
void* registered_cache=nullptr;
dex_mapping_abi::Prefix public_dex{};
bool wrong_registry=false,wrong_lookup=false;
bool lookup_length=std::getenv("SOURCE_LOOKUP_LENGTH")!=nullptr;
jint Vm(JNIEnv*,JavaVM** output){Jni();*output=reinterpret_cast<JavaVM*>(&runtime);return JNI_OK;}
abi::arm64::android11::ReleaseObjectPointer Lookup(void*l,void*self,const char*d,const void*loader){
 assert(l==&linker&&self==thread.data()&&!std::strcmp(d,"LFixture;")&&!Read32(loader));
 return {uintptr_t(wrong_lookup?parent_class:old_class)};
}
abi::arm64::android11::ReleaseObjectPointer LengthLookup(void*l,void*self,const char*d,size_t length,const void*loader){
 assert(length==std::strlen(d));return Lookup(l,self,d,loader);
}
bool Registered(void*l,void*self,const void*d){assert(l==&linker&&self==thread.data()&&d==&public_dex);return !wrong_registry;}
abi::arm64::android11::ReleaseObjectPointer Cache(void*l,void*self,const void*d){assert(l==&linker&&self==thread.data()&&d==&public_dex);return {uintptr_t(registered_cache)};}
bool Finish(void*,transaction_root::Holder*,void*){return true;}
bool Check(void*){return true;}
void Record(void*,jvmtiError,const void*,const void*){}
void Marker(void*){}
void PublishCache(void*,abi::arm64::android11::ReleaseObjectPointer,abi::arm64::android11::ReleaseObjectPointer){}
void ObserveFailure(void*,jvmtiError)noexcept{}
void SignedRoots(void*self,abi::arm64::android_common::HandleRootVisitor*visitor,int32_t id){assert(id==0);OriginalRoots(self,visitor,uint32_t{0});}
// Observe the public/private monitor episode independently of production
// readiness: exact fresh Object root, unlocked -> held -> unlocked, one pair.
size_t monitor_episode_calls=0,monitor_completed=0,monitor_enters=0,monitor_exits=0;
jobject OwnerPrivateNew(JNIEnv* e,jclass type,jmethodID id,va_list arguments){
 assert(!monitor_held);monitor_episode_calls=0;
 auto object=PrivateNew(e,type,id,arguments);
 assert(object==private_monitor&&object!=reinterpret_cast<jobject>(old_class));
 return object;
}
jboolean OwnerHolds(JNIEnv* e,jclass type,jmethodID id,va_list arguments){
 va_list copied;va_copy(copied,arguments);auto object=va_arg(copied,jobject);va_end(copied);
 assert(object==private_monitor&&object!=reinterpret_cast<jobject>(old_class));
 const auto result=Holds(e,type,id,arguments);
 if(private_monitor_locked){assert(monitor_episode_calls==0&&result);++monitor_episode_calls;return result;}
 assert(monitor_episode_calls<3);
 assert(bool(result)==(monitor_episode_calls==1));
 ++monitor_episode_calls;if(monitor_episode_calls==3)++monitor_completed;
 return result;
}
abi::arm64::android11::ReleaseObjectPointer OwnerEnter(void* self,abi::arm64::android11::ReleaseObjectPointer object,bool attempt){
 assert(object.reference==uintptr_t(private_monitor)&&object.reference!=uintptr_t(old_class));
 assert(monitor_episode_calls==1&&!private_monitor_locked);++monitor_enters;
 return Enter(self,object,attempt);
}
bool OwnerExit(void* self,abi::arm64::android11::ReleaseObjectPointer object){
 assert(object.reference==uintptr_t(private_monitor)&&monitor_episode_calls==2);++monitor_exits;
 return Exit(self,object);
}
class OwnerHooks final:public AllocationHooks {
 public:
 void* FindSymbol(const char* lib,const char* n)override{
  if(std::strstr(n,"MonitorEnter"))return reinterpret_cast<void*>(OwnerEnter);
  if(std::strstr(n,"MonitorExit"))return reinterpret_cast<void*>(OwnerExit);
  if(!std::strcmp(n,abi::arm64::android_common::kHandleVisitRoots))return nullptr;
  if(!std::strcmp(n,abi::arm64::android_common::kHandleVisitRootsSigned))return reinterpret_cast<void*>(SignedRoots);
  if(std::strstr(n,"GetMirrorClass"))return reinterpret_cast<void*>(AllocationMirror);
  if(std::strstr(n,"AllocateNewClassObject"))return reinterpret_cast<void*>(AllocateOriginal);
  if(std::strstr(n,"FinishNewClassAllocations"))return reinterpret_cast<void*>(Finish);
  if(std::strstr(n,"ClassLinker9LoadClass"))return reinterpret_cast<void*>(LoadOriginal);
  if(std::strstr(n,"EnsureInstanceFieldIds"))return reinterpret_cast<void*>(EnsureOriginal);
  if(std::strstr(n,"Class15SetStatusLocked"))return reinterpret_cast<void*>(RetireOriginal);
  if(std::strstr(n,"ClassLinker9FindClassEPNS_6ThreadEPKcNS_6Handle"))return lookup_length?nullptr:reinterpret_cast<void*>(Lookup);
  if(std::strstr(n,"ClassLinker9FindClassEPNS_6ThreadEPKcmNS_6Handle"))return lookup_length?reinterpret_cast<void*>(LengthLookup):nullptr;
  if(std::strstr(n,"ComputeModifiedUtf8Hash"))std::abort();
  if(std::strstr(n,"IsDexFileRegistered"))return reinterpret_cast<void*>(Registered);
  if(std::strstr(n,"FindDexCache"))return reinterpret_cast<void*>(Cache);
  if(std::strstr(n,"RegisterExistingDexCache"))return reinterpret_cast<void*>(PublishCache);
  if(std::strstr(n,"ClassRedefinition10CheckClass"))return reinterpret_cast<void*>(Check);
  if(std::strstr(n,"Redefiner13RecordFailure"))return reinterpret_cast<void*>(Record);
  if(std::strstr(n,"RecordNewMethodAdded"))return reinterpret_cast<void*>(Marker);
  return AllocationHooks::FindSymbol(lib,n);
 }
 void* Install(void* target,void* proxy,void** original,std::string*)override{
  assert(stopped==1);*original=target;
  if(target==reinterpret_cast<void*>(AllocateOriginal))allocator_proxy=reinterpret_cast<decltype(allocator_proxy)>(proxy);
  return target;
 }
 bool Remove(void*,std::string*)override{return true;}
};
bool OwnerProbe(const runnable_jni::Context& c,modern_field_metadata::FieldMetadata& metadata,source_handle_roots::Service&,modern_field_storage::Service& storage){
 OwnerHooks hooks;Owner owner;std::string error;
 table.GetJavaVM=Vm;table.GetStaticMethodID=MonitorMethod;table.CallStaticBooleanMethodV=OwnerHolds;table.NewObjectV=OwnerPrivateNew;
 runtime_services::Snapshot observed;observed.runtime=&runtime;observed.receivers.class_linker=&linker;observed.receivers.java_vm=&runtime;
 auto foreign=c;foreign.vm=&linker;
 assert(!owner.Prepare(hooks,storage,metadata,observed,foreign,&error)&&!owner.Prepared());
 auto foreign_self=c;foreign_self.self=foreign_thread.data();
 assert(!owner.Prepare(hooks,storage,metadata,observed,foreign_self,&error)&&!owner.Prepared());
 Put32(thread.data(),0,92u<<24);
 auto saved_env_self=env.source_self;env.source_self=foreign_thread.data();
 assert(!owner.Prepare(hooks,storage,metadata,observed,c,&error)&&!owner.Prepared());env.source_self=saved_env_self;
 // Exact independently observed JNIEnvExt identity replaces obsolete TLS
 // self/env assumptions. Root scope chain and named visitor remain mandatory.
 void* saved_tls_self=nullptr;void* saved_tls_env=nullptr;
 std::memcpy(&saved_tls_self,thread.data()+216,8);std::memcpy(&saved_tls_env,thread.data()+200,8);
 void* absent=nullptr;std::memcpy(thread.data()+216,&absent,8);std::memcpy(thread.data()+200,&absent,8);
 assert(owner.Prepare(hooks,storage,metadata,observed,c,&error)&&owner.Prepared()&&!owner.Ready());
 native_schema_run_state::Dependencies dependencies;
 assert(!owner.BindDependencies(&dependencies,&error)&&!dependencies.roots&&!dependencies.allocator);
 assert(owner.Prepare(hooks,storage,metadata,observed,c,&error));
 auto different=observed;different.runtime=&linker;assert(!owner.Prepare(hooks,storage,metadata,different,c,&error));
 assert(!owner.InstallHooks(nullptr,&error));
 assert(owner.InstallHooks(ObserveFailure,&error)&&owner.HooksInstalled()&&!stopped&&!owner.Ready());
 assert(owner.InstallHooks(ObserveFailure,&error));
 Put32(thread.data(),0,0);
 auto fixture=reinterpret_cast<jobject>(old_class);
 abi::arm64::android_common::RedefinerPrefix driver{};driver.environment=&runtime;driver.runtime=&runtime;driver.self=c.self;driver.result=JVMTI_ERROR_INTERNAL;
 abi::arm64::android_common::ClassRedefinitionPrefix class_owner{};class_owner.driver=&driver;
 registered_cache=heap.New(32);uint64_t file=uintptr_t(&public_dex);std::memcpy(static_cast<char*>(registered_cache)+16,&file,8);
 Put32(old_class,offsetof(class_metadata::Class17,dex_cache),uint32_t(uintptr_t(registered_cache)));
 allocation_service=&owner.Allocator();allocation_roots=&owner.Roots();
 assert(!owner.Begin(&driver,foreign,fixture,&error));
 assert(!owner.Begin(&driver,c,nullptr,&error));
 assert(owner.Begin(&driver,c,fixture,&error));
 assert(!owner.Begin(&driver,c,fixture,&error));
 assert(!owner.End(&class_owner,true,&error));
 assert(!owner.Ready()&&!owner.Roots().Calibrated());
 // The original source owns these handles; first operation independently
 // observes and calibrates the service's visitor, then forwards once.
 alignas(8) unsigned char original[32]{};OriginalHeader header{Top(),3};std::memcpy(original,&header,sizeof(header));
 if(sized_scope)Put32(original,12,3);
 Put32(original,OriginalReferences(),uint32_t(uintptr_t(old_class)));Put32(original,OriginalReferences()+4,uint32_t(uintptr_t(parent_class)));Put32(original,OriginalReferences()+8,uint32_t(uintptr_t(registered_cache)));Publish(original);
 const auto calls=allocation_calls;
 assert(allocator_proxy(&class_owner,original+OriginalReferences(),original+OriginalReferences()+4,original+OriginalReferences()+8,0).reference==uintptr_t(new_class));
 assert(allocation_calls==calls+1&&owner.Roots().Calibrated()&&!owner.Allocator().Calibrated());
 Publish(header.previous);
 assert(owner.End(&driver,true,&error)&&!owner.Ready());
 assert(!std::strcmp(owner.Diagnostic(),"requires-second-ordinary-allocation-proof"));
 assert(!native_hierarchy_session::CalibrationScope::Current());
 void* unchanged=parent_class;
 assert(!owner.Roots().BorrowOwnedStrongGlobal(foreign,fixture,&unchanged)&&unchanged==parent_class);
 env.source_self=foreign_thread.data();
 assert(!owner.Roots().BorrowOwnedStrongGlobal(c,fixture,&unchanged)&&unchanged==parent_class);
 env.source_self=saved_env_self;
 // A silent native-Dex/monitor failure must identify its actual calibration
 // stage rather than reusing an earlier generic readiness diagnostic.
 assert(owner.Begin(&driver,c,fixture,&error));
 uint64_t missing_file=0;std::memcpy(static_cast<char*>(registered_cache)+16,&missing_file,8);
 error="stale readiness error";assert(!owner.End(&driver,true,&error)&&!owner.Ready());
 assert(error=="DEX registry public DexCache has no readable fixed native DexFile");
 std::memcpy(static_cast<char*>(registered_cache)+16,&file,8);
 assert(owner.Begin(&driver,c,fixture,&error));private_monitor_locked=true;
 const auto refused_enters=monitor_enters;
 error="stale readiness error";assert(!owner.End(&driver,true,&error)&&!owner.Ready());
 assert(error=="Monitor proof fresh private Object was already locked");
 assert(monitor_enters==refused_enters&&!monitor_held&&private_globals==private_deleted);
 private_monitor_locked=false;
 // Wrong registry or lookup cannot promote even a successful source episode.
 assert(owner.Begin(&driver,c,fixture,&error));wrong_registry=true;
 assert(!owner.End(&driver,true,&error)&&!owner.Ready());wrong_registry=false;
 assert(owner.Begin(&driver,c,fixture,&error));wrong_lookup=true;
 assert(!owner.End(&driver,true,&error)&&!owner.Ready());wrong_lookup=false;
 // Re-establish independent lookup, then second actual original allocation
 // proves pre-Link capture, linked return, selected cache and exact lookup.
 assert(owner.Begin(&driver,c,fixture,&error));assert(owner.End(&driver,true,&error));
 assert(owner.Begin(&driver,c,fixture,&error));Publish(original);
 assert(allocator_proxy(&class_owner,original+OriginalReferences(),original+OriginalReferences()+4,original+OriginalReferences()+8,0).reference==uintptr_t(new_class));
 Publish(header.previous);
 assert(owner.End(&driver,true,&error)&&owner.Ready());
 assert(owner.BindDependencies(&dependencies,&error)&&dependencies.roots==&owner.Roots()&&dependencies.allocator==&owner.Allocator()&&dependencies.lookup==&owner.Lookup()&&dependencies.dex_registry==&owner.DexRegistry());
 source_handle_roots::Service foreign_roots;dependencies.roots=&foreign_roots;
 assert(!owner.BindDependencies(&dependencies,&error)&&dependencies.roots==&foreign_roots);
 assert(!native_hierarchy_session::CalibrationScope::Current()&&!monitor_held&&!stopped);
 assert(owner.Begin(&driver,c,fixture,&error));wrong_registry=true;
 assert(!owner.End(&driver,true,&error)&&!owner.Ready());wrong_registry=false;
 assert(owner.Begin(&driver,c,fixture,&error));assert(owner.End(&driver,true,&error)&&owner.Ready());
 assert(!owner.End(&driver,true,&error));
 assert(original_class_locked&&monitor_completed>0&&monitor_enters==monitor_completed&&monitor_exits==monitor_completed);
 assert(private_created==private_globals&&private_globals==private_deleted);
 std::memcpy(thread.data()+216,&saved_tls_self,8);std::memcpy(thread.data()+200,&saved_tls_env,8);
 return true;
}
}
int main(){source_probe=OwnerProbe;const auto result=SourceRootsMain();std::cout<<"Actual production capability owner: exact Runtime/VM, named original allocation twice, registry/lookup/monitor oracles, unique scope release, negative provenance/registry/lookup and no installation readiness PASS\n";return result;}
