#pragma once
#include <atomic>
#include <cstdint>
#include "platform/common/runtime_services.h"
namespace artdcevm::frameproof {
// Fixed jit.h and jit_code_cache.h: Android11 done has bool osr;
// Android12/13 has CompilationKind (source enum int); Android14--17 has two args.
inline constexpr char kEnqueue[]="_ZN3art3jit3Jit23MaybeEnqueueCompilationEPNS_9ArtMethodEPNS_6ThreadE";
inline constexpr char kLegacyEnqueue[]="_ZN3art3jit3Jit18MaybeCompileMethodEPNS_6ThreadEPNS_9ArtMethodEjjb";
inline constexpr char kDone11[]="_ZN3art3jit12JitCodeCache13DoneCompilingEPNS_9ArtMethodEPNS_6ThreadEb";
inline constexpr char kDone12[]="_ZN3art3jit12JitCodeCache13DoneCompilingEPNS_9ArtMethodEPNS_6ThreadENS_15CompilationKindE";
inline constexpr char kDone14[]="_ZN3art3jit12JitCodeCache13DoneCompilingEPNS_9ArtMethodEPNS_6ThreadE";
inline constexpr char kHeapCollect[]="_ZN3art2gc4Heap14CollectGarbageEbNS0_7GcCauseE";
inline constexpr char kHeapTotalMemory[]="_ZNK3art2gc4Heap14GetTotalMemoryEv";
using HeapTotalMemory=size_t (*)(const void*);
using Enqueue=void (*)(void*,void*,void*);
using LegacyEnqueue=bool (*)(void*,void*,void*,uint32_t,uint32_t,bool);
using Done11=void (*)(void*,void*,void*,bool);
using Done12=void (*)(void*,void*,void*,int);
using Done14=void (*)(void*,void*,void*);
using HeapCollect=void (*)(void*,bool,int);
struct Observation {
 std::atomic<void*> expected{nullptr},jit{nullptr},cache{nullptr},heap{nullptr};
 std::atomic<bool> gc_active{false},conflict{false};
 std::atomic<unsigned> enqueues{0},completions{0},collections{0},memory_queries{0};
 void Observe(std::atomic<void*>& slot,void* receiver) {
  void* empty=nullptr;
  if(!receiver||(!slot.compare_exchange_strong(empty,receiver)&&empty!=receiver))conflict=true;
 }
 void Enqueued(void* receiver,void* method) {
  if(method&&method==expected.load()){Observe(jit,receiver);++enqueues;}
 }
 void Completed(void* receiver,void* method) {
  if(method&&method==expected.load()){Observe(cache,receiver);++completions;}
 }
 void Collected(void* receiver) {if(gc_active.load()){Observe(heap,receiver);++collections;}}
 void MemoryQueried(void* receiver){Observe(heap,receiver);++memory_queries;}
 bool Ready()const {return expected.load()&&jit.load()&&cache.load()&&heap.load()&&!conflict.load()&&enqueues.load()>0&&completions.load()>0&&memory_queries.load()>0;}
 bool Complete()const{return Ready()&&memory_queries.load()>=2;}
};
// Matching every independent native receiver is mandatory. No field is used as
// a receiver to obtain its own observation, and no API/build fingerprint is read.
inline bool Same(const runtime_services::View& a,const runtime_services::View& b) {
 return a.class_linker&&a.jni_id_manager&&a.jit&&a.jit_code_cache&&a.thread_list&&a.heap&&a.java_vm&&
 a.class_linker==b.class_linker&&a.jni_id_manager==b.jni_id_manager&&a.jit==b.jit&&a.jit_code_cache==b.jit_code_cache&&a.thread_list==b.thread_list&&a.heap==b.heap&&a.java_vm==b.java_vm;
}
}
