#pragma once
#include "api/hook_engine.h"
#include <cstddef>
#include <cstdint>
namespace artdcevm::native_source_stop {class Session;class Observation;struct NativeHooks;}
namespace artdcevm::native_hierarchy_private_ledger::detail {struct Hooks;}
namespace artdcevm::native_source_gc_publication {
inline constexpr char kRegister[]="_ZN3art11ClassLinker24RegisterExistingDexCacheENS_6ObjPtrINS_6mirror8DexCacheEEENS1_INS2_11ClassLoaderEEE";
// Called synchronously before/after the original named publication. All roots
// and the bitmap are staged before the source-stop reservation is acquired.
struct Matcher {
 void* owner=nullptr;
 bool (*match)(void*,void* driver,const void* holder,void* linker,void* cache,
               void* loader,size_t* index) noexcept=nullptr;
};

// Only the named original-call hook can construct this capability. There is no
// public integer/state or caller-created scope that grants a nested GC lease.
class Scope final {
 private:
 friend struct native_hierarchy_private_ledger::detail::Hooks;
 friend struct native_source_stop::NativeHooks;
 Scope(void* l,void* c,void* d) noexcept:linker_(l),cache_(c),loader_(d){if(enter_)enter_(*this);}
 ~Scope(){if(exit_)exit_(*this);}
 void Returned() noexcept {returned_=true;}
 static bool Start(void* h,void* t,int32_t c,int32_t k) noexcept{return start_&&start_(h,t,c,k);}
 static bool Finish(void* h,void* t,int32_t k) noexcept{return finish_&&finish_(h,t,k);}
 using Enter=void(*)(Scope&) noexcept;
 using StartGC=bool(*)(void*,void*,int32_t,int32_t) noexcept;
 using FinishGC=bool(*)(void*,void*,int32_t) noexcept;
 inline static Enter enter_=nullptr,exit_=nullptr;
 inline static StartGC start_=nullptr;inline static FinishGC finish_=nullptr;
 native_source_stop::Session* session_=nullptr;
 native_source_stop::Observation* observation_=nullptr;
 Scope* previous_=nullptr;
 void* linker_=nullptr;void* cache_=nullptr;void* loader_=nullptr;
 size_t index_=0;uint32_t starts_=0,finishes_=0;int32_t collector_=0;
 bool returned_=false,attached_=false;
};
}
