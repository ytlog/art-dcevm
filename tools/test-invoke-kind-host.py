#!/usr/bin/env python3
"""Exercise production invocation guards with fake named ART services."""
from pathlib import Path
import runpy
import shutil
import subprocess
import tempfile
import sys
from evidence_paths import new_evidence_directory

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "platform/common/invoke_kind.h"
#include "platform/abi/arm64/common/runtime_objects.h"
#include "platform/symbols/invoke_kind_symbols.h"
#include "platform/symbols/structural_redefine_symbols.h"
#include "platform/symbols/runtime_thread_symbols.h"
#include "platform/symbols/interface_redefinition_symbols.h"
#include "platform/common/runtime_roots.h"
#include "platform/abi/arm64/common/redefinition.h"
#include <cstring>
#include <sys/mman.h>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace artdcevm;
using abi::arm64::Handle;
abi::arm64::ArtMethodPrefix method{};
int mode, exceptions = 0, original_calls = 0;
int hook_attempts = 0;
bool hook_failure = false;
void* installed_replacement = nullptr;
abi::arm64::ArtField kind_field{}, target_field{}, type_field{}, params_field{}, refs_field{}, delegate_field{}, receiver_field{};
abi::arm64::ArtField* FindField(void*, std::string_view name, std::string_view) {
  kind_field.offset = 8; target_field.offset = 16; type_field.offset = 12; params_field.offset = refs_field.offset = 8;
  delegate_field.offset=24;receiver_field.offset=28;
  return name=="delegate"?&delegate_field:name=="receiver"?&receiver_field:name == "handleKind" ? &kind_field : name == "artFieldOrMethod" ? &target_field :
      name == "type" ? &type_field : name == "ptypes" ? &params_field : name == "references" ? &refs_field : nullptr;
}
bool equivalent_types=false;
bool ExactType(void* type, uintptr_t other) { return equivalent_types||reinterpret_cast<uintptr_t>(type)==other; }
bool DescriptorEquals(void* klass, const char* descriptor) {
  return klass == reinterpret_cast<void*>(0x10000900) && std::strcmp(descriptor,"Ldalvik/system/EmulatedStackFrame;") == 0;
}
bool Dispatch(void*, void*, Handle, const abi::arm64::InstructionOperands*, uint64_t* result) {
  original_calls++; *result = 42; return true;
}
void* Refine(void*, void*, const int&, Handle, Handle, uint32_t, void*) { original_calls++; return &method; }

void* linker = reinterpret_cast<void*>(0x1000);
void* self = reinterpret_cast<void*>(0x2000);
void* referrer = reinterpret_cast<void*>(0x3000);
void Require(bool ok, const char* reason);
int bound_finds=0,bound_globals=0,bound_locals_deleted=0;
bool bound_find_fail=false,bound_global_fail=false,bound_reenter=false;
jclass JNICALL FindBound(JNIEnv* jni,const char* name){
 Require(std::strcmp(name,"java/lang/invoke/Transformers$BindTo")==0,"wrong bound bootstrap name");++bound_finds;
 if(bound_reenter)Require(!invoke_kind::PrepareBoundReceiverClass(jni),"reentrant bound class preparation accepted partial state");
 return bound_find_fail?nullptr:reinterpret_cast<jclass>(0x10000e80);
}
jobject JNICALL GlobalBound(JNIEnv*,jobject value){if(bound_global_fail)return nullptr;++bound_globals;return value;}
void JNICALL DeleteBoundLocal(JNIEnv*,jobject){++bound_locals_deleted;}
void JNICALL DeleteBoundGlobal(JNIEnv*,jobject){--bound_globals;}

bool missing_method = false;
abi::arm64::ArtMethodPrefix own_caller{};
struct FakeScope { void* link; int32_t capacity; uint32_t size; uint32_t root; };
struct FakeThread { uint32_t tid = 7; uint32_t marking = 0; void* scope = nullptr; } fake_thread;
namespace artdcevm::abi::arm64 { struct RootInfo {}; }
const abi::arm64::RootInfo root_info{};
bool moving = false, other_loader = false, other_class = false;
void* symbolic_type = nullptr;
void* LookupType(void* l, uint16_t index, void* cache, void* loader) {
  Require(l == linker && index == 4 && cache == reinterpret_cast<void*>(0x10000300) &&
          loader == reinterpret_cast<void*>(1), "wrong symbolic owner lookup ABI");
  return symbolic_type;
}
int selections = 0;
int cache_reads = 0;
uintptr_t cache_value = 0x7000;
uintptr_t CacheRead(void*) { cache_reads++; return cache_value; }
bool use_barrier = true;
void* Mark(void* object) { return object == reinterpret_cast<void*>(0x9000) ? reinterpret_cast<void*>(0x8000) : object; }
void Visit(void* thread, abi::arm64::RootVisitor* visitor, uint32_t tid) {
  Require(thread == &fake_thread && tid == 7, "incorrect root visitor ABI");
  auto* scope = static_cast<FakeScope*>(fake_thread.scope);
  uint32_t* roots[] = {&scope->root};
  Require(scope->capacity == 1 && scope->size == 1, "incorrect HandleScope declaration");
  visitor->VisitRoots(roots, 1, root_info);
}
const char* Descriptor(void* m) { return m == referrer && other_class ? "LOther;" : "LSubject;"; }
uintptr_t Loader(void* m) { return m == referrer && other_loader ? 2 : 1; }
void* Selection(void*, void*, uintptr_t*, const uint16_t*, bool, bool*) { selections++; return &own_caller; }
const char* expected_type = "Ljava/lang/IncompatibleClassChangeError;";
void Require(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
void* Current() { return self; }
void Throw(void* thread, const char* type, const char*) {
  Require(thread == self, "incorrect Thread ABI");
  Require(std::strcmp(type, expected_type) == 0, "wrong exception");
  exceptions++;
}
void* Resolve(void* l, uint32_t index, void* caller) {
  Require(l == linker && index == 9 && caller == referrer, "incorrect resolver ABI");
  if (moving) {
    auto* scope = static_cast<FakeScope*>(fake_thread.scope);
    Require(scope && scope->root == 0x4000, "receiver was not rooted across resolution");
    scope->root = 0x5000;
  }
  return missing_method ? nullptr : &method;
}
// Android 14--15 ResolveMethod<kNoChecks>(Thread*, ...) continues into the
// cache overload on a DEX cache miss, which production also hooks.
void* (*cache_hook)(void*, uint32_t, Handle, Handle, void*, int) = nullptr;
void* Caller(void* l, void* thread, uint32_t index, void* caller, int type) {
  Require(thread == self, "incorrect caller Thread");
  return cache_hook ? cache_hook(l, index, {}, {}, caller, type) : Resolve(l, index, caller);
}
void* Cache(void* l, uint32_t index, Handle cache, Handle loader, void* caller, int) {
  Require(cache.reference == nullptr && loader.reference == nullptr, "incorrect Handle ABI");
  return Resolve(l, index, caller);
}
uintptr_t Nterp(void* thread, void* caller, const uint16_t*) {
  Require(thread == self && caller == referrer, "incorrect nterp ABI"); original_calls++; return 42;
}
extern "C" void* shadowhook_hook_sym_addr(void* target, void* replacement, void** original) {
  hook_attempts++;
  if (hook_failure) return nullptr;
  installed_replacement = replacement; *original = target; return target;
}
extern "C" void* shadowhook_dlopen(const char*) { return linker; }
extern "C" void shadowhook_dlclose(void*) {}
extern "C" void* shadowhook_dlsym(void*, const char* name) {
  using namespace symbols::invoke_kind;
  if (mode == 0) return nullptr;
  if (mode >= 17) {
    if (name == std::string(symbols::structural::kFindInstanceField.name)) return HookAddress(FindField);
    if (name == std::string(kClassDescriptorEquals.name)) return mode==20?nullptr:HookAddress(DescriptorEquals);
    if (name == std::string(kMethodTypeExact.name)) return mode==21?nullptr:HookAddress(ExactType);
    if (mode != 19 && name == std::string(kHandleDispatch0.name)) return HookAddress(Dispatch);
    if (mode == 19 && name == std::string(kHandleRefineLegacy.name)) return HookAddress(Refine);
  }
  if (name == std::string(symbols::runtime_thread::kCurrentThread.name)) return HookAddress(Current);
  if (name == std::string(symbols::structural::kThrowNewException.name)) return HookAddress(Throw);
  // Case 10 is the Android 14--15 shape: selectors without ResolveMethodId.
  if ((mode == 1 || mode == 4 || (mode >= 6 && mode != 10 && mode != 11)) && name == std::string(kResolveMethodId.name)) return HookAddress(Resolve);
  if ((mode == 2 || mode == 3 || mode == 5 || mode == 10 || mode == 11) && name == std::string(kResolveCaller.name)) return HookAddress(Caller);
  if ((mode == 2 || mode == 3 || mode == 10 || mode == 11) && name == std::string(kResolveCache.name)) return HookAddress(Cache);
  if ((mode == 1 || mode == 2 || mode == 5 || mode >= 6) && name == std::string(kNterpGetMethod.name)) return HookAddress(Nterp);
  if (mode == 11 || mode == 16) {
    if (name == std::string(kObsoleteCacheLegacy.name)) return HookAddress(CacheRead);
    if (name == std::string(kSelectVirtual.name) || name == std::string(kSelectInterface.name) ||
        name == std::string(kObsoleteCacheRead.name) || name == std::string(kObsoleteCacheRaw.name)) return nullptr;
  }
  if (mode >= 6) {
    if ((mode == 12 || mode == 13 || mode >= 17) && name == std::string(symbols::interface_redefinition::kLookupCache.name)) return HookAddress(LookupType);
    if (mode == 13 && (name == std::string(kSelectVirtual.name) || name == std::string(kSelectInterface.name) ||
        name == std::string(kMethodLoader.name) || name == std::string(symbols::runtime_thread::kUseReadBarrier.name))) return nullptr;
    if (mode != 14 && mode != 15 && name == std::string(kSelectVirtual.name)) return HookAddress(Selection);
    if (mode != 7 && mode != 15 && name == std::string(kSelectInterface.name)) return HookAddress(Selection);
    if (mode != 8 && name == std::string(kMethodDescriptor.name)) return HookAddress(Descriptor);
    if (name == std::string(kMethodLoader.name)) return HookAddress(Loader);
    if (name == std::string(kObsoleteCacheRead.name) || name == std::string(kObsoleteCacheRaw.name)) return HookAddress(CacheRead);
    if (name == std::string(symbols::runtime_thread::kReadBarrierMark.name)) return HookAddress(Mark);
    if (name == std::string(symbols::runtime_thread::kUseReadBarrier.name)) return mode == 16 ? nullptr : &use_barrier;
    if (mode != 9 && name == std::string(symbols::interface_redefinition::kHandleScopeVisitRoots.name)) return HookAddress(Visit);
  }
  return nullptr;
}
extern "C" void* shadowhook_dlsym_dynsym(void* h, const char* n) { return shadowhook_dlsym(h,n); }
int main(int argc, char** argv) {
  try {
    Require(argc == 2, "case required"); mode = std::stoi(argv[1]);
    for (uint32_t precompiled : {0x00200000u, 0x00800000u}) {
      method.access_flags.store(precompiled | 1);
      invoke_kind::KeepInterpreted(&method, precompiled);
      Require(method.access_flags.load() == 0x02000001u, "precompiled caller remained compilable");
      for (uint32_t flag : {0x0100u, 0x0400u, 0x80000000u}) {
        method.access_flags.store(flag);
        invoke_kind::KeepInterpreted(&method, precompiled);
        Require(method.access_flags.load() == flag, "changed a native, abstract or intrinsic method");
      }
    }
    std::vector<Hook> hooks;
    bool supported = mode == 1 || mode == 2 || mode == 3 || mode >= 6;
    Require(invoke_kind::Prepare(&hooks) == supported, "wrong optional-service decision");
    if (!supported) {
      Require(hooks.empty(), "partial optional hooks escaped preparation");
      Require(!invoke_kind::Enable(), "enabled missing service family");
    } else {
      for (const auto& hook : hooks) *hook.original = hook.target;
      Require(!invoke_kind::Enable(), "enabled before installation");
      invoke_kind::Installed(true);
      Require(!invoke_kind::Enable(), "enabled without observed ClassLinker");
      invoke_kind::ObserveClassLinker(linker);
      Require(invoke_kind::Enable(), "could not enable complete guards");
      if (mode >= 17) {
        Require(hook_attempts == 0 && !invoke_kind::InterfaceSymbols() && !invoke_kind::EnableInterfaceSymbols(),
                "MethodHandle guard installed before verified declarations");
        constexpr auto legacy = abi::arm64::FrameReferenceFor<abi::arm64::android13::Class>();
        constexpr auto modern = abi::arm64::FrameReferenceFor<abi::arm64::android16::Class>();
        invoke_kind::ObserveClassLayout(0,4,8,8,offsetof(FakeThread,marking),8,12,mode==20 ? modern : legacy);
        if(mode==21){
          Require(!invoke_kind::InterfaceSymbols()&&!invoke_kind::EnableInterfaceSymbols()&&hook_attempts==0,
                  "missing named MethodType equality enabled an incomplete receiver guard");
          std::cout << "Invocation guard case " << mode << " PASS\n";return 0;
        }
        Require(invoke_kind::InterfaceSymbols(), "complete symbolic services unavailable");
        if (mode == 18) {
          hook_failure = true;
          Require(!invoke_kind::EnableInterfaceSymbols() && !installed_replacement, "failed lazy installation escaped admission");
          hook_failure = false;
        }
        Require(invoke_kind::EnableInterfaceSymbols(), "could not install symbolic MethodHandle selection");
        const int installed = hook_attempts;
        Require(invoke_kind::EnableInterfaceSymbols() && hook_attempts == installed, "installed lazy guard twice");
        void* page = mmap(reinterpret_cast<void*>(0x10000000),4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON|MAP_FIXED,-1,0);
        Require(page != MAP_FAILED, "cannot map MethodHandle fixtures");
        auto* declaring = reinterpret_cast<uint32_t*>(0x10000200); declaring[0] = 0x0200;
        method.declaring_class = 0x10000200;
        auto* receiver = reinterpret_cast<abi::arm64::Object*>(0x10000100); receiver->klass = 0x10000300;
        auto* klass = reinterpret_cast<uint32_t*>(0x10000300); klass[3] = 0x10000400;
        auto* table = reinterpret_cast<abi::arm64::ObjectArray*>(0x10000400); table->length = 2; table->elements[0] = 0x10000200;
        auto* mh = reinterpret_cast<uint32_t*>(0x10000500); mh[0] = 0x10000000; mh[2] = 4; mh[3] = 0x10000c00;
        *reinterpret_cast<uintptr_t*>(mh+4) = reinterpret_cast<uintptr_t>(&method);
        uint32_t mh_root = 0x10000500;
        alignas(8) uint8_t frame[128]{};
        size_t regs = mode == 20 ? offsetof(abi::arm64::android16::ShadowFrame,vregs) : offsetof(abi::arm64::android13::ShadowFrame,vregs);
        size_t count = mode == 20 ? offsetof(abi::arm64::android16::ShadowFrame,number_of_vregs) : offsetof(abi::arm64::android13::ShadowFrame,number_of_vregs);
        *reinterpret_cast<uint32_t*>(frame+count) = 4;
        auto* frame_receiver = reinterpret_cast<uint32_t*>(frame+regs+6*4); *frame_receiver = 0x10000100;
        struct Operands : abi::arm64::InstructionOperands {
          Operands() : InstructionOperands(1) {} uint32_t GetOperand(size_t i) const override { Require(i==0,"wrong operand");return 2; }
        } operands;
        auto* callsite = reinterpret_cast<uint32_t*>(0x10000600); callsite[0]=0x10000000;callsite[2]=0x10000700;
        auto* params = reinterpret_cast<abi::arm64::ObjectArray*>(0x10000700); params->length=1;params->elements[0]=0x10000b00;
        auto* handle_type=reinterpret_cast<uint32_t*>(0x10000c00); handle_type[0]=0x10000000;handle_type[2]=0x10000d00;
        auto* handle_params=reinterpret_cast<abi::arm64::ObjectArray*>(0x10000d00);handle_params->length=1;handle_params->elements[0]=0x10000b00;
        *reinterpret_cast<uint32_t*>(0x10000b00)=0x0200;
        uint32_t handle_type_root=0x10000c00;
        uint32_t callsite_root=0x10000600; int kind=4;
        auto call = [&]() {
          uint64_t result = 91;
          if (mode == 19) return reinterpret_cast<decltype(&Refine)>(installed_replacement)(self,frame,kind,{&handle_type_root}, {&callsite_root},2,&method) != nullptr;
          bool ok = reinterpret_cast<decltype(&Dispatch)>(installed_replacement)(self,frame,{&mh_root},&operands,&result);
          Require(result == (ok ? 42u : 0u), "wrong MethodHandle result on failure"); return ok;
        };
        Require(call() && original_calls==1, "valid parent handle did not delegate");
        table->length=0;
        Require(!call() && exceptions==1 && original_calls==1, "lost interface did not throw ICCE before ART reflection verification");
        handle_params->elements[0]=0x10000300;kind=0;mh[2]=0;expected_type="Ljava/lang/AbstractMethodError;";
        Require(!call() && exceptions==2, "virtual MethodHandle lost-slot failure was not AME");
        kind=4;mh[2]=4;
        Require(!call() && exceptions==3, "class receiver type was confused with ART interface kind");
        handle_params->elements[0]=0x10000b00;kind=0;mh[2]=0;expected_type="Ljava/lang/IncompatibleClassChangeError;";
        Require(!call() && exceptions==4, "interface receiver type was confused with ART virtual kind");
        handle_params->elements[0]=0x10000b00;kind=4;mh[2]=4;expected_type="Ljava/lang/IncompatibleClassChangeError;";
        *frame_receiver=0;
        Require(call(), "null receiver no longer delegates to ART NPE");
        *frame_receiver=0x10000100; declaring[0]=0;
        Require(call(), "ordinary class MethodHandle was intercepted");
        declaring[0]=0x0200; mh[2]=3;kind=3;
        Require(call(), "static handle was treated as interface dispatch");
        if (mode == 19) {
          kind=4; params->elements[0]=0x10000900;
          auto* emulated=reinterpret_cast<uint32_t*>(0x10000800);emulated[0]=0x10000000;emulated[2]=0x10000a00;
          auto* refs=reinterpret_cast<abi::arm64::ObjectArray*>(0x10000a00);refs->length=1;refs->elements[0]=0x10000100;
          *frame_receiver=0x10000800;
          Require(!call() && exceptions==5, "legacy caller-transformer receiver was not unwrapped");
        }
        auto* bound=reinterpret_cast<uint32_t*>(0x10000e00);
        bound[0]=0x10000e80;bound[2]=5;bound[3]=callsite_root;bound[6]=mh_root;bound[7]=0x10000100;
        JNINativeInterface_ bound_jt{};bound_jt.FindClass=FindBound;bound_jt.NewGlobalRef=GlobalBound;
        bound_jt.DeleteLocalRef=DeleteBoundLocal;bound_jt.DeleteGlobalRef=DeleteBoundGlobal;JNIEnv bound_jni{&bound_jt};
        bound_find_fail=true;Require(!invoke_kind::PrepareBoundReceiverClass(&bound_jni)&&!invoke_kind::BoundReceiverClass(),"missing bound class was published");bound_find_fail=false;
        bound_global_fail=true;Require(!invoke_kind::PrepareBoundReceiverClass(&bound_jni)&&!invoke_kind::BoundReceiverClass()&&bound_globals==0,"failed bound root was published");bound_global_fail=false;
        bound_reenter=true;Require(invoke_kind::PrepareBoundReceiverClass(&bound_jni)&&bound_globals==1,"bound preparation retry failed");bound_reenter=false;
        int prior_finds=bound_finds;
        Require(invoke_kind::PrepareBoundReceiverClass(&bound_jni)&&bound_finds==prior_finds&&bound_globals==1,"bound bootstrap root was prepared twice");
        invoke_kind::ObserveBoundReceiverClass(reinterpret_cast<void*>(0x10000e80));
        uint32_t bound_root=0x10000e00;
        params->length=0;mh[2]=4;method.access_flags.store(0);declaring[0]=0x0200;
        handle_params->elements[0]=0x10000b00;
        int before_bound=exceptions;
        auto bound_call=[&](uint32_t* type){return invoke_kind::RejectBoundInterfaceReceiver(self,{&bound_root},{type},frame,&operands);};
        table->length=2;
        Require(!bound_call(&callsite_root),"compatible bound receiver was intercepted");
        table->length=0;expected_type="Ljava/lang/IncompatibleClassChangeError;";
        Require(bound_call(&callsite_root)&&exceptions==++before_bound,"bound interface receiver did not throw ICCE");
        Require(!bound_call(&handle_type_root)&&exceptions==before_bound,"bound guard preceded outer type conversion");
        equivalent_types=true;
        Require(bound_call(&handle_type_root)&&exceptions==++before_bound,"named type equality was replaced with pointer identity");equivalent_types=false;
        bound[7]=0;Require(!bound_call(&callsite_root),"null bound receiver lost NPE ordering");bound[7]=0x10000100;
        method.access_flags.store(abi::arm64::kAccObsoleteMethod);
        Require(!bound_call(&callsite_root),"bound guard changed retired-target precedence");method.access_flags.store(0);
        mh[2]=3;Require(!bound_call(&callsite_root),"bound static argument was treated as a receiver");mh[2]=4;
        bound[0]=0x10000000;Require(!bound_call(&callsite_root),"unrelated transformer was intercepted");bound[0]=0x10000e80;
        auto* emulated=reinterpret_cast<uint32_t*>(0x10000800);emulated[0]=0x10000000;emulated[3]=callsite_root;
        uint32_t emulated_root=0x10000800;
        Require(invoke_kind::RejectBoundInterfaceReceiverWithFrame(self,{&bound_root},{&emulated_root})&&exceptions==++before_bound,
                "adapted bound frame bypassed receiver validation");
        emulated[3]=handle_type_root;
        Require(!invoke_kind::RejectBoundInterfaceReceiverWithFrame(self,{&bound_root},{&emulated_root}),"wrong bound frame type lost original validation");
        emulated[3]=callsite_root;
        if(mode==19){
          auto* legacy_type=reinterpret_cast<uint32_t*>(0x10000f00);legacy_type[0]=0x10000000;legacy_type[2]=0x10000f40;
          auto* legacy_params=reinterpret_cast<abi::arm64::ObjectArray*>(0x10000f40);legacy_params->length=1;legacy_params->elements[0]=0x10000900;
          uint32_t legacy_root=0x10000f00;*frame_receiver=emulated_root;
          Require(bound_call(&legacy_root)&&exceptions==++before_bound,"legacy bound caller frame was not unwrapped");
        }
        munmap(page,4096);
        std::cout << "Invocation guard case " << mode << " PASS\n"; return 0;
      }
      uint16_t instructions[] = {0x6e, 9, 0};
      method.access_flags.store(8);
      for (const auto& hook : hooks) {
        using namespace symbols::invoke_kind;
        if (std::string(hook.name) == kNterpGetMethod.name) {
          auto call = reinterpret_cast<uintptr_t (*)(void*,void*,const uint16_t*)>(hook.replacement);
          for (uint16_t opcode : {0x6e,0x6f,0x70,0x72,0x74,0x75,0x76,0x78}) {
            instructions[0] = opcode; int before = original_calls;
            Require(call(self,referrer,instructions) == 0 && original_calls == before,
                    "incompatible method reached nterp cache fill");
          }
          for (uint16_t opcode : {0x71,0x77}) {
            instructions[0] = opcode; Require(call(self,referrer,instructions) == 42, "valid static rejected");
          }
          method.access_flags.store(0);
          for (uint16_t opcode : {0x71,0x77}) {
            instructions[0] = opcode; int before = original_calls;
            Require(call(self,referrer,instructions) == 0 && original_calls == before,
                    "instance method reached static nterp cache fill");
          }
          instructions[0] = 0x6e;
          Require(call(self,referrer,instructions) == 42, "valid instance rejected");
          method.access_flags.store(2); expected_type = "Ljava/lang/IllegalAccessError;";
          for (uint16_t opcode : {0x6e,0x6f,0x72,0x74,0x75,0x78}) {
            instructions[0] = opcode; int before = original_calls;
            Require(call(self,referrer,instructions) == 0 && original_calls == before,
                    "private method reached virtual/super/interface cache fill");
          }
          for (uint16_t opcode : {0x70,0x76}) {
            instructions[0] = opcode;
            Require(call(self,referrer,instructions) == 42, "legal private direct call rejected");
          }
          expected_type = "Ljava/lang/IncompatibleClassChangeError;"; method.access_flags.store(8);
          missing_method = true; instructions[0] = 0x6e; int before = exceptions;
          Require(call(self,referrer,instructions) == 0 && exceptions == before, "replaced existing resolver failure");
          missing_method = false;
        } else if (std::string(hook.name) == kResolveCaller.name) {
          auto call = reinterpret_cast<decltype(&Caller)>(hook.replacement);
          Require(call(linker,self,9,referrer,2) == nullptr, "cached virtual mismatch accepted");
          Require(call(linker,self,9,referrer,0) == &method, "valid static caller rejected");
          method.access_flags.store(0);
          Require(call(linker,self,9,referrer,2) == &method, "valid virtual caller rejected");
          Require(call(linker,self,9,referrer,0) == nullptr, "cached static mismatch accepted");
          method.access_flags.store(2); expected_type = "Ljava/lang/IllegalAccessError;";
          for (int type : {2,3,4}) Require(call(linker,self,9,referrer,type) == nullptr, "cached private mismatch accepted");
          Require(call(linker,self,9,referrer,1) == &method, "legal private caller rejected");
          expected_type = "Ljava/lang/IncompatibleClassChangeError;"; method.access_flags.store(8);
        } else if (std::string(hook.name) == kResolveCache.name) {
          auto call = reinterpret_cast<decltype(&Cache)>(hook.replacement);
          Require(call(linker,9,{}, {},referrer,2) == nullptr, "cold virtual mismatch accepted");
          Require(call(linker,9,{}, {},referrer,0) == &method, "valid cold static rejected");
          method.access_flags.store(0);
          Require(call(linker,9,{}, {},referrer,0) == nullptr, "cold static mismatch accepted");
          Require(call(linker,9,{}, {},referrer,2) == &method, "valid cold instance rejected");
          method.access_flags.store(2); expected_type = "Ljava/lang/IllegalAccessError;";
          for (int type : {2,3,4}) Require(call(linker,9,{}, {},referrer,type) == nullptr, "cold private mismatch accepted");
          Require(call(linker,9,{}, {},referrer,1) == &method, "legal cold private call rejected");
          expected_type = "Ljava/lang/IncompatibleClassChangeError;"; method.access_flags.store(8);
        }
      }
      Require(exceptions > 0, "no invocation mismatch tested");
      if (mode >= 6) {
        abi::arm64::RuntimeThread thread_layout{offsetof(FakeThread,tid),0,0,0,offsetof(FakeThread,scope)};
        abi::arm64::InterfaceRuntime layout{};
        layout.scope_header_size = offsetof(FakeScope,root); layout.scope_link = offsetof(FakeScope,link);
        layout.scope_capacity = offsetof(FakeScope,capacity); layout.scope_size = offsetof(FakeScope,size);
        FakeScope admission{nullptr,1,1,0x6000}; fake_thread.scope = &admission;
        invoke_kind::ObserveScope(&fake_thread,&thread_layout,&layout,{nullptr});
        Require(!invoke_kind::SelfCalls(), "unprobed scope enabled self calls");
        invoke_kind::ObserveScope(&fake_thread,&thread_layout,&layout,{&admission.root});
        Require(!invoke_kind::SelfCalls(), "unprobed Class enabled self calls");
        if (mode == 11 || mode == 16) {
          for (const auto& hook : hooks)
            if (std::string(hook.name) == symbols::invoke_kind::kObsoleteCacheLegacy.name)
              Require(reinterpret_cast<decltype(&CacheRead)>(hook.replacement)(&method) == 0x7000,
                      "unprobed legacy cache bypassed ART");
        }
        if (mode == 16) {
          bool found = false;
          for (const auto& hook : hooks) found |= std::string(hook.name) == symbols::invoke_kind::kObsoleteCacheLegacy.name;
          Require(found, "legacy copied-obsolete cache hook requires an unavailable modern collector flag");
          self = &fake_thread;
          fake_thread.marking = 1;
        }
        invoke_kind::ObserveClassLayout(0,4,8,8,offsetof(FakeThread,marking),8);
        const bool complete = mode == 6 || mode == 10 || mode == 12;
        Require(invoke_kind::SelfCalls() == complete && invoke_kind::SelectionServices() == complete,
                "combined selection capability is not the conjunction");
        Require(invoke_kind::SelfCalls(false) == (complete || mode == 7) &&
                invoke_kind::SelfCalls(true) == (complete || mode == 14), "wrong per-kind self admission");
        for (bool iface : {false, true})
          Require(invoke_kind::SelectionServices(iface) == (invoke_kind::SelectionUnavailable(iface) == nullptr),
                  "selection reason disagrees with capability");
        if (complete || mode == 7 || mode == 8 || mode == 9 || mode == 14 || mode == 15 || mode == 11 || mode == 13 || mode == 16) {
          void* klass = mmap(reinterpret_cast<void*>(0x10000000),4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON|MAP_FIXED,-1,0);
          Require(klass != MAP_FAILED, "unable to allocate low-address class fixture");
          auto* fields = static_cast<uint32_t*>(klass); fields[0] = 0x00200000; fields[1] = 0x9000;
          // Fake ClassExt and PointerArray in the same low-address page.
          fields[2] = 0x10000100; auto* ext = reinterpret_cast<uint32_t*>(0x10000100);
          auto* obsolete = reinterpret_cast<abi::arm64::PointerArray*>(0x10000200);
          ext[2] = 0x10000200; obsolete->length = 0;
          method.declaring_class = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(klass));
          for (const auto& hook : hooks) {
            bool read = std::string(hook.name) == symbols::invoke_kind::kObsoleteCacheRead.name ||
                        std::string(hook.name) == symbols::invoke_kind::kObsoleteCacheLegacy.name;
            if (!read && std::string(hook.name) != symbols::invoke_kind::kObsoleteCacheRaw.name) continue;
            auto call = reinterpret_cast<decltype(&CacheRead)>(hook.replacement);
            method.access_flags.store(0x01040000);
            Require(call(&method) == (read ? 0x8000u : 0x9000u), "copied obsolete frame used an unrelated DEX cache");
            obsolete->length = 1; obsolete->elements[0] = reinterpret_cast<uintptr_t>(&method);
            Require(call(&method) == 0x7000, "earlier non-structural frame lost its obsolete-map DEX");
            obsolete->length = 0;
            use_barrier = false; if (mode == 16) fake_thread.marking = 0;
            Require(call(&method) == 0x9000u, "CMC copied cache used the CC marker");
            use_barrier = true; if (mode == 16) fake_thread.marking = 1;
            fields[0] = 0;
            Require(call(&method) == 0x7000, "non-structural obsolete cache bypassed ART");
            fields[0] = 0x00200000; method.access_flags.store(0x00040000);
            Require(call(&method) == 0x7000, "declared obsolete cache bypassed ART");
          }
          Require(cache_reads == (mode == 13 ? 0 : (mode == 11 || mode == 16) ? 4 : 6), "wrong obsolete cache delegation");
          if (mode == 12 || mode == 13) {
            uint8_t bytes[256]{};
            uint32_t size = sizeof(bytes), count = 10, offset = 0x70;
            std::memcpy(bytes + 32, &size, 4); std::memcpy(bytes + 88, &count, 4);
            std::memcpy(bytes + 92, &offset, 4); bytes[0x70 + 9 * 8] = 4;
            abi::arm64::DexFilePrefix dex{};
            dex.begin = dex.data_begin = bytes; dex.data_size = sizeof(bytes);
            auto* cache = reinterpret_cast<abi::arm64::DexCache*>(0x10000300);
            cache->class_loader = 1;
            cache->dex_file = reinterpret_cast<uintptr_t>(&dex);
            fields[1] = 0x10000300;
            symbolic_type = reinterpret_cast<void*>(0x10000500);
            auto* symbolic_flags = static_cast<uint32_t*>(symbolic_type);
            own_caller.declaring_class = 0x10000000; own_caller.access_flags.store(1);
            if (mode == 13) {
              self = &fake_thread; cache_value = 0x10000300;
              own_caller.access_flags.store(abi::arm64::kAccObsoleteMethod);
            }
            referrer = &own_caller; method.access_flags.store(1);
            *symbolic_flags = 0x0200;
            fields[0] = 0;
            Require(invoke_kind::InterfaceResolution(cache, 9), "canonical class target lost its interface symbolic owner");
            *symbolic_flags = 0;
            fields[0] = 0x0200;
            Require(!invoke_kind::InterfaceResolution(cache, 9), "copied default confused class-owned cache identity");
            Require(!invoke_kind::InterfaceResolution(cache, 10), "invalid method ID became an interface entry");
            symbolic_type = nullptr;
            Require(!invoke_kind::InterfaceResolution(cache, 9), "unresolved owner was accepted");
            symbolic_type = reinterpret_cast<void*>(0x10000500);
            Require(invoke_kind::ClassKinds() && invoke_kind::EnableClassKinds(), "complete kind guards unavailable");
            for (const auto& hook : hooks) {
              if (std::string(hook.name) != symbols::invoke_kind::kNterpGetMethod.name) continue;
              auto call = reinterpret_cast<uintptr_t (*)(void*,void*,const uint16_t*)>(hook.replacement);
              uint16_t inst[]{0x6e,9,0};
              *symbolic_flags = 0;
              // The resolved method can belong to an interface (copied
              // default); only the DEX's symbolic class determines legality.
              fields[0] = 0x0200;
              Require(call(self,referrer,inst) == 42, "legal class call to inherited interface default rejected");
              *symbolic_flags = 0x0200; fields[0] = 0;
              Require(call(self,referrer,inst) == 0, "old virtual interface owner entered a vtable");
              method.access_flags.store(2);
              Require(call(self,referrer,inst) == 0, "private method access masked the symbolic class/interface ICCE");
              method.access_flags.store(1);
              inst[0] = 0x72;
              Require(call(self,referrer,inst) == 42, "interface call to Object declaration rejected");
              *symbolic_flags = 0;
              Require(call(self,referrer,inst) == 0, "old interface class owner entered an IMT");
              symbolic_type = nullptr;
              Require(call(self,referrer,inst) == 0, "missing symbolic owner was accepted");
              symbolic_type = reinterpret_cast<void*>(0x10000500);
              struct Constructor { uint32_t owner; std::atomic<uint32_t> flags; uint32_t index; } ctor{0x10000000, 0x00050000, 9};
              cache_value = 0x10000300;
              *symbolic_flags = 0x0600;
              Require(invoke_kind::UninstantiableConstructor(&ctor), "retired constructor ignored current interface instantiability");
              *symbolic_flags = 0;
              Require(!invoke_kind::UninstantiableConstructor(&ctor), "restored concrete class remained uninstantiable");
            }
            // Later self-call cases use a synthetic caller with no DEX.
            invoke_kind::Installed(false);
            munmap(klass,4096);
            std::cout << "Invocation guard case " << mode << " PASS\n";
            return 0;
          }
          munmap(klass,4096); method.declaring_class = 0;
          self = &fake_thread; referrer = &own_caller;
          own_caller.access_flags.store(abi::arm64::kAccObsoleteMethod);
          uint16_t inst[]{0x6e,9,0}; bool string_init = false;
          for (const auto& hook : hooks)
            if (std::string(hook.name) == symbols::invoke_kind::kResolveCache.name)
              cache_hook = reinterpret_cast<decltype(cache_hook)>(hook.replacement);
          Require((cache_hook != nullptr) == (mode == 10 || mode == 11), "unexpected cache overload hook");
          for (const auto& hook : hooks) {
            if (std::string(hook.name) != symbols::invoke_kind::kSelectVirtual.name &&
                std::string(hook.name) != symbols::invoke_kind::kSelectInterface.name) continue;
            auto call = reinterpret_cast<decltype(&Selection)>(hook.replacement);
            method.access_flags.store(2); uintptr_t receiver = 0x4000; moving = true;
            Require(call(self,referrer,&receiver,inst,false,&string_init) == &method, "own private target not selected directly");
            Require(receiver == 0x5000 && fake_thread.scope == &admission && selections == 0, "receiver/root chain not refreshed");
            moving = false; receiver = 0;
            expected_type = "Ljava/lang/NullPointerException;";
            Require(call(self,referrer,&receiver,inst,false,&string_init) == nullptr, "own null call accepted");
            receiver = 0x4000; expected_type = "Ljava/lang/IllegalAccessError;";
            other_loader = true;
            Require(call(self,referrer,&receiver,inst,false,&string_init) == nullptr, "same descriptor in another loader gained private access");
            other_loader = false; other_class = true;
            Require(call(self,referrer,&receiver,inst,false,&string_init) == nullptr, "another class gained private access");
            other_class = false;
            int before = exceptions;
            Require(call(self,referrer,&receiver,inst,true,&string_init) == nullptr && exceptions == before, "lookup-only performed resolution");
            missing_method = true;
            Require(call(self,referrer,&receiver,inst,false,&string_init) == nullptr && exceptions == before, "overwrote resolver failure");
            missing_method = false; method.access_flags.store(8); expected_type = "Ljava/lang/IncompatibleClassChangeError;";
            Require(call(self,referrer,&receiver,inst,false,&string_init) == nullptr, "own static mismatch accepted");
            method.access_flags.store(1);
            Require(call(self,referrer,&receiver,inst,false,&string_init) == &own_caller, "ordinary dispatch bypassed ART");
            selections = 0;
          }
        }
      }
      invoke_kind::Installed(false);
      Require(!invoke_kind::Enable(), "enabled after installer rollback");
      Require(!invoke_kind::SelfCalls(false) && !invoke_kind::SelfCalls(true), "self admission survived rollback");
    }
    std::cout << "Invocation guard case " << mode << " PASS\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
'''


def main():
    evidence = new_evidence_directory(ROOT / 'build/evidence/host/invoke-kind')
    helper = runpy.run_path(str(ROOT / 'tools/test-structural-policy-host.py'))
    include = helper['java_home'](None) / 'include'
    platform = 'darwin' if (include / 'darwin').is_dir() else 'linux'
    with tempfile.TemporaryDirectory(prefix='cases-', dir=evidence) as temp:
        work = Path(temp)
        (work / 'shadowhook.h').write_text('#pragma once\nextern "C" {\n'
            'void* shadowhook_dlopen(const char*); void shadowhook_dlclose(void*);\n'
            'void* shadowhook_hook_sym_addr(void*,void*,void**);\n'
            'void* shadowhook_dlsym(void*,const char*); void* shadowhook_dlsym_dynsym(void*,const char*);\n}\n')
        source = work / 'test.cc'; source.write_text(SOURCE)
        native = ROOT / 'artdcevm/src/main/cpp'; binary = work / 'test'
        command = [shutil.which('clang++') or shutil.which('c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                   '-I' + str(work), '-I' + str(native), '-I' + str(include), '-I' + str(include / platform),
                   '-I' + str(ROOT / 'third_party/jvmti'), str(source), str(native / 'platform/common/invoke_kind.cc'), '-o', str(binary)]
        if sys.platform == 'darwin':
            # ART heap references are 32-bit; reserve a low-address fake Class.
            command.extend(['-Wl,-pagezero_size,0x1000'])
        result = subprocess.run(command, capture_output=True, text=True)
        (evidence / 'compile.log').write_text(result.stdout + result.stderr)
        if result.returncode: raise RuntimeError('Guard compilation failed; inspect ignored evidence')
        for case in range(22):
            result = subprocess.run([str(binary), str(case)], capture_output=True, text=True)
            (evidence / ('case-' + str(case) + '.log')).write_text(result.stdout + result.stderr)
            if result.returncode: raise RuntimeError('Guard case failed; inspect ignored evidence')
            print(result.stdout.strip())
    print('Evidence: ' + str(evidence.relative_to(ROOT)))


if __name__ == '__main__':
    main()
