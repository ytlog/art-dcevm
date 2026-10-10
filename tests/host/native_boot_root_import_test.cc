#define main SourceRootsBaselineMain
#include "import_source_roots.inc"
#undef main
#include "platform/common/native_boot_method_catalog.h"

bool import_snapshot_failure=false;
namespace {
std::vector<jclass> import_classes;
bool import_active=false;
size_t import_decodes=0;
uint64_t import_first_queries=0;
int import_jni_before=0;
void ImportDecodeObservation(){
 if(!import_active)return;
 assert(jni_calls==import_jni_before);
 const auto queries=method_identity::MappingOsQueries();
 if(!import_decodes)import_first_queries=queries;
 else assert(queries==import_first_queries);
 ++import_decodes;
}
jvmtiError ImportCapabilities(jvmtiEnv*,jvmtiCapabilities*out){*out={};out->can_get_bytecodes=1;return JVMTI_ERROR_NONE;}
jvmtiError ImportLoaded(jvmtiEnv*,jint*n,jclass**out){*n=jint(import_classes.size());*out=static_cast<jclass*>(std::malloc(sizeof(jclass)*import_classes.size()));std::copy(import_classes.begin(),import_classes.end(),*out);return JVMTI_ERROR_NONE;}
jvmtiError ImportLoader(jvmtiEnv*,jclass,jobject*out){*out=nullptr;return JVMTI_ERROR_NONE;}
jvmtiError ImportStatus(jvmtiEnv*,jclass,jint*out){*out=JVMTI_CLASS_STATUS_PREPARED;return JVMTI_ERROR_NONE;}
jvmtiError ImportSignature(jvmtiEnv*,jclass klass,char**out,char**){const auto i=std::find(import_classes.begin(),import_classes.end(),klass)-import_classes.begin();const auto text="Lboot/Import"+std::to_string(i)+";";*out=strdup(text.c_str());return JVMTI_ERROR_NONE;}
jvmtiError ImportMethods(jvmtiEnv*,jclass,jint*n,jmethodID**out){*n=0;*out=nullptr;return JVMTI_ERROR_NONE;}
jvmtiError ImportDeallocate(jvmtiEnv*,unsigned char*p){std::free(p);return JVMTI_ERROR_NONE;}
void* ImportMethod(void*,jmethodID){std::abort();}
void* ImportReflected(const abi::common::RunnableJniScope&,jobject){std::abort();}
void* ImportDeclaring(const void*,void*)noexcept{std::abort();}
bool CheckImport(const runnable_jni::Context&context,modern_field_metadata::FieldMetadata&metadata,source_handle_roots::Service&source,modern_field_storage::Service&){
 std::string calibration_error;
 assert(source.ObserveAllocatorHandle(context,reinterpret_cast<jobject>(old_class),public_slot,&calibration_error)&&source.Calibrated());
 constexpr size_t count=3000;
 import_classes.reserve(count);
 for(size_t i=0;i<count;++i){auto*klass=heap.New(160);std::memcpy(klass,old_class,160);import_classes.push_back(reinterpret_cast<jclass>(klass));}
 jvmtiInterface_1 funcs{};jvmtiEnv ti{};ti.functions=&funcs;
 funcs.GetCapabilities=ImportCapabilities;funcs.GetLoadedClasses=ImportLoaded;funcs.GetClassLoader=ImportLoader;funcs.GetClassStatus=ImportStatus;funcs.GetClassSignature=ImportSignature;funcs.GetClassMethods=ImportMethods;funcs.Deallocate=ImportDeallocate;
 native_boot_method_catalog::Catalog catalog;std::string error;
 assert(catalog.Prepare(context.env,&ti,{&metadata,ImportMethod,ImportReflected},&error));
 assert(catalog.Complete()&&catalog.Roots().size()==count);
 uint64_t baseline_queries=0;
 {
  source_handle_roots::Pool baseline(source);
  const auto before=method_identity::MappingOsQueries();const auto jni_before=jni_calls;
  // The previous real import loop, without a mapping view. Same actual
  // retained globals, named decoder, context and original native scope writes.
  for(auto strong:catalog.Roots()){void*klass=nullptr;assert(source.BorrowOwnedStrongGlobal(context,strong,&klass)&&klass&&baseline.Add(klass,&error));}
  baseline_queries=method_identity::MappingOsQueries()-before;
  assert(baseline_queries==3*count+2*((count+31)/32)&&jni_calls==jni_before);
 }
 import_decoder_observer=ImportDecodeObservation;
 // Force only the real mapping snapshot's platform-query failure boundary.
 // Import must refuse before any decoder, root slot or published binding.
 {
  source_handle_roots::Pool denied(source);native_boot_method_catalog::Rooted rejected;
  const auto top=Top();import_snapshot_failure=true;import_decodes=0;import_active=true;import_jni_before=jni_calls;
  assert(!rejected.Import(catalog,context,source,denied,&metadata,ImportDeclaring,&error));
  import_active=false;import_snapshot_failure=false;
  assert(error=="Boot root import mapping snapshot is incomplete"&&import_decodes==0&&Top()==top);
  size_t slots=0;assert(denied.VisitUsedSlots([&](void*){++slots;return true;}));assert(!slots);
 }
 // A foreign original thread/env cannot gain permission from readable maps.
 {
  source_handle_roots::Pool denied(source);native_boot_method_catalog::Rooted rejected;
  auto wrong=context;wrong.self=foreign_thread.data();const auto top=Top();
  assert(!rejected.Import(catalog,wrong,source,denied,&metadata,ImportDeclaring,&error)&&Top()==top);
  wrong=context;wrong.vm=&linker;
  assert(!rejected.Import(catalog,wrong,source,denied,&metadata,ImportDeclaring,&error)&&Top()==top);
 }
 const auto original_top=Top();
 {
  source_handle_roots::Pool pool(source);
  auto* anchor_slot=pool.Add(old_class,&error);assert(anchor_slot);const auto anchor=Top();
  {
   auto temporary=source.CreateScope(1,&error);assert(temporary);temporary->Set(0,old_class);const auto temporary_top=Top();
   native_boot_method_catalog::Rooted rooted;
   const auto before=method_identity::MappingOsQueries();import_decodes=0;import_active=true;import_jni_before=jni_calls;
   assert(rooted.Import(catalog,context,source,pool,&metadata,ImportDeclaring,&error));
   import_active=false;
   const auto delta=method_identity::MappingOsQueries()-before;
   assert(import_decodes==count&&method_identity::MappingOsQueries()==import_first_queries&&jni_calls==import_jni_before&&Top()==temporary_top);
#ifndef __APPLE__
   assert(delta==1);
#endif
   std::vector<void*> slots;assert(pool.VisitUsedSlots([&](void*slot){slots.push_back(slot);return true;}));assert(slots.size()==count+1);
   for(size_t i=0;i<count;++i){void*value=nullptr;assert(pool.Read(slots[i+1],&value)&&value==import_classes[i]);}
   auto* relocated=heap.New(160);std::memcpy(relocated,import_classes.front(),160);
   MoveRoots move;move.old_value=uint32_t(uintptr_t(import_classes.front()));move.next_value=uint32_t(uintptr_t(relocated));OriginalRoots(thread.data(),&move,0);
   void*value=nullptr;assert(pool.Read(slots[1],&value)&&value==relocated&&Top()==temporary_top&&temporary->Get(0)==old_class);
   assert(!rooted.Import(catalog,context,source,pool,&metadata,ImportDeclaring,&error));
   std::cout<<"Actual Rooted::Import roots="<<count<<" previous-loop OSQueries="<<baseline_queries<<" fresh-capture OSQueries="<<delta<<" post-first-decode OSQueries=0 JNI=0, source identity negatives, Ready refusal, collector update and temporary-scope LIFO PASS\n";
  }
  assert(Top()==anchor);
 }
 assert(Top()==original_top);
 import_decoder_observer=nullptr;catalog.Clear(context.env);import_classes.clear();return true;
}
}
int main(){source_probe=CheckImport;return SourceRootsBaselineMain();}
