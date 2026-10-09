#include <new>
#include <cstdlib>
namespace {bool deny_native_allocation=false;}
void* operator new(std::size_t n){if(deny_native_allocation)throw std::bad_alloc();if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete[](void* p)noexcept{::operator delete(p);}
#define main SourceRootsMain
#include "source_handle_roots_test.cc"
#undef main
#include "platform/common/native_hierarchy_allocator.h"
namespace {
native_hierarchy_allocator::Service* allocation_service=nullptr;
source_handle_roots::Service* allocation_roots=nullptr;
bool monitor_held=false,allocate_oom=false,wrong_return=false,hide_capture=false,link_failure=false,deny_after_capture=false,move_temporary=false;
void* moved_temporary=nullptr;
size_t allocation_calls=0,retire_calls=0,source_failures=0;
void* monitor_object=nullptr;int holds_id;
abi::arm64::android11::ReleaseObjectPointer Enter(void* self,abi::arm64::android11::ReleaseObjectPointer p,bool attempt){assert(self==thread.data()&&!attempt&&!monitor_held);monitor_held=true;monitor_object=reinterpret_cast<void*>(p.reference);return p;}
bool Exit(void* self,abi::arm64::android11::ReleaseObjectPointer p){assert(self==thread.data()&&monitor_held&&p.reference==uintptr_t(monitor_object));monitor_held=false;monitor_object=nullptr;return true;}
jmethodID MonitorMethod(JNIEnv* e,jclass c,const char* n,const char* s){return !std::strcmp(n,"holdsLock")?reinterpret_cast<jmethodID>(&holds_id):StaticMethodId(e,c,n,s);}
jboolean Holds(JNIEnv*,jclass,jmethodID id,va_list args){Jni();assert(id==reinterpret_cast<jmethodID>(&holds_id));auto owner=va_arg(args,jobject);return monitor_held&&uintptr_t(owner)==uintptr_t(monitor_object);}
class AllocationHooks:public Hooks {public:void* FindSymbol(const char* lib,const char* n)override{if(std::strstr(n,"MonitorEnter"))return reinterpret_cast<void*>(Enter);if(std::strstr(n,"MonitorExit"))return reinterpret_cast<void*>(Exit);return Hooks::FindSymbol(lib,n);}};
abi::arm64::android11::ReleaseObjectPointer FindOriginal(void* linker_arg,void* self,const char* n,void* loader){assert(linker_arg==&linker&&self==thread.data()&&!Read32(loader)&&!std::strcmp(n,"LFixture;"));return {uintptr_t(old_class)};}
void LoadOriginal(void* receiver,void* self,const void*,const void*,const void*){assert(receiver==&linker&&self==thread.data());}
bool EnsureOriginal(const void*){return !allocate_oom;}
void SourceFailure(void*,int error,const char*)noexcept{assert(error==JVMTI_ERROR_OUT_OF_MEMORY);++source_failures;}
void RetireOriginal(void* object,uint8_t status){assert(monitor_held&&monitor_object==object&&(object==new_class||object==moved_temporary)&&status==1);Put32(object,offsetof(class_metadata::Class17,status),uint32_t(status)<<28);++retire_calls;}
abi::arm64::android11::ReleaseObjectPointer AllocateOriginal(void*,void* old,void* parent,void* cache,uint16_t index){
 ++allocation_calls;assert(Read32(old)==uintptr_t(old_class)&&Read32(parent)==uintptr_t(parent_class)&&index==0);
 Put32(new_class,offsetof(class_metadata::Class17,dex_cache),Read32(cache));
 Put32(new_class,offsetof(class_metadata::Class17,status),15u<<28);
 auto source=allocation_roots->CreateScope(1,nullptr);assert(source);source->Set(0,new_class);
 allocation_service->LoadClass(&linker,thread.data(),nullptr,nullptr,source->Slot(0));
 if(link_failure)return {0};
 if(move_temporary){
  moved_temporary=heap.New(sizeof(class_metadata::Class17));std::memcpy(moved_temporary,new_class,sizeof(class_metadata::Class17));
  MoveRoots mover;mover.old_value=uint32_t(uintptr_t(new_class));mover.next_value=uint32_t(uintptr_t(moved_temporary));OriginalRoots(thread.data(),&mover,0);
 }
 if(deny_after_capture)deny_native_allocation=true;
 if(!hide_capture)assert(allocation_service->EnsureInstanceIds(source->Slot(0))==!allocate_oom);
 return {allocate_oom?0u:uintptr_t(wrong_return?old_class:source->Get(0))};
}
bool Probe(const runnable_jni::Context& c,modern_field_metadata::FieldMetadata& metadata,source_handle_roots::Service& roots,modern_field_storage::Service&){
 AllocationHooks hooks;native_hierarchy_allocator::Service owner;std::string error;runtime_services::Snapshot runtime;runtime.receivers.class_linker=&linker;
 assert(native_hierarchy_allocator::Service::Bind(hooks,metadata,roots,runtime,&owner,&error));
 native_hierarchy_allocator::Functions funcs{AllocateOriginal,FindOriginal,EnsureOriginal,SourceFailure,RetireOriginal,LoadOriginal};
 assert(owner.SetOriginals(funcs,&error));allocation_service=&owner;allocation_roots=&roots;
 table.GetStaticMethodID=MonitorMethod;table.CallStaticBooleanMethodV=Holds;
 auto original=roots.CreateScope(3,&error);assert(original);original->Set(0,old_class);original->Set(1,parent_class);
 auto* cache=heap.New(32);original->Set(2,cache);
 // Missing independently observed monitor grants nothing even with all names.
 native_hierarchy_graph::Graph graph;std::vector<native_hierarchy_graph::Input> inputs;
 native_hierarchy_graph::Node object_node;object_node.klass=parent_class;object_node.descriptor="Ljava/lang/Object;";
 native_hierarchy_graph::Node fixture_node;fixture_node.klass=old_class;fixture_node.strong_class=reinterpret_cast<jclass>(old_class);fixture_node.descriptor="LFixture;";fixture_node.parent=0;fixture_node.proposed_parent=0;fixture_node.affected=true;
 graph.nodes={object_node,fixture_node};graph.inputs={1};
 native_hierarchy_graph::Input input;input.strong_original=reinterpret_cast<jclass>(old_class);input.before.descriptor="LFixture;";input.before.superclass="Ljava/lang/Object;";input.after=input.before;inputs.push_back(input);
 assert(!owner.VerifyStaged(graph,inputs,&error));
 assert(owner.CalibrateRetirement(c,reinterpret_cast<jobject>(old_class),&error)&&!monitor_held);
 assert(!owner.VerifyStaged(graph,inputs,&error));
 const auto before=allocation_calls;
 assert(owner.ObserveOriginal(c,reinterpret_cast<jobject>(old_class),&linker,original->Slot(0),original->Slot(1),original->Slot(2),0,&error)==new_class);
 assert(allocation_calls==before+1&&!monitor_held&&retire_calls==0);
 // A named original operation plus actual source root/capture/FindClass and
 // public holdsLock proofs completes this source boundary observation.
 assert(owner.VerifyStaged(graph,inputs,&error));
 allocate_oom=true;
 assert(!owner.ObserveOriginal(c,reinterpret_cast<jobject>(old_class),&linker,original->Slot(0),original->Slot(1),original->Slot(2),0,&error));
 assert(retire_calls==1&&!monitor_held&&Read32(new_class,offsetof(class_metadata::Class17,status))==(1u<<28));
 allocate_oom=false;link_failure=true;
 assert(!owner.ObserveOriginal(c,reinterpret_cast<jobject>(old_class),&linker,original->Slot(0),original->Slot(1),original->Slot(2),0,&error));
 assert(retire_calls==2&&!monitor_held);link_failure=false;
 allocate_oom=true;move_temporary=true;
 assert(!owner.ObserveOriginal(c,reinterpret_cast<jobject>(old_class),&linker,original->Slot(0),original->Slot(1),original->Slot(2),0,&error));
 assert(retire_calls==3&&!monitor_held&&Read32(moved_temporary,offsetof(class_metadata::Class17,status))==(1u<<28));move_temporary=false;
 allocate_oom=true;deny_after_capture=true;
 assert(!owner.ObserveOriginal(c,reinterpret_cast<jobject>(old_class),&linker,original->Slot(0),original->Slot(1),original->Slot(2),0,&error));
 deny_native_allocation=false;deny_after_capture=false;move_temporary=false;
 assert(retire_calls==4&&!monitor_held);
 const auto before_native_oom=allocation_calls;deny_native_allocation=true;
 assert(!owner.ObserveOriginal(c,reinterpret_cast<jobject>(old_class),&linker,original->Slot(0),original->Slot(1),original->Slot(2),0,&error));
 deny_native_allocation=false;assert(allocation_calls==before_native_oom&&source_failures==1);
 allocate_oom=false;wrong_return=true;
 // Observation preserves the original source's result, but never publishes a
 // graph capability from a mismatched return or missing early-capture hook.
 assert(owner.SetOriginals(funcs,&error));
 assert(owner.ObserveOriginal(c,reinterpret_cast<jobject>(old_class),&linker,original->Slot(0),original->Slot(1),original->Slot(2),0,&error)==old_class);
 assert(!owner.VerifyStaged(graph,inputs,&error));wrong_return=false;hide_capture=true;
 assert(owner.ObserveOriginal(c,reinterpret_cast<jobject>(old_class),&linker,original->Slot(0),original->Slot(1),original->Slot(2),0,&error)==new_class);
 assert(!owner.VerifyStaged(graph,inputs,&error));hide_capture=false;
 assert(source_failures==1);table.GetStaticMethodID=StaticMethodId;
 return true;
}
}
int main(){source_probe=Probe;const auto result=SourceRootsMain();std::cout<<"Actual source allocator observation: one original call, independent public monitor and named FindClass proof, pre-Link and pre-IDs capture, OOM retirement, native bad_alloc converted without crossing ART frames PASS\n";return result;}
